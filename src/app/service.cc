#include "xtils/app/service.h"

#include <vector>

#include "xtils/app/app.h"
#include "xtils/app/auto-gen.h"
#include "xtils/system/signal_handler.h"

void app_version(uint32_t& major, uint32_t& minor, uint32_t& patch) {
  major = XTILS_VER_MAJOR;
  minor = XTILS_VER_MINOR;
  patch = XTILS_VER_PATCH;
}

__attribute__((weak)) void app_main(xtils::App& ctx,
                                    const std::vector<std::string>& argv) {}

namespace xtils {
void Init(const std::vector<std::string>& args) { App::Ins()->Init(args); }

void Init(int argc, const char* const argv[]) { Init({argv, argv + argc}); }

void RunForever() { App::Ins()->Run(); }
void RunDaemon() { App::Ins()->RunDaemon(); }

bool IsOk() { return !system::SignalHandler::IsShutdownRequested(); }

void Shutdown() {
  if (IsOk()) system::SignalHandler::Shutdown();
  while (App::Ins()->IsRunning()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace xtils
