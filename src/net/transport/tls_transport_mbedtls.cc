#ifdef USE_MBEDTLS

#include <mbedtls/error.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"
#include "xtils/net/transport/mbedtls_transport.h"
#include "xtils/net/transport/transport.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

// ---------- MbedTlsContext ----------

MbedTlsContext::MbedTlsContext() {
  mbedtls_ssl_config_init(&conf_);
  mbedtls_x509_crt_init(&ca_cert_);
  mbedtls_x509_crt_init(&client_cert_);
  mbedtls_pk_init(&client_key_);
  mbedtls_entropy_init(&entropy_);
  mbedtls_ctr_drbg_init(&ctr_drbg_);
}

MbedTlsContext::~MbedTlsContext() {
  mbedtls_ssl_config_free(&conf_);
  // All contexts were initialized, including partially parsed failure paths.
  mbedtls_x509_crt_free(&ca_cert_);
  mbedtls_x509_crt_free(&client_cert_);
  mbedtls_pk_free(&client_key_);
  mbedtls_ctr_drbg_free(&ctr_drbg_);
  mbedtls_entropy_free(&entropy_);
}

std::shared_ptr<MbedTlsContext> MbedTlsContext::Create(
    const TlsCertConfig& cfg) {
  auto ctx = std::shared_ptr<MbedTlsContext>(new MbedTlsContext());

  // Seed the random number generator
  int ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg_, mbedtls_entropy_func,
                                  &ctx->entropy_, nullptr, 0);
  if (ret != 0) {
    LogE("mbedtls_ctr_drbg_seed failed: -0x%04x", -ret);
    return nullptr;
  }

  // Set up the SSL configuration for a TLS client
  ret = mbedtls_ssl_config_defaults(&ctx->conf_, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    LogE("mbedtls_ssl_config_defaults failed: -0x%04x", -ret);
    return nullptr;
  }

  // Set minimum TLS version to 1.2
  mbedtls_ssl_conf_min_tls_version(&ctx->conf_, MBEDTLS_SSL_VERSION_TLS1_2);

  mbedtls_ssl_conf_rng(&ctx->conf_, mbedtls_ctr_drbg_random, &ctx->ctr_drbg_);

  // Load CA certificates
  if (!cfg.ca_file.empty()) {
    ret = mbedtls_x509_crt_parse_file(&ctx->ca_cert_, cfg.ca_file.c_str());
    if (ret < 0) {
      LogW("mbedtls_x509_crt_parse_file CA failed: -0x%04x", -ret);
      // Non-fatal if verify_peer is false
    } else {
      mbedtls_ssl_conf_ca_chain(&ctx->conf_, &ctx->ca_cert_, nullptr);
    }
  }

  // Configure peer verification
  if (cfg.verify_peer) {
    mbedtls_ssl_conf_authmode(&ctx->conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
  } else {
    mbedtls_ssl_conf_authmode(&ctx->conf_, MBEDTLS_SSL_VERIFY_NONE);
  }

  // Load client certificate and key
  if (!cfg.cert_file.empty()) {
    ret =
        mbedtls_x509_crt_parse_file(&ctx->client_cert_, cfg.cert_file.c_str());
    if (ret != 0) {
      LogE("mbedtls_x509_crt_parse_file cert failed: -0x%04x", -ret);
      return nullptr;
    }

    ret = mbedtls_pk_parse_keyfile(&ctx->client_key_, cfg.key_file.c_str(),
                                   nullptr, mbedtls_ctr_drbg_random,
                                   &ctx->ctr_drbg_);
    if (ret != 0) {
      LogE("mbedtls_pk_parse_keyfile failed: -0x%04x", -ret);
      return nullptr;
    }

    mbedtls_ssl_conf_own_cert(&ctx->conf_, &ctx->client_cert_,
                              &ctx->client_key_);
  }

  return ctx;
}

// ---------- MbedTlsTransport ----------

MbedTlsTransport::MbedTlsTransport(TaskRunner* runner,
                                   TransportEventListener* listener)
    : Transport(listener),
      runner_(runner),
      ctx_(nullptr),
      state_(TLSState::kIdle) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);
}

