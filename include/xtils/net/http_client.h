#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xtils/net/http_common.h"
#include "xtils/net/transport/transport.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

class HttpClient : public TransportEventListener {
 public:
  struct MultipartFile {
    std::string field_name;    // Form field name (e.g., "file")
    std::string filename;      // Filename to send (e.g., "photo.jpg")
    std::string content_type;  // MIME type (e.g., "image/jpeg")
    std::string file_path;     // Path to file on disk
  };

  struct MultipartField {
    std::string name;
    std::string value;
  };

  struct Request {
    HttpMethod method = HttpMethod::kGet;
    HttpUrl url;
    HttpHeaders headers;
    std::string body;
    uint32_t timeout_ms = 30000;  // 30 seconds default

    void AddHeader(const std::string& name, const std::string& value);
    void SetContentType(const std::string& content_type);
    void SetUserAgent(const std::string& user_agent);
    void SetAuthorization(const std::string& auth);
    void SetBody(const std::string& data, const std::string& content_type = "");
    void SetJsonBody(const std::string& json);
    void SetFormBody(const std::map<std::string, std::string>& form_data);
    void SetMultipartBody(const std::vector<MultipartField>& fields,
                          const std::vector<MultipartFile>& files);
    bool is_multipart() const {
      return !multipart_fields.empty() || !multipart_files.empty();
    }

    // Multipart is handled differently - don't load into body.
    std::vector<MultipartField> multipart_fields;
    std::vector<MultipartFile> multipart_files;
    std::string boundary;
  };

  struct Response {
    int status_code = 0;
    std::string status_message;
    HttpHeaders headers;
    std::string body;
    size_t content_length = 0;
    bool chunked_encoding = false;

    std::string GetHeader(const std::string& name) const;
    bool HasHeader(const std::string& name) const;
    bool IsSuccessful() const {
      return status_code >= 200 && status_code < 300;
    }
    bool IsRedirect() const { return status_code >= 300 && status_code < 400; }
    bool IsError() const { return status_code >= 400; }
  };

  class Listener {
   public:
    virtual ~Listener() = default;

    virtual void OnHttpResponse(HttpClient* client,
                                const Response& response) = 0;
    virtual void OnHttpError(HttpClient* client, const std::string& error) = 0;
    virtual void OnProgress(HttpClient* client, size_t bytes_transferred,
                            int64_t total_bytes) {}
    virtual void OnRedirect(HttpClient* client, const std::string& new_url) {}

    // Called when receiving body data (for streaming large files).
    // Return false to stop accumulating this data in Response::body.
    virtual bool OnBodyData(HttpClient* client, const void* data, size_t len) {
      return true;
    }
  };

  enum class State {
    kIdle = 0,
    kConnecting,
    kSendingRequest,
    kReceivingResponse,
    kCompleted,
    kError
  };

  explicit HttpClient(TaskRunner* task_runner);
  ~HttpClient() override;

  // Synchronous HTTP request. A single HttpClient is single-flight: this
  // returns an error response if another request is already in progress.
  Response Send(const Request& request);

  // Asynchronous HTTP request. Returns false if another request is in progress.
  bool SendAsync(const Request& request, Listener* listener);

  // Convenience methods for common HTTP operations.
  Response Get(const std::string& url);
  Response Post(const std::string& url, const std::string& body,
                const std::string& content_type = "");
  Response PostJson(const std::string& url, const std::string& json);
  Response PostForm(const std::string& url,
                    const std::map<std::string, std::string>& form_data);
  Response PostMultipart(const std::string& url,
                         const std::vector<MultipartField>& fields,
                         const std::vector<MultipartFile>& files);

  // Async versions.
  bool GetAsync(const std::string& url, Listener* listener);
  bool PostAsync(const std::string& url, const std::string& body,
                 const std::string& content_type, Listener* listener);
  bool PostJsonAsync(const std::string& url, const std::string& json,
                     Listener* listener);
  bool PostMultipartAsync(const std::string& url,
                          const std::vector<MultipartField>& fields,
                          const std::vector<MultipartFile>& files,
                          Listener* listener);

  // Cancel current request. Synchronous waiters are notified with
  // "Request cancelled".
  void Cancel();

