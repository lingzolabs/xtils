#ifdef USE_OPENSSL

#include <arpa/inet.h>
#include <openssl/err.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"
#include "xtils/net/transport/tls_transport.h"
#include "xtils/net/transport/transport.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {
namespace {

// OpenSSL's default socket BIO uses write(), which can terminate the process
// with SIGPIPE if a peer disconnects. Suppress it per send, not process-wide.
BIO_METHOD* SocketBioMethod() {
  static const std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> method(
      [] {
        BIO_METHOD* m = BIO_meth_new(BIO_TYPE_SOCKET, "xtils socket");
        if (!m) return m;
        const BIO_METHOD* socket = BIO_s_socket();
        BIO_meth_set_create(m, BIO_meth_get_create(socket));
        BIO_meth_set_destroy(m, BIO_meth_get_destroy(socket));
        BIO_meth_set_ctrl(m, BIO_meth_get_ctrl(socket));
        BIO_meth_set_read_ex(
            m, [](BIO* bio, char* data, size_t len, size_t* size) {
              BIO_clear_retry_flags(bio);
              auto n = ::recv(BIO_get_fd(bio, nullptr), data, len, 0);
              if (n < 0 &&
                  (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                BIO_set_retry_read(bio);
              *size = n > 0 ? static_cast<size_t>(n) : 0;
              return n > 0 ? 1 : 0;
            });
        BIO_meth_set_write_ex(m, [](BIO* bio, const char* data, size_t len,
                                    size_t* size) {
          BIO_clear_retry_flags(bio);
          auto n = ::send(BIO_get_fd(bio, nullptr), data, len, MSG_NOSIGNAL);
          if (n < 0 &&
              (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            BIO_set_retry_write(bio);
          *size = n > 0 ? static_cast<size_t>(n) : 0;
          return n > 0 ? 1 : 0;
        });
        return m;
      }(),
      BIO_meth_free);
  return method.get();
}

}  // namespace

TlsTransport::TlsTransport(TaskRunner* runner, TransportEventListener* listener)
    : Transport(listener),
      runner_(runner),
      ctx_(nullptr),
      ssl_(nullptr),
      state_(TLSState::kIdle) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);

  // OpenSSL 1.1+ initializes itself in a thread-safe manner.
}

TlsTransport::~TlsTransport() { Close(); }

bool TlsTransport::Connect(const HttpUrl& url, TlsContextPtr ctx) {
  if (state_ != TLSState::kIdle || !ctx || ctx->engine() != TlsEngine::kOpenSSL)
    return false;
  tcp_ = std::make_unique<TcpClient>(runner_, this);

  host_ = url.host;
  ctx_ = ctx;
  state_ = TLSState::kIdle;
  return tcp_->ConnectToHost(url.host, url.port);
}

void TlsTransport::OnConnected(bool success) {
  if (!success) {
    Fail("TCP connect failed");
    return;
  }

  ssl_ = SSL_new(reinterpret_cast<OpenSslContext*>(ctx_.get())->ctx());
  if (!ssl_) {
    Fail("SSL_new failed");
    return;
  }
  auto* method = SocketBioMethod();
  BIO* bio = method ? BIO_new(method) : nullptr;
  if (!bio) {
    Fail("Failed to allocate TLS socket BIO");
    return;
  }
  BIO_set_fd(bio, tcp_->GetSocketFd(), BIO_NOCLOSE);
  SSL_set_bio(ssl_, bio, bio);
  // LogI("host: %s", host_.c_str());
  unsigned char address[sizeof(in6_addr)];
  const bool is_ip = inet_pton(AF_INET, host_.c_str(), address) == 1 ||
                     inet_pton(AF_INET6, host_.c_str(), address) == 1;
  if (!is_ip) SSL_set_tlsext_host_name(ssl_, host_.c_str());
  if (SSL_get_verify_mode(ssl_) != SSL_VERIFY_NONE) {
    auto* param = SSL_get0_param(ssl_);
    int ok = is_ip ? X509_VERIFY_PARAM_set1_ip_asc(param, host_.c_str())
                   : SSL_set1_host(ssl_, host_.c_str());
    if (ok != 1) {
      Fail("Failed to configure TLS peer identity verification");
      return;
    }
  }
  SSL_set_connect_state(ssl_);

  state_ = TLSState::kHandshaking;
  ContinueHandshake();
}

void TlsTransport::ContinueHandshake() {
  int ret = SSL_connect(ssl_);
  if (ret == 1) {
    state_ = TLSState::kConnected;
    tcp_->SetWritableInterest(false);
    if (listener_) listener_->OnConnected(true);
    return;
  }
  int ssl_err = SSL_get_error(ssl_, ret);

  switch (ssl_err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      WaitForIO(ssl_err);
      return;

    case SSL_ERROR_ZERO_RETURN:
      Fail("TLS closed by peer");
      return;

    case SSL_ERROR_SYSCALL:
      perror("SSL syscall error");
      Fail("TLS syscall error");
      return;

    case SSL_ERROR_SSL: {
      unsigned long e;
      char buf[256];
      while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        // LogI("SSL_ERROR_SSL: %s", buf);
      }
      Fail("TLS protocol error");
      return;
    }

    default:
      Fail("Unknown TLS error");
      return;
  }
}

void TlsTransport::OnDataReceived(const void*, size_t) {
  LogW("TlsTransport::OnDataReceived should not be called");
}

bool TlsTransport::OnReadable() {
  if (state_ == TLSState::kHandshaking) {
    ContinueHandshake();
    return true;
  } else if (state_ == TLSState::kConnected) {
    auto weak = weak_factory_.GetWeakPtr();
    if (read_wants_write_) {
      DrainSSLRead();
      if (!weak || state_ != TLSState::kConnected || read_wants_write_)
        return true;
    }
    if (!FlushWrites()) return true;
    if (!pending_writes_.empty() && !write_wants_write_) return true;
    DrainSSLRead();
  }
  // The TCP layer must never consume encrypted bytes, even after TLS failure.
  return true;
}

void TlsTransport::DrainSSLRead() {
  size_t budget = 256 * 1024;
  while (state_ == TLSState::kConnected && ssl_) {
    int n = SSL_read(ssl_, read_buffer_.data(), read_buffer_.size());
    if (n > 0) {
      read_wants_write_ = false;
      auto weak = weak_factory_.GetWeakPtr();
      if (listener_)
        listener_->OnDataReceived(read_buffer_.data(), static_cast<size_t>(n));
      if (!weak) return;
      budget -= std::min(budget, static_cast<size_t>(n));
      if (budget == 0) {
        ScheduleRead();
        return;
      }
      continue;
    }

    int err = SSL_get_error(ssl_, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      read_wants_write_ = err == SSL_ERROR_WANT_WRITE;
      WaitForIO(err);
      return;
    }

    if (err == SSL_ERROR_ZERO_RETURN) {
      OnDisconnected();
      return;
    }
    if (state_ == TLSState::kConnected) {
      char reason[256];
      ERR_error_string_n(err, reason, sizeof(reason));
      Fail(reason);
    }
    break;
  }
}

void TlsTransport::ScheduleRead() {
  if (read_scheduled_ || state_ != TLSState::kConnected) return;
  read_scheduled_ = true;
  auto weak = weak_factory_.GetWeakPtr();
  auto generation = connection_generation_;
  runner_->PostTask([weak, generation] {
    if (!weak || weak->connection_generation_ != generation) return;
    weak->read_scheduled_ = false;
    if (weak->state_ == TLSState::kConnected) weak->OnReadable();
  });
}

SendStatus TlsTransport::TrySend(const void* data, size_t len) {
  if (len > kMaxSendChunkSize || (len && !data)) return SendStatus::kError;
  if (state_ != TLSState::kConnected) return SendStatus::kClosed;
  if (len > 8 * 1024 * 1024 - queued_bytes_ ||
      (len > 0 && pending_writes_.size() >= 1024)) {
    write_capacity_waiting_ = true;
    return SendStatus::kWouldBlock;
  }
  return Send(data, len) ? SendStatus::kAccepted : SendStatus::kError;
}

void TlsTransport::NotifyWriteCapacity() {
  if (!write_capacity_waiting_ || queued_bytes_ > 4 * 1024 * 1024 ||
      pending_writes_.size() >= 512)
    return;
  write_capacity_waiting_ = false;
  auto weak = weak_factory_.GetWeakPtr();
  auto generation = connection_generation_;
  runner_->PostTask([weak, generation] {
    if (weak && weak->state_ == TLSState::kConnected &&
        weak->connection_generation_ == generation && weak->listener_)
      weak->listener_->OnWriteCapacityAvailable();
  });
}

bool TlsTransport::Send(const void* data, size_t len) {
  if (state_ != TLSState::kConnected) return false;

  constexpr size_t kMaxQueuedBytes = 8 * 1024 * 1024;
  if (len > kMaxQueuedBytes - queued_bytes_ ||
      (len + 16383) / 16384 > 1024 - pending_writes_.size())
    return false;
  if (len == 0) return true;
  if (!data) return false;
  // Stable record-sized buffers preserve SSL_write retry arguments, even if
  // callers enqueue more data while a previous write is waiting for I/O.
  const char* bytes = static_cast<const char*>(data);
  for (size_t offset = 0; offset < len; offset += 16384) {
    pending_writes_.emplace_back(bytes + offset,
                                 std::min(size_t{16384}, len - offset));
  }
  queued_bytes_ += len;
  return read_wants_write_ || FlushWrites();
}

bool TlsTransport::WaitForIO(int ssl_error) {
  NotifyWriteCapacity();
  const bool writable = ssl_error == SSL_ERROR_WANT_WRITE ||
                        (!pending_writes_.empty() && write_wants_write_);
  if (!tcp_->SetWritableInterest(writable)) {
    Fail("TaskRunner does not support writable socket watches");
    return false;
  }
  return true;
}

bool TlsTransport::FlushWrites() {
  size_t budget = 256 * 1024;
  while (!pending_writes_.empty() && budget > 0) {
    const auto& front = pending_writes_.front();
    int n = SSL_write(ssl_, front.data() + write_offset_,
                      static_cast<int>(front.size() - write_offset_));
    if (n > 0) {
      write_offset_ += static_cast<size_t>(n);
      queued_bytes_ -= static_cast<size_t>(n);
      budget -= std::min(budget, static_cast<size_t>(n));
      if (write_offset_ == front.size()) {
        pending_writes_.pop_front();
        write_offset_ = 0;
      }
      continue;
    }
    int error = SSL_get_error(ssl_, n);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
      write_wants_write_ = error == SSL_ERROR_WANT_WRITE;
      return WaitForIO(error);
    }
    Fail("SSL_write failed");
    return false;
  }
  write_wants_write_ = !pending_writes_.empty();
  return WaitForIO(write_wants_write_ ? SSL_ERROR_WANT_WRITE
                                      : SSL_ERROR_WANT_READ);
}

void TlsTransport::OnDisconnected() {
  auto weak = weak_factory_.GetWeakPtr();
  if (state_ != TLSState::kError && listener_) {
    listener_->OnDisconnected();
  }
  if (weak) Close();
}

void TlsTransport::OnError(const std::string& error) { Fail(error); }

void TlsTransport::Fail(const std::string& reason) {
  state_ = TLSState::kError;
  tcp_->Disconnect();
  if (listener_) listener_->OnError(reason);
}

void TlsTransport::Close() {
  read_scheduled_ = false;
  ++connection_generation_;
  write_capacity_waiting_ = false;
  if (tcp_) {
    tcp_->Disconnect();
  }
  if (ssl_) {
    SSL_set_quiet_shutdown(ssl_, 1);
    SSL_free(ssl_);
    ssl_ = nullptr;
  }

  pending_writes_.clear();
  write_offset_ = 0;
  queued_bytes_ = 0;
  read_wants_write_ = false;
  write_wants_write_ = false;
  state_ = TLSState::kIdle;
}

}  // namespace xtils

#endif  // USE_OPENSSL
