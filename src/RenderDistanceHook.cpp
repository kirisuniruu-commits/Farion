#include "RenderDistanceHook.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace dhmod {
namespace {

// 7 instructions that set up the call that builds the Render Distance option:
//   mov x0,x25 / mov w1,#<option id> / mov w2,#1 / mov w3,#16 /
//   mov w6,w26 / str x20,[sp,#8] / strb wzr,[sp]      -> then BL <constructor>
// The option id immediate is wildcarded.
constexpr const char *kPattern =
    "E0 03 19 AA ?1 ?? ?? 52 22 00 80 52 03 02 80 52 "
    "E6 03 1A 2A F4 07 00 F9 FF 03 00 39";
constexpr size_t kBlOffset = 28; // the BL follows the 7 matched instructions

// NumberComponent<int>-style constructor:
//   (this, optionId, a2, a3, name*, label*, defaultValue, allowedValues*,
//    flag [stack], extra* [stack])
using CtorFn = void (*)(void *, int, int, int, void *, void *, int,
                        std::vector<int> *, bool, void *);

CtorFn gOriginal = nullptr;
std::atomic<int> gMaxDistance{0};
std::atomic<int> gOptionId{-1};

void detour(void *self, int id, int a2, int a3, void *s4, void *s5, int def,
            std::vector<int> *values, bool flag, void *extra) {
  const int want = gMaxDistance.load(std::memory_order_relaxed);
  if (id == gOptionId.load(std::memory_order_relaxed) && want > 0 && values &&
      !values->empty()) {
    const auto [lo, hi] = std::minmax_element(values->begin(), values->end());
    if (want > *hi) {
      std::vector<int> extended;
      extended.reserve(static_cast<size_t>(want - *lo + 1));
      for (int v = *lo; v <= want; ++v) extended.push_back(v);
      gOriginal(self, id, a2, a3, s4, s5, def, &extended, flag, extra);
      return;
    }
  }
  gOriginal(self, id, a2, a3, s4, s5, def, values, flag, extra);
}

class Impl {
public:
  void start(ll::mod::NativeMod &mod, int maxDistance) {
    stop();
    gMaxDistance = maxDistance;
    if (maxDistance <= 0) {
      mod.getLogger().info("Render distance unlock is off");
      return;
    }
    mStop = false;
    mThread = std::thread([this, &mod] { run(mod); });
  }

  void stop() {
    {
      std::lock_guard lock(mMutex);
      mStop = true;
    }
    mCv.notify_all();
    if (mThread.joinable()) mThread.join();
    mHook.reset();
  }

private:
  bool sleepFor(std::chrono::milliseconds d) {
    std::unique_lock lock(mMutex);
    return !mCv.wait_for(lock, d, [this] { return mStop; });
  }

  void run(ll::mod::NativeMod &mod) {
    auto &log = mod.getLogger();
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = mod.getDataDir();
    fs::create_directories(dir, ec);
    const fs::path pending = dir / "rd_hook.pending";
    const fs::path disabled = dir / "rd_hook.disabled";

    if (fs::exists(pending, ec)) {
      // The game died during the install window last time.
      std::ofstream(disabled) << "auto-disabled after a crash\n";
      fs::remove(pending, ec);
    }
    if (fs::exists(disabled, ec)) {
      log.warn("Render distance unlock is disabled (the game crashed while it "
               "was installing last time). Delete {} to try again.",
               disabled.string());
      return;
    }

    uintptr_t site = 0;
    for (int attempt = 0; attempt < 4 && site == 0; ++attempt) {
      if (attempt > 0 && !sleepFor(std::chrono::seconds(3))) return;
      site = pl::memory::resolveSignature(kPattern, "libminecraftpe.so");
    }
    if (site == 0) {
      log.warn("Render distance hook point not found: this Minecraft build is "
               "not supported. Nothing was changed.");
      return;
    }

    uint32_t movz = 0, bl = 0;
    std::memcpy(&movz, reinterpret_cast<const void *>(site + 4), 4);
    std::memcpy(&bl, reinterpret_cast<const void *>(site + kBlOffset), 4);
    if ((bl >> 26) != 0x25u || (movz & 0xffe0001fu) != 0x52800001u) {
      log.warn("Render distance hook point looks wrong; skipped");
      return;
    }
    int32_t imm26 = static_cast<int32_t>(bl & 0x03ffffffu);
    if (imm26 & 0x02000000) imm26 -= 0x04000000;
    const uintptr_t target =
        site + kBlOffset + static_cast<intptr_t>(imm26) * 4;
    gOptionId = static_cast<int>((movz >> 5) & 0xffffu);

    { std::ofstream(pending) << "installing\n"; }
    mHook = pl::memory::HookHandle(
        reinterpret_cast<void *>(target), reinterpret_cast<void *>(&detour),
        reinterpret_cast<void **>(&gOriginal));
    if (!mHook.installed()) {
      log.error("Render distance hook install failed");
      fs::remove(pending, ec);
      return;
    }
    log.info("Render distance unlock ready: slider max {}", gMaxDistance.load());

    // If the game is still alive after this window, the install was safe.
    if (sleepFor(std::chrono::seconds(45))) fs::remove(pending, ec);
  }

  std::mutex mMutex;
  std::condition_variable mCv;
  bool mStop = false;
  std::thread mThread;
  pl::memory::HookHandle mHook;
};

Impl &impl() {
  static Impl i;
  return i;
}

} // namespace

RenderDistanceHook &RenderDistanceHook::instance() {
  static RenderDistanceHook h;
  return h;
}

void RenderDistanceHook::start(ll::mod::NativeMod &mod, int maxDistance) {
  impl().start(mod, maxDistance);
}

void RenderDistanceHook::stop() { impl().stop(); }

} // namespace dhmod
