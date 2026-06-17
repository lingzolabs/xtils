#include "xtils/net/http_client_pool.h"

#include <thread>

#include "xtils/logging/logger.h"

namespace xtils {

HttpClientPool::HttpClientPool(TaskRunner* task_runner, int size) {
  int n = size > 0 ? size
                   : static_cast<int>(std::thread::hardware_concurrency());
  if (n <= 0) n = 1;
  clients_.reserve(static_cast<size_t>(n));
  free_.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    clients_.push_back(std::make_unique<HttpClient>(task_runner));
    free_.push_back(clients_.back().get());
  }
}

HttpClientPool::~HttpClientPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    cv_.notify_all();
  }
  // Cancel any in-flight requests before destroying the clients.
  for (auto& c : clients_) {
    c->Cancel();
  }
}

HttpClientPool::Handle HttpClientPool::Acquire(
    std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!cv_.wait_for(lock, timeout,
                    [this] { return !free_.empty() || stopping_; })) {
    return Handle();  // timeout
  }
  if (stopping_ || free_.empty()) return Handle();
  HttpClient* client = free_.back();
  free_.pop_back();
  return Handle(this, client);
}

void HttpClientPool::Release(HttpClient* client) {
  if (!client) return;
  std::lock_guard<std::mutex> lock(mutex_);
  free_.push_back(client);
  cv_.notify_one();
}

HttpClient::Response HttpClientPool::Send(
    const HttpClient::Request& request,
    std::chrono::milliseconds acquire_timeout) {
  auto handle = Acquire(acquire_timeout);
  if (!handle) {
    HttpClient::Response err;
    err.status_code = 0;
    err.status_message = "HttpClientPool: timed out acquiring client";
    return err;
  }
  return handle->Send(request);
}

size_t HttpClientPool::AvailableForTesting() {
  std::lock_guard<std::mutex> lock(mutex_);
  return free_.size();
}

}  // namespace xtils
