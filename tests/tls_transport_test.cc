#include "xtils/net/transport/tls_transport.h"

#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <future>
#include <memory>

#include "net_test_server.h"
#include "xtils/net/http_client.h"
#include "xtils/net/http_common.h"
#include "xtils/tasks/thread_task_runner.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;
using namespace std::chrono_literals;

namespace {
struct Certificate {
  explicit Certificate(const char* dns = "localhost") {
    EVP_PKEY_CTX* key_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(key_ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(key_ctx, 2048);
    EVP_PKEY_keygen(key_ctx, &key);
    EVP_PKEY_CTX_free(key_ctx);
    cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), -60);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);
    auto* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(dns), -1,
                               -1, 0);
    X509_set_issuer_name(cert, name);
    std::string san = std::string("DNS:") + dns;
    auto* ext =
        X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, san.data());
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);
    X509_sign(cert, key, EVP_sha256());
    server_ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_use_certificate(server_ctx, cert);
    SSL_CTX_use_PrivateKey(server_ctx, key);
  }
  ~Certificate() {
    SSL_CTX_free(server_ctx);
    X509_free(cert);
    EVP_PKEY_free(key);
  }
  EVP_PKEY* key = nullptr;
  X509* cert = nullptr;
  SSL_CTX* server_ctx = nullptr;
};

struct Listener : TransportEventListener {
  void OnConnected(bool success) override { Finish(success); }
  void OnDataReceived(const void*, size_t) override {}
  void OnDisconnected() override { Finish(false); }
  void OnError(const std::string&) override { Finish(false); }
  void Finish(bool success) {
    if (!finished) {
      finished = true;
      result.set_value(success);
    }
  }
  bool finished = false;
  std::promise<bool> result;
};
}  // namespace

TEST_CASE("HTTPS: asynchronous timeout includes a silent TLS handshake") {
  std::promise<void> release;
  auto released = release.get_future();
  NetTestServer server([&](int) { released.wait(); });
  struct HttpListener : HttpClient::Listener {
    void OnHttpResponse(HttpClient*, const HttpClient::Response&) override {
      result.set_value("unexpected response");
    }
    void OnHttpError(HttpClient*, const std::string& error) override {
      result.set_value(error);
    }
    std::promise<std::string> result;
  } listener;
  auto runner = ThreadTaskRunner::CreateAndStart("https_timeout");
  HttpClient client(&runner);
  client.SetVerifySSL(false);
  client.SetTimeout(50);
  REQUIRE(client.GetAsync("https://127.0.0.1:" + std::to_string(server.port),
                          &listener));
  auto result = listener.result.get_future();
  bool ready = result.wait_for(1s) == std::future_status::ready;
  CHECK(ready);
  release.set_value();
  if (ready) CHECK(result.get() == "Request timeout");
  client.Cancel();
}

TEST_CASE("HTTPS: redirects cannot downgrade to plaintext") {
  Certificate cert;
  NetTestServer server([&](int fd) {
    SSL* ssl = SSL_new(cert.server_ctx);
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) == 1) {
      char request[4096];
      if (SSL_read(ssl, request, sizeof(request)) > 0) {
        const std::string response =
            "HTTP/1.1 302 Found\r\n"
            "Location: http://127.0.0.1:1/\r\nContent-Length: 0\r\n\r\n";
        SSL_write(ssl, response.data(), response.size());
      }
    }
    SSL_free(ssl);
  });
  auto runner = ThreadTaskRunner::CreateAndStart("https_downgrade");
  HttpClient client(&runner);
  client.SetVerifySSL(false);
  auto response =
      client.Get("https://127.0.0.1:" + std::to_string(server.port));
  CHECK(response.status_code == 0);
  CHECK(response.status_message == "Refusing insecure TLS redirect");
}

