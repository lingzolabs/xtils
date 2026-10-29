#include "xtils/net/http_client.h"

#include <algorithm>
#include <charconv>
#include <exception>
#include <fstream>
#include <sstream>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"
#include "xtils/net/transport/plain_tcp_transport.h"
#include "xtils/net/transport/tls_factory.h"
#include "xtils/utils/scoped.h"
#include "xtils/utils/string_utils.h"

namespace xtils {

// HttpClient::Request implementation
void HttpClient::Request::AddHeader(const std::string& name,
                                    const std::string& value) {
  headers.emplace_back(name, value);
}

void HttpClient::Request::SetContentType(const std::string& content_type) {
  AddHeader("Content-Type", content_type);
}

void HttpClient::Request::SetUserAgent(const std::string& user_agent) {
  AddHeader("User-Agent", user_agent);
}

void HttpClient::Request::SetAuthorization(const std::string& auth) {
  AddHeader("Authorization", auth);
}

void HttpClient::Request::SetBody(const std::string& data,
                                  const std::string& content_type) {
  body = data;
  if (!content_type.empty()) {
    SetContentType(content_type);
  }
}

void HttpClient::Request::SetJsonBody(const std::string& json) {
  SetBody(json, "application/json");
}

void HttpClient::Request::SetFormBody(
    const std::map<std::string, std::string>& form_data) {
  SetBody(HttpUtils::FormDataEncode(form_data),
          "application/x-www-form-urlencoded");
}

void HttpClient::Request::SetMultipartBody(
    const std::vector<HttpClient::MultipartField>& fields,
    const std::vector<HttpClient::MultipartFile>& files) {
  multipart_fields = fields;
  multipart_files = files;
}

// HttpClient::Response implementation
std::string HttpClient::Response::GetHeader(const std::string& name) const {
  return HttpUtils::GetHeaderValue(headers, name);
}

bool HttpClient::Response::HasHeader(const std::string& name) const {
  return HttpUtils::HasHeader(headers, name);
}

// Keeps transport callbacks safe while destruction hands the transport back to
// its owner runner. No transport callback owns a raw, unguarded HttpClient.
class HttpClient::TransportCallbacks final : public TransportEventListener {
 public:
  TransportCallbacks(HttpClient* owner, std::shared_ptr<LifetimeToken> lifetime)
      : owner_(owner), lifetime_(std::move(lifetime)) {}
  void OnConnected(bool ok) override {
    Invoke([ok](HttpClient* c) { c->OnConnected(ok); });
  }
  void OnDataReceived(const void* data, size_t len) override {
    Invoke([data, len](HttpClient* c) { c->OnDataReceived(data, len); });
  }
  void OnDisconnected() override {
    Invoke([](HttpClient* c) { c->OnDisconnected(); });
  }
  void OnError(const std::string& error) override {
    Invoke([&error](HttpClient* c) { c->OnError(error); });
  }
  void OnWriteCapacityAvailable() override {
    Invoke([](HttpClient* c) { c->OnWriteCapacityAvailable(); });
  }

