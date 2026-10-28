#pragma once
#ifdef USE_OPENSSL

#include <openssl/ssl.h>

#include <array>
#include <atomic>
#include <deque>
#include <memory>

#include "xtils/net/tcp_client.h"
#include "xtils/net/transport/transport.h"

namespace xtils {

class TaskRunner;

class OpenSslContext final : public TlsContext {
 public:
  static std::shared_ptr<OpenSslContext> Create(const TlsCertConfig& cfg) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (!ctx) return nullptr;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION);

    if (!cfg.ca_file.empty()) {
      SSL_CTX_load_verify_locations(ctx, cfg.ca_file.c_str(), nullptr);
    }
    if (cfg.verify_peer)
      SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                         nullptr);
    else {
      SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    }

    if (!cfg.cert_file.empty()) {
      SSL_CTX_use_certificate_file(ctx, cfg.cert_file.c_str(),
                                   SSL_FILETYPE_PEM);
      SSL_CTX_use_PrivateKey_file(ctx, cfg.key_file.c_str(), SSL_FILETYPE_PEM);
    }

    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE |
                              SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    return std::shared_ptr<OpenSslContext>(new OpenSslContext(ctx));
  }

  ~OpenSslContext() override { SSL_CTX_free(ctx_); }

  TlsEngine engine() const override { return TlsEngine::kOpenSSL; }

  SSL_CTX* ctx() const { return ctx_; }

 private:
  explicit OpenSslContext(SSL_CTX* ctx) : ctx_(ctx) {}
  SSL_CTX* ctx_;
};

class TlsTransport final : public Transport, public TcpClientEventListener {
 public:
  explicit TlsTransport(TaskRunner* runner, TransportEventListener* listener);
  ~TlsTransport() override;

  bool Connect(const HttpUrl& url, TlsContextPtr ctx) override;
  // Non-blocking, bounded 8 MiB / 1024-record queue. false on backpressure
  // or TLS failure. Send accepts either the whole buffer or none on capacity
  // rejection; callers must retry later or close on false.
  // All transport methods and destruction must run on the supplied runner.
  bool Send(const void* data, size_t len) override;
  SendStatus TrySend(const void* data, size_t len) override;
  void Close() override;

 private:
  // TcpClientEventListener
  void OnConnected(bool success) override;
  void OnDataReceived(const void* data, size_t len) override;
  void OnDisconnected() override;
  void OnError(const std::string& error) override;
  bool OnReadable() override;

  void ContinueHandshake();
  void DrainSSLRead();
  bool FlushWrites();
  void NotifyWriteCapacity();
  void ScheduleRead();
  bool WaitForIO(int ssl_error);
  void Fail(const std::string& reason);

 private:
  enum class TLSState { kIdle, kHandshaking, kConnected, kError };

  TaskRunner* runner_;
  std::unique_ptr<TcpClient> tcp_;

  TlsContextPtr ctx_;
  SSL* ssl_;
  std::atomic<TLSState> state_;

  std::string host_;
  std::deque<std::string> pending_writes_;
  size_t write_offset_ = 0;
  size_t queued_bytes_ = 0;
  bool write_capacity_waiting_ = false;
  bool read_scheduled_ = false;
  uint64_t connection_generation_ = 0;
  bool read_wants_write_ = false;
  bool write_wants_write_ = false;
  std::array<uint8_t, 16384> read_buffer_{};
  WeakPtrFactory<TlsTransport> weak_factory_{this};
};

}  // namespace xtils

#endif  // USE_OPENSSL