TEST_CASE("TLS: silent peer does not block runner tasks") {
  std::promise<void> accepted;
  std::promise<void> release;
  auto release_future = release.get_future();
  NetTestServer server([&](int) {
    accepted.set_value();
    release_future.wait();
  });
  auto runner = ThreadTaskRunner::CreateAndStart("tls_stall_test");
  Listener listener;
  TlsTransport transport(&runner, &listener);
  runner.PostTask([&] {
    transport.Connect(
        HttpUrl("https://127.0.0.1:" + std::to_string(server.port)),
        OpenSslContext::Create(TlsCertConfig::Insecure()));
  });
  accepted.get_future().wait();
  std::promise<void> marker;
  runner.PostDelayedTask([&] { marker.set_value(); }, 10);
  CHECK(marker.get_future().wait_for(1s) == std::future_status::ready);
  release.set_value();
  std::promise<void> closed;
  runner.PostTask([&] {
    transport.Close();
    closed.set_value();
  });
  closed.get_future().wait();
}

TEST_CASE("TLS: Send preserves large payloads across partial writes") {
  Certificate cert;
  const std::string payload(4 * 1024 * 1024, 'x');
  std::promise<void> send_returned;
  auto may_read = send_returned.get_future();
  std::promise<size_t> received;
  NetTestServer server([&](int fd) {
    SSL* ssl = SSL_new(cert.server_ctx);
    SSL_set_fd(ssl, fd);
    size_t total = 0;
    if (SSL_accept(ssl) == 1) {
      may_read.wait();
      char buf[16384];
      while (total < payload.size()) {
        int n = SSL_read(ssl, buf, sizeof(buf));
        if (n <= 0) break;
        total += static_cast<size_t>(n);
      }
    }
    received.set_value(total);
    SSL_free(ssl);
  });
  struct Sender : Listener {
    void OnConnected(bool success) override {
      if (success) {
        overflow_rejected =
            !transport->Send(payload->data(), 8 * 1024 * 1024 + 1);
        accepted = transport->Send(payload->data(), payload->size());
      }
      returned->set_value();
    }
    Transport* transport = nullptr;
    const std::string* payload = nullptr;
    std::promise<void>* returned = nullptr;
    bool accepted = false;
    bool overflow_rejected = false;
  } listener;
  auto runner = ThreadTaskRunner::CreateAndStart("tls_send_test");
  TlsTransport transport(&runner, &listener);
  listener.transport = &transport;
  listener.payload = &payload;
  listener.returned = &send_returned;
  runner.PostTask([&] {
    transport.Connect(
        HttpUrl("https://127.0.0.1:" + std::to_string(server.port)),
        OpenSslContext::Create(TlsCertConfig::Insecure()));
  });
  CHECK(received.get_future().get() == payload.size());
  std::promise<void> closed;
  runner.PostTask([&] {
    transport.Close();
    closed.set_value();
  });
  closed.get_future().wait();
  CHECK(listener.accepted);
  CHECK(listener.overflow_rejected);
}

TEST_CASE("TLS: trusted certificate must match DNS name or IP address") {
  std::string host = "localhost";
  const char* certificate_name = "localhost";
  bool trusted = true;
  bool expected = false;
  SUBCASE("matching DNS name") {
    host = "localhost";
    expected = true;
  }
  SUBCASE("IP absent from subjectAltName") { host = "127.0.0.1"; }
  SUBCASE("different DNS name") { certificate_name = "wrong.invalid"; }
  SUBCASE("untrusted certificate") { trusted = false; }
  Certificate cert(certificate_name);
  auto ctx = OpenSslContext::Create(TlsCertConfig::Default());
  REQUIRE(ctx);
  if (trusted) {
    REQUIRE(X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx->ctx()),
                                cert.cert) == 1);
  }
  NetTestServer server([&](int fd) {
    SSL* ssl = SSL_new(cert.server_ctx);
    SSL_set_fd(ssl, fd);
    SSL_accept(ssl);
    SSL_free(ssl);
  });
  auto runner = ThreadTaskRunner::CreateAndStart("tls_test");
  Listener listener;
  TlsTransport transport(&runner, &listener);
  auto result = listener.result.get_future();
  runner.PostTask([&] {
    transport.Connect(
        HttpUrl("https://" + host + ":" + std::to_string(server.port)), ctx);
  });
  REQUIRE(result.wait_for(6s) == std::future_status::ready);
  CHECK(result.get() == expected);
  std::promise<void> closed;
  runner.PostTask([&] {
    transport.Close();
    closed.set_value();
  });
  closed.get_future().wait();
}