  // Configuration.
  void SetDefaultHeaders(const HttpHeaders& headers);
  void AddDefaultHeader(const std::string& name, const std::string& value);
  void SetUserAgent(const std::string& user_agent);
  void SetTimeout(uint32_t timeout_ms);
  void SetFollowRedirects(bool follow, int max_redirects = 5);
  void SetKeepAlive(bool keep_alive);
  void SetMaxReceiveBufferSize(size_t max_size);

  // Cookie management.
  void SetCookie(const std::string& name, const std::string& value,
                 const std::string& domain = "");
  void ClearCookies();
  std::string GetCookies(const std::string& domain = "") const;

  void SetVerifySSL(bool verify);
  void SetSSLCertificate(const std::string& cert_path);

  State GetState() const { return state_.load(); }
  bool IsBusy() const {
    State current_state = state_.load();
    return current_state != State::kIdle &&
           current_state != State::kCompleted && current_state != State::kError;
  }

  const Response& GetLastResponse() const { return last_response_; }

 private:
  void OnConnected(bool success) override;
  void OnDataReceived(const void* data, size_t len) override;
  void OnDisconnected() override;
  void OnError(const std::string& error) override;

  // HTTP protocol handling.
  std::string BuildHttpRequest(const Request& request);
  bool SendHttpRequest(const Request& request);
  bool SendMultipartBody(const Request& request);
  void ProcessReceivedData(const void* data, size_t len);
  void ProcessChunkedBody();
  void ProcessFixedLengthBody();
  void HandleRedirect();
  void CompleteRequest();
  void HandleError(const std::string& error);
  void CancelOnRunner();
  bool FinishDeferredCancelOnRunner();

  // URL parsing and connection.
  bool ConnectToHost(const HttpUrl& url);

  // Header utilities.
  HttpHeaders MergeHeaders(const HttpHeaders& request_headers);
  void ParseResponseHeaders(const std::string& header_text);

  // Cookie utilities.
  void ProcessSetCookieHeader(const std::string& cookie_header,
                              const std::string& domain);

  // Multipart utilities.
  std::string GenerateBoundary();
  size_t CalculateMultipartSize(const std::vector<MultipartField>& fields,
                                const std::vector<MultipartFile>& files,
                                const std::string& boundary);

  struct LifetimeToken {
    std::mutex mutex;
    std::condition_variable cv;
    bool alive = true;
    size_t active_callbacks = 0;
    std::thread::id callback_thread;
  };

  struct RequestState {
    Request request;
    Response response;
    std::string receive_buffer;
    bool headers_received = false;
    bool has_content_length = false;
    size_t content_length = 0;
    size_t bytes_received = 0;
    size_t body_bytes_received = 0;
    bool chunked_encoding = false;
    int chunk_size = -1;
    int redirect_count = 0;
    uint32_t timeout_ms = 0;
    std::atomic<bool> timeout_scheduled{false};
    std::atomic<bool> completed{false};
    bool in_user_callback = false;
    bool cancel_requested = false;
    bool deferred_cancel_scheduled = false;

    size_t bytes_sent = 0;
    size_t total_bytes = 0;

    void Reset() {
      request = Request();
      response = Response();
      receive_buffer.clear();
      headers_received = false;
      has_content_length = false;
      content_length = 0;
      bytes_received = 0;
      body_bytes_received = 0;
      chunked_encoding = false;
      chunk_size = -1;
      redirect_count = 0;
      timeout_ms = 0;
      timeout_scheduled.store(false);
      completed.store(false);
      in_user_callback = false;
      cancel_requested = false;
      deferred_cancel_scheduled = false;
      bytes_sent = 0;
      total_bytes = 0;
    }
  };

  TaskRunner* task_runner_;
  Listener* listener_;
  std::unique_ptr<Transport> transport_;
  std::atomic<State> state_;

  RequestState current_;
  Response last_response_;

  HttpHeaders default_headers_;
  uint32_t default_timeout_ms_;
  bool follow_redirects_;
  int max_redirects_;
  bool keep_alive_;
  size_t max_receive_buffer_size_;

  bool verify_ssl_;
  std::string ssl_cert_path_;

  std::map<std::string, std::map<std::string, std::string>> cookies_;

  std::atomic<uint64_t> request_generation_{0};
  std::atomic<uint64_t> active_request_generation_{0};

  std::shared_ptr<LifetimeToken> lifetime_;

  std::mutex request_mutex_;
  std::mutex sync_mutex_;
  std::condition_variable sync_cv_;
};

}  // namespace xtils
