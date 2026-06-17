#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "xtils/net/http_client.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

// HttpClientPool — a fixed-size pool of HttpClient instances for concurrent
// HTTP requests.
//
// A single HttpClient is single-flight (one in-progress request at a time).
// HttpClientPool owns N clients sharing one TaskRunner and lets callers
// borrow them via Acquire() / Send() helpers.
//
// Usage:
//   HttpClientPool pool(&task_runner, 4);
//   auto resp = pool.Send(request);            // sync, auto acquire+release
//   auto handle = pool.Acquire();              // borrow a client
//   handle.client()->SendAsync(request, ...);  // ...
//
class HttpClientPool {
 public:
  class Handle {
   public:
    Handle() = default;
    Handle(HttpClientPool* pool, HttpClient* client)
        : pool_(pool), client_(client) {}
    ~Handle() {
      if (pool_ && client_) pool_->Release(client_);
    }

    Handle(Handle&& other) noexcept
        : pool_(other.pool_), client_(other.client_) {
      other.pool_ = nullptr;
      other.client_ = nullptr;
    }
    Handle& operator=(Handle&& other) noexcept {
      if (this != &other) {
        if (pool_ && client_) pool_->Release(client_);
        pool_ = other.pool_;
        client_ = other.client_;
        other.pool_ = nullptr;
        other.client_ = nullptr;
      }
      return *this;
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HttpClient* client() const { return client_; }
    HttpClient* operator->() const { return client_; }
    explicit operator bool() const { return client_ != nullptr; }

   private:
    HttpClientPool* pool_ = nullptr;
    HttpClient* client_ = nullptr;
  };

  // Construct a pool of `size` HttpClients sharing `task_runner`.
  // size <= 0 falls back to std::thread::hardware_concurrency().
  HttpClientPool(TaskRunner* task_runner, int size = 0);
  ~HttpClientPool();

  HttpClientPool(const HttpClientPool&) = delete;
  HttpClientPool& operator=(const HttpClientPool&) = delete;

  // Borrow a client. Blocks up to `timeout` waiting for a free instance.
  // Returns an empty handle on timeout.
  Handle Acquire(std::chrono::milliseconds timeout =
                     std::chrono::milliseconds(30000));

  // Synchronous request helper: acquires, sends, releases.
  // Returns an error Response (status_code == 0, status_message describing
  // the failure) if no client could be acquired within `acquire_timeout`.
  HttpClient::Response Send(const HttpClient::Request& request,
                            std::chrono::milliseconds acquire_timeout =
                                std::chrono::milliseconds(30000));

  size_t Size() const { return clients_.size(); }
  size_t AvailableForTesting();

 private:
  friend class Handle;
  void Release(HttpClient* client);

  std::vector<std::unique_ptr<HttpClient>> clients_;  // owned
  std::vector<HttpClient*> free_;                     // free list
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
};

}  // namespace xtils