MbedTlsTransport::~MbedTlsTransport() { Close(); }

bool MbedTlsTransport::Connect(const HttpUrl& url, TlsContextPtr ctx) {
  if (state_ != TLSState::kIdle || !ctx || ctx->engine() != TlsEngine::kMbedTLS)
    return false;
  tcp_ = std::make_unique<TcpClient>(runner_, this);

  host_ = url.host;
  ctx_ = ctx;
  state_ = TLSState::kIdle;
  return tcp_->ConnectToHost(url.host, url.port);
}

int MbedTlsTransport::MbedSend(void* ctx, const unsigned char* buf,
                               size_t len) {
  auto* self = static_cast<MbedTlsTransport*>(ctx);
  int fd = self->tcp_->GetSocketFd();
  ssize_t ret = ::send(fd, buf, len, MSG_NOSIGNAL);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  return static_cast<int>(ret);
}

int MbedTlsTransport::MbedRecv(void* ctx, unsigned char* buf, size_t len) {
  auto* self = static_cast<MbedTlsTransport*>(ctx);
  int fd = self->tcp_->GetSocketFd();
  ssize_t ret = ::recv(fd, buf, len, 0);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  return static_cast<int>(ret);
}

void MbedTlsTransport::OnConnected(bool success) {
  if (!success) {
    Fail("TCP connect failed");
    return;
  }

  auto* mbed_ctx = static_cast<MbedTlsContext*>(ctx_.get());

  mbedtls_ssl_init(&ssl_);
  ssl_inited_ = true;

  int ret = mbedtls_ssl_setup(&ssl_, mbed_ctx->config());
  if (ret != 0) {
    Fail("mbedtls_ssl_setup failed");
    return;
  }

  // Set SNI hostname
  ret = mbedtls_ssl_set_hostname(&ssl_, host_.c_str());
  if (ret != 0) {
    Fail("mbedtls_ssl_set_hostname failed");
    return;
  }

  // Set custom BIO callbacks using the raw TCP socket fd
  mbedtls_ssl_set_bio(&ssl_, this, MbedSend, MbedRecv, nullptr);

  // Keep the socket non-blocking throughout the handshake.
  state_ = TLSState::kHandshaking;
  ContinueHandshake();
}

void MbedTlsTransport::ContinueHandshake() {
  int ret = mbedtls_ssl_handshake(&ssl_);
  if (ret == 0) {
    state_ = TLSState::kConnected;
    tcp_->SetWritableInterest(false);
    if (listener_) listener_->OnConnected(true);
    return;
  }

  if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
    WaitForIO(ret);
    return;
  }

  // Real error
  char err_buf[128];
  mbedtls_strerror(ret, err_buf, sizeof(err_buf));
  Fail(std::string("TLS handshake failed: ") + err_buf);
}

void MbedTlsTransport::OnDataReceived(const void*, size_t) {
  LogW("MbedTlsTransport::OnDataReceived should not be called");
}

bool MbedTlsTransport::OnReadable() {
  if (state_ == TLSState::kHandshaking) {
    ContinueHandshake();
    return true;
  } else if (state_ == TLSState::kConnected) {
    auto weak = weak_factory_.GetWeakPtr();
    if (read_wants_write_) {
      DrainRead();
      if (!weak || state_ != TLSState::kConnected || read_wants_write_)
        return true;
    }
    if (!FlushWrites()) return true;
    if (!pending_writes_.empty() && !write_wants_write_) return true;
    DrainRead();
  }
  // TCP must never consume encrypted bytes, even after a TLS failure.
  return true;
}

void MbedTlsTransport::DrainRead() {
  size_t budget = 256 * 1024;
  while (state_ == TLSState::kConnected && ssl_inited_) {
    int n = mbedtls_ssl_read(&ssl_, read_buffer_.data(), read_buffer_.size());
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

    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
      read_wants_write_ = n == MBEDTLS_ERR_SSL_WANT_WRITE;
      WaitForIO(n);
      return;
    }

    if (n == 0) {
      Fail("Unexpected EOF without TLS close_notify");
      return;
    }
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      OnDisconnected();
      return;
    }

    if (state_ == TLSState::kConnected) {
      char err_buf[128];
      mbedtls_strerror(n, err_buf, sizeof(err_buf));
      Fail(err_buf);
    }
    break;
  }
}

