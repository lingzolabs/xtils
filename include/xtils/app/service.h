#include <string>
#include <vector>

#include "xtils/app/app.h"
#include "xtils/config/config.h"
#include "xtils/utils/weak_ptr.h"

void app_version(uint32_t& major, uint32_t& minor, uint32_t& patch);
// call by internal main function
void app_main(xtils::App& ctx, const std::vector<std::string>& args);

namespace xtils {

/**
 * @brief Base service interface.
 *
 * Lifecycle:
 *   1. Init()   — called after infrastructure (thread pool, event loop) is ready
 *   2. Deinit() — called BEFORE infrastructure shutdown, so services can still
 *                 use event loop, thread pool, timers for cleanup (e.g. WebSocket
 *                 close handshake, flushing pending I/O)
 *
 * Constraints for Deinit():
 *   - Keep it fast (< 3s). The framework does NOT enforce a timeout but a slow
 *     Deinit blocks the entire shutdown sequence.
 *   - Do NOT call std::exit() or abort() — let the framework finish orderly.
 *   - Safe to call: network close, cancel timers, flush buffers, log.
 */
class IService {
 public:
  explicit IService(const char* n) : name(n) {}
  virtual void Init() = 0;
  virtual void Deinit() = 0;
  virtual ~IService() = default;

  // Names of services this service depends on. App initialises
  // dependencies first (topological order), and deinitialises in the
  // reverse order. Default: no dependencies.
  virtual std::vector<std::string> Dependencies() const { return {}; }

  const std::string& Name() const { return name; }

 protected:
  std::string name;
  friend class App;
  xtils::App* ctx;
  Config config;
};

template <typename ServiceType>
class Service : public IService {
 public:
  explicit Service(const char* n) : IService(n), weak_factory_(this) {}
  virtual void Init() = 0;
  virtual void Deinit() = 0;
  auto GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  template <typename T>
  void Emit(const T& e) {
    ctx->Emit<T>(e);
  }

 protected:
  WeakPtrFactory<Service<ServiceType>> weak_factory_;
};

/**
 * @brief check if the global context is ok
 * @return true if ok
 */
bool IsOk();
/**
 * @brief init the global context
 * @param args command line arguments
 */
void Init(const std::vector<std::string>& args);

/**
 * @brief init the global context for backward
 * @param argc number of argv
 * @param argv params const char*
 */
void Init(int argc, const char* const argv[]);
/**
 * @brief wait until resource released
 */
void Shutdown();
/**
 * @brief run the main loop, return when shutdown
 */
void RunForever();
/**
 * @brief run as a daemon process
 */
void RunDaemon();

}  // namespace xtils