 private:
  template <typename Callback>
  void Invoke(Callback callback) {
    auto lifetime = lifetime_;
    {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      if (!lifetime->alive) return;
      ++lifetime->active_callbacks;
      lifetime->callback_thread = std::this_thread::get_id();
    }
    Scoped guard([lifetime] {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      if (--lifetime->active_callbacks == 0) {
        lifetime->callback_thread = std::thread::id();
        lifetime->cv.notify_all();
      }
    });
    callback(owner_);
  }
  HttpClient* owner_;
  std::shared_ptr<LifetimeToken> lifetime_;
};

// HttpClient implementation
HttpClient::HttpClient(TaskRunner* task_runner)
    : task_runner_(task_runner),
      listener_(nullptr),
      state_(State::kIdle),
      default_timeout_ms_(30000),
      follow_redirects_(true),
      max_redirects_(5),
      keep_alive_(true),
      max_receive_buffer_size_(100 * 1024 * 1024),
      verify_ssl_(true),
      ssl_cert_path_(),
      lifetime_(std::make_shared<LifetimeToken>()) {
  SetUserAgent("xtils/1.0");
  InitializeLifecycle();
  transport_callbacks_ = std::make_unique<TransportCallbacks>(this, lifetime_);
}

HttpClient::~HttpClient() {
  {
    std::unique_lock<std::mutex> lock(lifetime_->mutex);
    lifetime_->alive = false;
    if (lifetime_->callback_thread != std::this_thread::get_id())
      lifetime_->cv.wait(lock,
                         [this] { return lifetime_->active_callbacks == 0; });
  }
  // Never synchronously wait on a different runner for network cleanup.
  struct Cleanup {
    std::unique_ptr<TransportCallbacks> callbacks;
    std::unique_ptr<Transport> transport;
  };
  auto cleanup = std::make_shared<Cleanup>();
  cleanup->callbacks = std::move(transport_callbacks_);
  cleanup->transport = std::move(transport_);
  auto* runner = task_runner_;
  auto timeout = timeout_handle_;
  runner->PostTask([cleanup, runner, timeout] {
    if (timeout) runner->CancelDelayedTask(timeout);
    if (cleanup->transport) cleanup->transport->Close();
  });
}

HttpClient::Response HttpClient::Send(const Request& request) {
  Response error;
  if (task_runner_->RunsTasksOnCurrentThread()) {
    error.status_message = "Synchronous HTTP request on owner runner";
    return error;
  }
  if (!request.url.IsValid()) {
    error.status_message = "Invalid URL";
    return error;
  }
  auto completion = std::make_shared<std::promise<Response>>();
  auto result = completion->get_future();
  if (!Submit(request, nullptr, completion)) {
    error.status_message = "Client is busy";
    return error;
  }
  // Keep an independent deadline: a stopped or stalled runner cannot run its
  // timer or consume a cancellation command. The request remains single-flight
  // until the runner eventually processes that cancellation.
  auto timeout = std::chrono::milliseconds(
      request.timeout_ms > 0 ? request.timeout_ms : default_timeout_ms_);
  if (result.wait_for(timeout) != std::future_status::ready) {
    Cancel();
    error.status_message = "Request timeout";
    return error;
  }
  return result.get();
}

bool HttpClient::SendAsync(const Request& request, Listener* listener) {
  return Submit(request, listener, nullptr);
}

bool HttpClient::Submit(const Request& request, Listener* listener,
                        std::shared_ptr<std::promise<Response>> completion) {
  if (!request.url.IsValid()) return false;
  std::lock_guard<std::mutex> lock(request_mutex_);
  State expected = State::kIdle;
  if (!state_.compare_exchange_strong(expected, State::kConnecting))
    return false;
  auto generation = request_generation_.fetch_add(1) + 1;
  active_request_generation_.store(generation);
  PostOnRunner([this, request, listener, completion, generation]() mutable {
    StartOnRunner(std::move(request), listener, completion, generation);
  });
  return true;
}

void HttpClient::StartOnRunner(
    Request request, Listener* listener,
    std::shared_ptr<std::promise<Response>> completion, uint64_t generation) {
  if (generation != active_request_generation_.load()) return;
  current_.Reset();
  current_.request = std::move(request);
  listener_ = listener;
  completion_ = std::move(completion);
  terminal_error_.clear();
  current_.timeout_ms = current_.request.timeout_ms > 0
                            ? current_.request.timeout_ms
                            : default_timeout_ms_;
  Dispatch(Event::kStart);
  if (cancelled_generation_.load() == generation) {
    Dispatch(Event::kCancel, "Request cancelled");
    return;
  }
  if (current_.request.is_multipart() && current_.request.boundary.empty())
    current_.request.boundary = GenerateBoundary();
  ScheduleRequestTimeout();
  ConnectToHost(current_.request.url);
}

void HttpClient::InitializeLifecycle() {
  idle_ = lifecycle_.AddState("Idle");
  connecting_ = lifecycle_.AddState("Connecting");
  active_ = lifecycle_.AddState("Active");
  finishing_ = lifecycle_.AddState("Finishing");
  auto add = [this](fsm::StateId from, fsm::StateId to, Event event) {
    lifecycle_.AddTransition(from, to, static_cast<fsm::EventType>(event));
  };
  add(idle_, connecting_, Event::kStart);
  add(connecting_, active_, Event::kConnected);
  add(active_, connecting_, Event::kRedirect);
  add(active_, finishing_, Event::kComplete);
  for (auto from : {connecting_, active_})
    for (auto event : {Event::kFail, Event::kCancel, Event::kTimeout})
      add(from, finishing_, event);
  add(finishing_, idle_, Event::kFinished);
  lifecycle_.Start(idle_);
}

void HttpClient::Dispatch(Event event, std::string error) {
  events_.push_back({event, std::move(error)});
  if (dispatching_) return;
  dispatching_ = true;
  Scoped guard([this] { dispatching_ = false; });
  while (!events_.empty()) {
    auto next = std::move(events_.front());
    events_.pop_front();
    bool was_finishing = lifecycle_.IsInState(finishing_);
    lifecycle_.ProcessEvent(static_cast<fsm::EventType>(next.event));
    if (!was_finishing && lifecycle_.IsInState(finishing_)) {
      current_.completed.store(true);
      terminal_error_ = std::move(next.error);
      // Cleanup and user callbacks run only after parser/transport/FSM stacks
      // unwind. Nested events are queued, never recursively ProcessEvent'ed.
      PostOnRunner([this] { FinishOnRunner(); });
    }
  }
}

void HttpClient::FinishOnRunner() {
  if (!lifecycle_.IsInState(finishing_)) return;
  ++output_generation_;
  output_scheduled_ = false;
  output_.Reset();
  if (timeout_handle_) task_runner_->CancelDelayedTask(timeout_handle_);
  timeout_handle_ = 0;
  if (transport_) transport_->Close();
  if (!terminal_error_.empty()) {
    current_.response.status_code = 0;
    current_.response.status_message = terminal_error_;
  }
  last_response_ = current_.response;
  auto response = std::move(current_.response);
  current_.request = Request();
  current_.receive_buffer.clear();
  auto error = terminal_error_;
  auto* listener = listener_;
  auto completion = std::move(completion_);
  listener_ = nullptr;
  Dispatch(Event::kFinished);
  state_.store(State::kIdle);
  // Do not touch members after publishing: the callback/waiter may destroy us.
  if (completion) {
    completion->set_value(std::move(response));
  } else if (listener) {
    if (error.empty())
      listener->OnHttpResponse(this, response);
    else
      listener->OnHttpError(this, error);
  }
}

HttpClient::Response HttpClient::Get(const std::string& url) {
  HttpClient::Request request;
  request.method = HttpMethod::kGet;
  request.url = HttpUrl(url);
  return Send(request);
}

HttpClient::Response HttpClient::Post(const std::string& url,
                                      const std::string& body,
                                      const std::string& content_type) {
  HttpClient::Request request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.SetBody(body, content_type);
  return Send(request);
}

HttpClient::Response HttpClient::PostJson(const std::string& url,
                                          const std::string& json) {
  return Post(url, json, "application/json");
}

HttpClient::Response HttpClient::PostForm(
    const std::string& url,
    const std::map<std::string, std::string>& form_data) {
  HttpClient::Request request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.SetFormBody(form_data);
  return Send(request);
}

HttpClient::Response HttpClient::PostMultipart(
    const std::string& url,
    const std::vector<HttpClient::MultipartField>& fields,
    const std::vector<HttpClient::MultipartFile>& files) {
  HttpClient::Request request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.multipart_fields = fields;
  request.multipart_files = files;
  request.boundary = GenerateBoundary();
  return Send(request);
}

bool HttpClient::GetAsync(const std::string& url,
                          HttpClient::Listener* listener) {
  HttpClient::Request request;
  request.method = HttpMethod::kGet;
  request.url = HttpUrl(url);
  return SendAsync(request, listener);
}

bool HttpClient::PostAsync(const std::string& url, const std::string& body,
                           const std::string& content_type,
                           HttpClient::Listener* listener) {
  HttpClient::Request request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.SetBody(body, content_type);
  return SendAsync(request, listener);
}

bool HttpClient::PostJsonAsync(const std::string& url, const std::string& json,
                               HttpClient::Listener* listener) {
  return PostAsync(url, json, "application/json", listener);
}

bool HttpClient::PostMultipartAsync(
    const std::string& url,
    const std::vector<HttpClient::MultipartField>& fields,
    const std::vector<HttpClient::MultipartFile>& files,
    HttpClient::Listener* listener) {
  HttpClient::Request request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.multipart_fields = fields;
  request.multipart_files = files;
  request.boundary = GenerateBoundary();
  return SendAsync(request, listener);
}

void HttpClient::Cancel() {
  auto generation = active_request_generation_.load();
  cancelled_generation_.store(generation);
  auto cancel = [this, generation] {
    if (generation == active_request_generation_.load() && IsBusy())
      Dispatch(Event::kCancel, "Request cancelled");
  };
  if (task_runner_->RunsTasksOnCurrentThread())
    cancel();
  else
    PostOnRunner(std::move(cancel));
}

void HttpClient::SetDefaultHeaders(const HttpHeaders& headers) {
  default_headers_ = headers;
}

void HttpClient::AddDefaultHeader(const std::string& name,
                                  const std::string& value) {
  HttpUtils::AddHeader(default_headers_, name, value);
}

void HttpClient::SetUserAgent(const std::string& user_agent) {
  AddDefaultHeader("User-Agent", user_agent);
}

void HttpClient::SetTimeout(uint32_t timeout_ms) {
  default_timeout_ms_ = timeout_ms;
}

void HttpClient::SetFollowRedirects(bool follow, int max_redirects) {
  follow_redirects_ = follow;
  max_redirects_ = max_redirects;
}

void HttpClient::SetKeepAlive(bool keep_alive) { keep_alive_ = keep_alive; }

void HttpClient::SetMaxReceiveBufferSize(size_t max_size) {
  max_receive_buffer_size_ = max_size;
}

void HttpClient::SetCookie(const std::string& name, const std::string& value,
                           const std::string& domain) {
  std::string cookie_domain = domain.empty() ? "" : domain;
  cookies_[cookie_domain][name] = value;
}

void HttpClient::ClearCookies() { cookies_.clear(); }

std::string HttpClient::GetCookies(const std::string& domain) const {
  std::ostringstream oss;
  bool first = true;

  for (const auto& domain_cookies : cookies_) {
    if (domain_cookies.first.empty() || domain_cookies.first == domain) {
      for (const auto& cookie : domain_cookies.second) {
        if (!first) oss << "; ";
        first = false;
        oss << cookie.first << "=" << cookie.second;
      }
    }
  }

  return oss.str();
}

void HttpClient::SetVerifySSL(bool verify) { verify_ssl_ = verify; }

void HttpClient::SetSSLCertificate(const std::string& cert_path) {
  ssl_cert_path_ = cert_path;
}
void HttpClient::OnConnected(bool success) {
  if (current_.completed.load() || state_.load() != State::kConnecting) {
    return;
  }
  if (!success) {
    HandleError("Failed to connect to host");
    return;
  }

  Dispatch(Event::kConnected);
  state_.store(State::kSendingRequest);

  // Send the HTTP request
  if (!PrepareOutput(current_.request)) {
    HandleError(output_.error());
    return;
  }
  ScheduleOutput();
}

void HttpClient::OnDataReceived(const void* data, size_t len) {
  if (current_.completed.load() || state_.load() == State::kIdle) {
    return;
  }
  state_.store(State::kReceivingResponse);
  ProcessReceivedData(data, len);
}

void HttpClient::OnDisconnected() {
  if (current_.completed.load() || state_.load() == State::kIdle) {
    return;
  }
  if (current_.headers_received && !current_.chunked_encoding &&
      !current_.has_content_length) {
    CompleteRequest();
  } else {
    // Fixed-length and chunked bodies complete in the parser, never on EOF.
    HandleError("Unexpected EOF before the HTTP response was complete");
  }
}

void HttpClient::OnError(const std::string& error) {
  if (current_.completed.load() || state_.load() == State::kIdle) {
    return;
  }
  const auto generation = active_request_generation_.load();
  PostOnRunner([this, generation, error] {
    if (generation == active_request_generation_.load()) HandleError(error);
  });
}

void HttpClient::OnWriteCapacityAvailable() { ScheduleOutput(); }

// HTTP protocol handling
std::string HttpClient::BuildHttpRequest(const HttpClient::Request& request) {
  std::ostringstream oss;

  // Request line
  oss << HttpUtils::HttpMethodToString(request.method) << " ";
  oss << (request.url.path.empty() ? "/" : request.url.path);
  if (!request.url.query.empty()) {
    oss << "?" << request.url.query;
  }
  oss << " HTTP/1.1\r\n";

  // Host header
  oss << "Host: " << request.url.host;
  if ((request.url.scheme == "http" && request.url.port != 80 &&
       request.url.port != 0) ||
      (request.url.scheme == "https" && request.url.port != 443 &&
       request.url.port != 0)) {
    oss << ":" << request.url.port;
  }
  oss << "\r\n";

  // Merge and add headers
  HttpHeaders merged_headers = MergeHeaders(request.headers);
  for (const auto& header : merged_headers) {
    if (current_.strip_sensitive_headers &&
        (CaseInsensitiveEqual(header.name, "Authorization") ||
         CaseInsensitiveEqual(header.name, "Proxy-Authorization") ||
         CaseInsensitiveEqual(header.name, "Cookie") ||
         CaseInsensitiveEqual(header.name, "Host")))
      continue;
    oss << header.name << ": " << header.value << "\r\n";
  }

  // Add cookies
  std::string cookies = GetCookies(request.url.host);
  if (!cookies.empty()) {
    oss << "Cookie: " << cookies << "\r\n";
  }

  // Connection header
  oss << "Connection: " << (keep_alive_ ? "keep-alive" : "close") << "\r\n";

  // Handle multipart
  if (request.is_multipart()) {
    oss << "Content-Type: multipart/form-data; boundary=" << request.boundary
        << "\r\n";
    oss << "Content-Length: " << current_.total_bytes << "\r\n";
  } else if (!request.body.empty()) {
    if (!HttpUtils::HasHeader(merged_headers, "Content-Length")) {
      oss << "Content-Length: " << request.body.size() << "\r\n";
    }
  }

  // End of headers
  oss << "\r\n";

  return oss.str();
}

void HttpClient::PostOnRunner(std::function<void()> task) {
  auto lifetime = lifetime_;
  task_runner_->PostTask([lifetime, task = std::move(task)] {
    {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      if (!lifetime->alive) return;
      ++lifetime->active_callbacks;
      lifetime->callback_thread = std::this_thread::get_id();
    }
    Scoped guard([lifetime] {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      if (--lifetime->active_callbacks == 0) {
        lifetime->callback_thread = std::thread::id();
        lifetime->cv.notify_all();
      }
    });
    task();
  });
}

bool HttpClient::PrepareOutput(const Request& request) {
  ++output_generation_;
  output_scheduled_ = false;
  output_.Reset();
  if (!request.is_multipart()) {
    output_.Append(request.body);
  } else {
    for (const auto& field : request.multipart_fields) {
      output_.Append("--" + request.boundary + "\r\n" +
                     "Content-Disposition: form-data; name=\"" + field.name +
                     "\"\r\n\r\n");
      output_.Append(field.value);
      output_.Append("\r\n");
    }
    for (const auto& file : request.multipart_files) {
      output_.Append("--" + request.boundary + "\r\n" +
                     "Content-Disposition: form-data; name=\"" +
                     file.field_name + "\"; filename=\"" + file.filename +
                     "\"\r\n" + "Content-Type: " + file.content_type +
                     "\r\n\r\n");
      if (!output_.AppendFile(file.file_path)) return false;
      output_.Append("\r\n");
    }
    output_.Append("--" + request.boundary + "--\r\n");
  }
  current_.total_bytes = output_.total_bytes();
  output_.Prepend(BuildHttpRequest(request));
  current_.total_bytes = output_.total_bytes();
  current_.bytes_sent = 0;
  return output_.error().empty();
}

void HttpClient::ScheduleOutput() {
  if (output_scheduled_ || current_.completed.load() || output_.empty()) return;
  output_scheduled_ = true;
  const auto generation = output_generation_;
  PostOnRunner([this, generation] {
    if (generation != output_generation_) return;
    output_scheduled_ = false;
    PumpOutput();
  });
}

void HttpClient::PumpOutput() {
  if (current_.completed.load() || !transport_ || output_.empty()) return;
  auto result = output_.Pump([this](const void* data, size_t len) {
    return transport_->TrySend(data, len);
  });
  if (result == StreamWriter::PumpResult::kError) {
    HandleError(output_.error());
    return;
  }
  current_.bytes_sent = output_.accepted_bytes();
  if (result == StreamWriter::PumpResult::kYield) ScheduleOutput();
  if (listener_) {
    auto generation = active_request_generation_.load();
    auto sent = current_.bytes_sent;
    auto total = current_.total_bytes;
    PostOnRunner([this, generation, sent, total] {
      if (generation != active_request_generation_.load() ||
          current_.completed.load() || !listener_)
        return;
      listener_->OnProgress(this, sent, total);
    });
  }
}

bool HttpClient::IsAlive(const std::shared_ptr<LifetimeToken>& lifetime) {
  std::lock_guard<std::mutex> lock(lifetime->mutex);
  return lifetime->alive;
}

void HttpClient::ProcessReceivedData(const void* data, size_t len) {
  current_.receive_buffer.append(static_cast<const char*>(data), len);
  current_.bytes_received += len;
  while (!current_.headers_received) {
    auto end = current_.receive_buffer.find("\r\n\r\n");
    if ((end == std::string::npos ? current_.receive_buffer.size() : end + 4) >
        65536 - current_.framing_bytes) {
      HandleError("HTTP response headers exceed limit");
      return;
    }
    if (end == std::string::npos) return;
    if (!ParseResponseHeaders(current_.receive_buffer.substr(0, end))) {
      HandleError("Invalid HTTP response headers");
      return;
    }
    current_.framing_bytes += end + 4;
    current_.receive_buffer.erase(0, end + 4);
    if (current_.response.status_code < 200) {
      if (current_.response.status_code == 101) {
        HandleError("Unexpected HTTP protocol upgrade");
        return;
      }
      current_.response = Response();
      continue;
    }
    current_.headers_received = true;
    if (current_.request.method == HttpMethod::kHead ||
        current_.response.status_code == 204 ||
        current_.response.status_code == 304) {
      CompleteRequest();
      return;
    }
    bool has_transfer_encoding = false;
    for (const auto& header : current_.response.headers) {
      if (CaseInsensitiveEqual(header.name, "Transfer-Encoding")) {
        if (has_transfer_encoding ||
            !CaseInsensitiveEqual(header.value, "chunked")) {
          HandleError("Unsupported Transfer-Encoding");
          return;
        }
        has_transfer_encoding = true;
        current_.chunked_encoding = true;
        current_.response.chunked_encoding = true;
      } else if (CaseInsensitiveEqual(header.name, "Content-Length")) {
        size_t length = 0;
        const auto& value = header.value;
        auto parsed =
            std::from_chars(value.data(), value.data() + value.size(), length);
        if (value.empty() || parsed.ec != std::errc() ||
            parsed.ptr != value.data() + value.size() ||
            (current_.has_content_length &&
             current_.content_length != length)) {
          HandleError("Invalid Content-Length header");
          return;
        }
        current_.has_content_length = true;
        current_.content_length = length;
        current_.response.content_length = length;
      }
    }
    if (current_.has_content_length && current_.chunked_encoding) {
      HandleError("Ambiguous HTTP response framing");
      return;
    }
    if (current_.content_length > max_receive_buffer_size_) {
      HandleError("Response content length exceeds maximum buffer size");
      return;
    }
  }
  if (current_.chunked_encoding)
    ProcessChunkedBody();
  else
    ProcessFixedLengthBody();
}

bool HttpClient::DeliverBody(size_t len) {
  if (len > max_receive_buffer_size_ - current_.body_bytes_received) {
    HandleError("Response body exceeds maximum buffer size");
    return false;
  }
  auto lifetime = lifetime_;
  bool append = true;
  if (listener_) {
    listener_->OnProgress(
        this, current_.body_bytes_received + len,
        current_.has_content_length ? current_.content_length : 0);
    if (!IsAlive(lifetime) || current_.completed.load()) return false;
    append = listener_->OnBodyData(this, current_.receive_buffer.data(), len);
    if (!IsAlive(lifetime) || current_.completed.load()) return false;
  }
  if (append)
    current_.response.body.append(current_.receive_buffer.data(), len);
  current_.body_bytes_received += len;
  current_.receive_buffer.erase(0, len);
  return true;
}

void HttpClient::ProcessChunkedBody() {
  using Phase = RequestState::ChunkPhase;
  for (;;) {
    auto& buffer = current_.receive_buffer;
    switch (current_.chunk_phase) {
      case Phase::kSize: {
        auto end = buffer.find("\r\n");
        if ((end == std::string::npos ? buffer.size() : end) > 8192) {
          HandleError("Chunk size line exceeds limit");
          return;
        }
        if (end == std::string::npos) return;
        auto digits = std::min(end, buffer.find(';'));
        size_t size = 0;
        auto parsed =
            std::from_chars(buffer.data(), buffer.data() + digits, size, 16);
        if (digits == 0 || parsed.ec != std::errc() ||
            parsed.ptr != buffer.data() + digits) {
          HandleError("Invalid chunk size in chunked encoding");
          return;
        }
        if (size > max_receive_buffer_size_ - current_.body_bytes_received) {
          HandleError("Response body exceeds maximum buffer size");
          return;
        }
        buffer.erase(0, end + 2);
        current_.chunk_remaining = size;
        current_.chunk_phase = size ? Phase::kData : Phase::kTrailers;
        break;
      }
      case Phase::kData: {
        auto count = std::min(current_.chunk_remaining, buffer.size());
        if (count == 0) return;
        if (!DeliverBody(count)) return;
        current_.chunk_remaining -= count;
        if (current_.chunk_remaining == 0)
          current_.chunk_phase = Phase::kTerminator;
        break;
      }
      case Phase::kTerminator:
        if (buffer.size() < 2) return;
        if (buffer.compare(0, 2, "\r\n") != 0) {
          HandleError("Invalid chunk terminator");
          return;
        }
        buffer.erase(0, 2);
        current_.chunk_phase = Phase::kSize;
        break;
      case Phase::kTrailers: {
        auto end = buffer.find("\r\n");
        if ((end == std::string::npos ? buffer.size() : end + 2) >
            65536 - current_.framing_bytes) {
          HandleError("HTTP response trailers exceed limit");
          return;
        }
        if (end == std::string::npos) return;
        if (end == 0) {
          buffer.erase(0, 2);
          CompleteRequest();
          return;
        }
        auto colon = buffer.find(':');
        if (colon == 0 || colon >= end) {
          HandleError("Invalid HTTP response trailer");
          return;
        }
        current_.framing_bytes += end + 2;
        buffer.erase(0, end + 2);
        break;
      }
    }
  }
}

void HttpClient::ProcessFixedLengthBody() {
  size_t count = current_.receive_buffer.size();
  if (current_.has_content_length)
    count =
        std::min(count, current_.content_length - current_.body_bytes_received);
  if (count && !DeliverBody(count)) return;
  if (current_.has_content_length &&
      current_.body_bytes_received == current_.content_length)
    CompleteRequest();
}

void HttpClient::HandleRedirect() {
  if (current_.redirect_count >= max_redirects_) {
    HandleError("Too many redirects");
    return;
  }

  std::string location = current_.response.GetHeader("Location");
  if (location.empty()) {
    HandleError("Redirect response missing Location header");
    return;
  }

  if (listener_) {
    auto lifetime = lifetime_;
    listener_->OnRedirect(this, location);
    if (!IsAlive(lifetime)) return;
    if (current_.completed.load()) return;
  }

  current_.redirect_count++;

  // Parse new URL
  HttpUrl new_url = current_.request.url.base();
  if (location[0] == '/') {
    new_url.path = location;
  } else {
    new_url = HttpUrl(location);
  }
  if (!new_url.IsValid()) {
    HandleError("Invalid redirect URL");
    return;
  }

  const auto& old_url = current_.request.url;
  if (old_url.IsHttps() && !new_url.IsHttps()) {
    HandleError("Refusing insecure TLS redirect");
    return;
  }
  if (!CaseInsensitiveEqual(old_url.scheme, new_url.scheme) ||
      !CaseInsensitiveEqual(old_url.host, new_url.host) ||
      old_url.port != new_url.port) {
    // Also filter default headers, without changing defaults for future
    // requests.
    current_.strip_sensitive_headers = true;
  }

  // Update request URL
  current_.request.url = new_url;

  // Reset state for new request
  current_.receive_buffer.clear();
  current_.headers_received = false;
  current_.has_content_length = false;
  current_.content_length = 0;
  current_.bytes_received = 0;
  current_.body_bytes_received = 0;
  current_.chunked_encoding = false;
  current_.chunk_phase = RequestState::ChunkPhase::kSize;
  current_.chunk_remaining = 0;
  current_.framing_bytes = 0;
  current_.response = HttpClient::Response();

  // Disconnect and reconnect; defer replacement until this callback unwinds.
  ++output_generation_;
  output_scheduled_ = false;
  output_.Reset();
  Dispatch(Event::kRedirect);
  if (transport_) transport_->Close();

  current_.request.url = new_url;
  // Send new request
  if (!ConnectToHost(new_url)) {
    HandleError("failed to send request");
  }
}

void HttpClient::CompleteRequest() {
  if (current_.completed.load()) return;
  const int status = current_.response.status_code;
  if (follow_redirects_ && (status == 301 || status == 302 || status == 303 ||
                            status == 307 || status == 308)) {
    HandleRedirect();
    return;
  }
  for (const auto& header : current_.response.headers) {
    if (CaseInsensitiveEqual(header.name, "Set-Cookie"))
      ProcessSetCookieHeader(header.value, current_.request.url.host);
  }
  Dispatch(Event::kComplete);
}

void HttpClient::HandleError(const std::string& error) {
  if (!current_.completed.load())
    Dispatch(Event::kFail, error.empty() ? "HTTP request failed" : error);
}

void HttpClient::ScheduleRequestTimeout() {
  // Schedule timeout
  if (current_.timeout_ms > 0 && !current_.timeout_scheduled.exchange(true)) {
    auto lifetime = lifetime_;
    const uint64_t generation = active_request_generation_.load();
    timeout_handle_ = task_runner_->PostDelayedTaskWithHandle(
        [this, lifetime, generation]() {
          {
            std::lock_guard<std::mutex> lock(lifetime->mutex);
            if (!lifetime->alive) return;
            ++lifetime->active_callbacks;
            lifetime->callback_thread = std::this_thread::get_id();
          }
          Scoped callback_guard([lifetime]() {
            std::lock_guard<std::mutex> lock(lifetime->mutex);
            if (lifetime->active_callbacks > 0) --lifetime->active_callbacks;
            if (lifetime->active_callbacks == 0) {
              lifetime->callback_thread = std::thread::id();
              lifetime->cv.notify_all();
            }
          });

          if (active_request_generation_.load() == generation && IsBusy() &&
              !current_.completed.load()) {
            Dispatch(Event::kTimeout, "Request timeout");
          }
        },
        current_.timeout_ms);
  }
}

bool HttpClient::ConnectToHost(const HttpUrl& url) {
  state_ = State::kConnecting;
  auto lifetime = lifetime_;
  const uint64_t generation = active_request_generation_.load();
  // Socket construction and replacement must run on the runner. Otherwise a
  // connection callback can run before TcpClient has installed its socket.
  // Deferral also lets the old transport's redirect callback unwind first.
  task_runner_->PostTask([this, lifetime, generation, url] {
    {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      if (!lifetime->alive) return;
      ++lifetime->active_callbacks;
      lifetime->callback_thread = std::this_thread::get_id();
    }
    Scoped guard([lifetime] {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      if (--lifetime->active_callbacks == 0) {
        lifetime->callback_thread = std::thread::id();
        lifetime->cv.notify_all();
      }
    });
    if (active_request_generation_.load() != generation ||
        current_.completed.load())
      return;
    ScheduleRequestTimeout();  // Includes TCP connection and TLS handshake.
    if (url.IsHttps()) {
      transport_ = CreateTlsTransport(task_runner_, transport_callbacks_.get());
    } else {
      transport_ = std::make_unique<PlainTcpTransport>(
          task_runner_, transport_callbacks_.get());
    }
    TlsCertConfig cfg = TlsCertConfig::Default();
    if (!verify_ssl_) {
      cfg = TlsCertConfig::Insecure();
    } else if (!ssl_cert_path_.empty()) {
      cfg.ca_file = ssl_cert_path_;
    }
    auto tls = url.IsHttps() ? CreateTlsContext(cfg) : nullptr;
    if (!transport_->Connect(url, tls))
      HandleError("Failed to initiate connection");
  });
  return true;
}

HttpHeaders HttpClient::MergeHeaders(const HttpHeaders& request_headers) {
  HttpHeaders merged = default_headers_;

  for (const auto& header : request_headers) {
    // Check if header already exists in default headers
    bool found = false;
    for (auto& default_header : merged) {
      if (default_header.name == header.name) {
        default_header.value = header.value;
        found = true;
        break;
      }
    }
    if (!found) {
      merged.push_back(header);
    }
  }

  return merged;
}

bool HttpClient::ParseResponseHeaders(const std::string& header_text) {
  std::istringstream stream(header_text);
  std::string line;

  // Parse status line
  if (std::getline(stream, line)) {
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Parse "HTTP/1.1 200 OK"
    std::istringstream status_line(line);
    std::string http_version;
    std::string status;
    status_line >> http_version >> status;
    auto parsed = std::from_chars(status.data(), status.data() + status.size(),
                                  current_.response.status_code);
    if ((http_version != "HTTP/1.0" && http_version != "HTTP/1.1") ||
        status.size() != 3 || parsed.ec != std::errc() ||
        parsed.ptr != status.data() + status.size() ||
        current_.response.status_code < 100 ||
        current_.response.status_code > 599)
      return false;
    std::getline(status_line, current_.response.status_message);
    // Trim leading space
    if (!current_.response.status_message.empty() &&
        current_.response.status_message[0] == ' ') {
      current_.response.status_message.erase(0, 1);
    }
  }

  // Parse headers
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      break;
    }

    size_t colon_pos = line.find(':');
    if (colon_pos == 0 || colon_pos == std::string::npos) return false;
    {
      std::string name = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);

      value = TrimWhitespace(value);
      current_.response.headers.emplace_back(name, value);
    }
  }
  return true;
}

void HttpClient::ProcessSetCookieHeader(const std::string& cookie_header,
                                        const std::string& domain) {
  // Simple cookie parsing: "name=value; other attributes"
  size_t eq_pos = cookie_header.find('=');
  if (eq_pos == std::string::npos) {
    return;
  }

  size_t semicolon_pos = cookie_header.find(';');
  std::string name = cookie_header.substr(0, eq_pos);
  std::string value =
      semicolon_pos != std::string::npos
          ? cookie_header.substr(eq_pos + 1, semicolon_pos - eq_pos - 1)
          : cookie_header.substr(eq_pos + 1);

  SetCookie(name, value, domain);
}

std::string HttpClient::GenerateBoundary() {
  static const char alphanum[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::string boundary = "----XTILSFormBoundary";
  for (int i = 0; i < 16; i++) {
    boundary += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return boundary;
}

}  // namespace xtils
