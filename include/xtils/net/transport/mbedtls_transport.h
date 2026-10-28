#pragma once
#ifdef USE_MBEDTLS

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <array>
#include <atomic>
#include <deque>
#include <memory>

#include "xtils/net/tcp_client.h"
#include "xtils/net/transport/transport.h"

namespace xtils {

class TaskRunner;

class MbedTlsContext final : public TlsContext {
 public:
  static std::shared_ptr<MbedTlsContext> Create(const TlsCertConfig& cfg);

  ~MbedTlsContext() override;

  TlsEngine engine() const override { return TlsEngine::kMbedTLS; }

  mbedtls_ssl_config* config() { return &conf_; }
  mbedtls_ctr_drbg_context* ctr_drbg() { return &ctr_drbg_; }

 private:
  MbedTlsContext();

  mbedtls_ssl_config conf_;
  mbedtls_x509_crt ca_cert_;
  mbedtls_x509_crt client_cert_;
  mbedtls_pk_context client_key_;
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context ctr_drbg_;
};

class MbedTlsTransport final : public Transport,
                                public TcpClientEventListener {
 public:
  explicit MbedTlsTransport(TaskRunner* runner,
                             TransportEventListener* listener);
  ~MbedTlsTransport() override;

  bool Connect(const HttpUrl& url, TlsContextPtr ctx) override;
  // Non-blocking, bounded 8 MiB / 1024-record queue, like the OpenSSL backend.
  // Methods and destruction must run on the supplied runner.
  bool Send(const void* data, size_t len) override;
  void Close() override;

 private:
  // TcpClientEventListener
  void OnConnected(bool success) override;
  void OnDataReceived(const void* data, size_t len) override;
  void OnDisconnected() override;
  void OnError(const std::string& error) override;
  bool OnReadable() override;

  void ContinueHandshake();
  void DrainRead();
  bool FlushWrites();
  bool WaitForIO(int error);
  void Fail(const std::string& reason);

  static int MbedSend(void* ctx, const unsigned char* buf, size_t len);
  static int MbedRecv(void* ctx, unsigned char* buf, size_t len);

 private:
  enum class TLSState { kIdle, kHandshaking, kConnected, kError };

  TaskRunner* runner_;
  std::unique_ptr<TcpClient> tcp_;

  TlsContextPtr ctx_;
  mbedtls_ssl_context ssl_;
  bool ssl_inited_{false};
  std::atomic<TLSState> state_;

  std::string host_;
  std::deque<std::string> pending_writes_;
  size_t write_offset_ = 0;
  size_t queued_bytes_ = 0;
  bool read_wants_write_ = false;
  bool write_wants_write_ = false;
  std::array<uint8_t, 16384> read_buffer_{};
  WeakPtrFactory<MbedTlsTransport> weak_factory_{this};
};

}  // namespace xtils

#endif  // USE_MBEDTLS