void MbedTlsTransport::ScheduleRead() {
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

SendStatus MbedTlsTransport::TrySend(const void* data, size_t len) {
  if (len > kMaxSendChunkSize || (len && !data)) return SendStatus::kError;
  if (state_ != TLSState::kConnected) return SendStatus::kClosed;
  if (len > 8 * 1024 * 1024 - queued_bytes_ ||
      (len > 0 && pending_writes_.size() >= 1024)) {
    write_capacity_waiting_ = true;
    return SendStatus::kWouldBlock;
  }
  return Send(data, len) ? SendStatus::kAccepted : SendStatus::kError;
}

void MbedTlsTransport::NotifyWriteCapacity() {
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

bool MbedTlsTransport::Send(const void* data, size_t len) {
  if (state_ != TLSState::kConnected) return false;

  constexpr size_t kMaxQueuedBytes = 8 * 1024 * 1024;
  if (len > kMaxQueuedBytes - queued_bytes_ ||
      (len + 16383) / 16384 > 1024 - pending_writes_.size())
    return false;
  if (len == 0) return true;
  if (!data) return false;
  const char* bytes = static_cast<const char*>(data);
  for (size_t offset = 0; offset < len; offset += 16384) {
    pending_writes_.emplace_back(bytes + offset,
                                 std::min(size_t{16384}, len - offset));
  }
  queued_bytes_ += len;
  return read_wants_write_ || FlushWrites();
}

bool MbedTlsTransport::WaitForIO(int error) {
  NotifyWriteCapacity();
  const bool writable = error == MBEDTLS_ERR_SSL_WANT_WRITE ||
                        (!pending_writes_.empty() && write_wants_write_);
  if (!tcp_->SetWritableInterest(writable)) {
    Fail("TaskRunner does not support writable socket watches");
    return false;
  }
  return true;
}

bool MbedTlsTransport::FlushWrites() {
  size_t budget = 256 * 1024;
  while (!pending_writes_.empty() && budget > 0) {
    const auto& front = pending_writes_.front();
    const auto* data = reinterpret_cast<const unsigned char*>(front.data());
    // Preserve both buffer and length on WANT_READ/WANT_WRITE retries.
    int n = mbedtls_ssl_write(&ssl_, data + write_offset_,
                              front.size() - write_offset_);
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
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
      write_wants_write_ = n == MBEDTLS_ERR_SSL_WANT_WRITE;
      return WaitForIO(n);
    }
    Fail("mbedtls_ssl_write failed");
    return false;
  }
  write_wants_write_ = !pending_writes_.empty();
  return WaitForIO(write_wants_write_ ? MBEDTLS_ERR_SSL_WANT_WRITE
                                      : MBEDTLS_ERR_SSL_WANT_READ);
}

void MbedTlsTransport::OnDisconnected() {
  auto weak = weak_factory_.GetWeakPtr();
  if (state_ != TLSState::kError && listener_) {
    listener_->OnDisconnected();
  }
  if (weak) Close();
}

void MbedTlsTransport::OnError(const std::string& error) { Fail(error); }

void MbedTlsTransport::Fail(const std::string& reason) {
  state_ = TLSState::kError;
  tcp_->Disconnect();
  if (listener_) listener_->OnError(reason);
}

void MbedTlsTransport::Close() {
  read_scheduled_ = false;
  ++connection_generation_;
  write_capacity_waiting_ = false;
  if (ssl_inited_) {
    if (state_ == TLSState::kConnected) mbedtls_ssl_close_notify(&ssl_);
    mbedtls_ssl_free(&ssl_);
    ssl_inited_ = false;
  }
  if (tcp_) {
    tcp_->Disconnect();
  }
  pending_writes_.clear();
  write_offset_ = 0;
  queued_bytes_ = 0;
  read_wants_write_ = false;
  write_wants_write_ = false;
  state_ = TLSState::kIdle;
}

}  // namespace xtils

#endif  // USE_MBEDTLS
