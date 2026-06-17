#include "xtils/config/config_watcher.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

#include "xtils/logging/logger.h"

namespace xtils {

class ConfigWatcher::Impl {
 public:
  Impl(Config* cfg, TaskRunner* runner) : cfg_(cfg), runner_(runner) {}
  ~Impl() { Stop(); }

  bool Watch(const std::string& filename, OnReload on_reload) {
    Stop();  // replace any prior watch

    fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
      LogE("ConfigWatcher: inotify_init1 failed: %s", strerror(errno));
      return false;
    }
    // Watch the file via its containing directory: editors that "atomic
    // save" by rename/replace would invalidate a direct file watch. We
    // pick directory watch + filename matching for robustness.
    auto last_slash = filename.find_last_of('/');
    std::string dir = (last_slash == std::string::npos)
                          ? std::string(".")
                          : filename.substr(0, last_slash);
    base_name_ = (last_slash == std::string::npos)
                     ? filename
                     : filename.substr(last_slash + 1);
    wd_ = inotify_add_watch(fd_, dir.c_str(),
                            IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO);
    if (wd_ < 0) {
      LogE("ConfigWatcher: inotify_add_watch('%s') failed: %s", dir.c_str(),
           strerror(errno));
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    filename_ = filename;
    on_reload_ = std::move(on_reload);

    runner_->AddFileDescriptorWatch(fd_, [this]() { OnFdReady(); });
    return true;
  }

  void Stop() {
    if (fd_ < 0) return;
    runner_->RemoveFileDescriptorWatch(fd_);
    if (wd_ >= 0) inotify_rm_watch(fd_, wd_);
    ::close(fd_);
    fd_ = -1;
    wd_ = -1;
    on_reload_ = nullptr;
  }

 private:
  void OnFdReady() {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    bool relevant = false;
    while (true) {
      ssize_t n = ::read(fd_, buf, sizeof(buf));
      if (n <= 0) break;
      for (char* p = buf; p < buf + n;) {
        auto* ev = reinterpret_cast<struct inotify_event*>(p);
        if (ev->len > 0 && base_name_ == ev->name) relevant = true;
        p += sizeof(struct inotify_event) + ev->len;
      }
    }
    if (!relevant || !on_reload_) return;
    if (cfg_->LoadFile(filename_)) {
      on_reload_(*cfg_);
    } else {
      LogW("ConfigWatcher: reload of '%s' failed", filename_.c_str());
    }
  }

  Config* cfg_;
  TaskRunner* runner_;
  int fd_ = -1;
  int wd_ = -1;
  std::string filename_;
  std::string base_name_;
  OnReload on_reload_;
};

ConfigWatcher::ConfigWatcher(Config* config, TaskRunner* task_runner)
    : impl_(std::make_unique<Impl>(config, task_runner)) {}

ConfigWatcher::~ConfigWatcher() = default;

bool ConfigWatcher::Watch(const std::string& filename, OnReload on_reload) {
  return impl_->Watch(filename, std::move(on_reload));
}

void ConfigWatcher::Stop() { impl_->Stop(); }

}  // namespace xtils
