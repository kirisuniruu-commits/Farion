#include "DhConfig.hpp"
#include "RenderDistanceHook.hpp"
#include "lod/LodCache.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Signature.hpp>

namespace {

using dhmod::DhConfig;
using dhmod::Quality;

constexpr const char *kModuleId = "farion.main";

int parseInt(std::string_view v, int fallback) {
  std::string s(v);
  char *end = nullptr;
  errno = 0;
  const long p = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0' || errno == ERANGE) return fallback;
  return static_cast<int>(std::clamp<long>(p, -1000000, 1000000));
}

bool parseBool(std::string_view v, bool fallback) {
  if (v == "true" || v == "1") return true;
  if (v == "false" || v == "0") return false;
  return fallback;
}

Quality qualityFromIndex(int i, Quality fallback) {
  switch (i) {
  case 0: return Quality::Low;
  case 1: return Quality::Medium;
  case 2: return Quality::High;
  default: return fallback;
  }
}

void normalize(DhConfig &c) {
  c.version = DhConfig{}.version;
  c.lodDistanceChunks =
      std::clamp(c.lodDistanceChunks, dhmod::kMinDistance, dhmod::kMaxDistance);
  c.memoryBudgetMb =
      std::clamp(c.memoryBudgetMb, dhmod::kMinMemoryMb, dhmod::kMaxMemoryMb);
  c.cpuBudgetPercent =
      std::clamp(c.cpuBudgetPercent, dhmod::kMinCpu, dhmod::kMaxCpu);
  c.quality = qualityFromIndex(static_cast<int>(c.quality), Quality::Medium);
  c.tilesPerSecond = std::clamp(c.tilesPerSecond, dhmod::kMinTilesPerSecond,
                                dhmod::kMaxTilesPerSecond);
  c.maxRenderDistance = std::clamp(c.maxRenderDistance, 0,
                                   dhmod::kMaxRenderDistanceLimit);
}

size_t budgetBytes(const DhConfig &c) {
  return static_cast<size_t>(c.memoryBudgetMb) * 1024u * 1024u;
}

class DistantHorizonsMod {
public:
  static DistantHorizonsMod &instance() {
    static DistantHorizonsMod mod;
    return mod;
  }

  DistantHorizonsMod() : mSelf(*ll::mod::NativeMod::current()) {}

  bool load() {
    auto &log = mSelf.getLogger();
    std::error_code ec;
    std::filesystem::create_directories(mSelf.getConfigDir(), ec);

    std::lock_guard lock(mMutex);
    mConfigFile.emplace();
    if (!mConfigFile->load()) {
      log.error("Failed to load config");
      mConfigFile.reset();
      return false;
    }
    normalize(mConfigFile->value());
    mConfigFile->save();

    // Resolve game hooks. Missing signatures are NOT an error: the mod simply
    // stays inert so it can never crash or slow down an unsupported version.
    loadSignatures();
    return true;
  }

  bool enable() {
    const DhConfig cfg = snapshot();
    if (cfg.enabled) startRuntime(cfg);
    dhmod::RenderDistanceHook::instance().start(mSelf, cfg.maxRenderDistance);

    const bool ok =
        pl::modmenu::ModuleBuilder(kModuleId, "Farion")
            .modId(mSelf.getId())
            .description(mHooksReady
                             ? "Distant terrain (LOD) renderer."
                             : "Settings ready. Render hooks are OFF: add "
                               "signatures to config/signatures.json.")
            .defaultEnabled(cfg.enabled)
            .onToggle(onToggle)
            .config("lodDistanceChunks", "LOD Distance (chunks)",
                    pl::modmenu::ConfigType::SliderInt,
                    std::to_string(cfg.lodDistanceChunks),
                    std::to_string(dhmod::kMinDistance),
                    std::to_string(dhmod::kMaxDistance))
            .config("quality", "Quality", pl::modmenu::ConfigType::Radio,
                    std::to_string(static_cast<int>(cfg.quality)),
                    std::string(dhmod::kQualityMenuOptions))
            .config("memoryBudgetMb", "Memory Budget (MB)",
                    pl::modmenu::ConfigType::SliderInt,
                    std::to_string(cfg.memoryBudgetMb),
                    std::to_string(dhmod::kMinMemoryMb),
                    std::to_string(dhmod::kMaxMemoryMb))
            .config("cpuBudgetPercent", "CPU Budget (%)",
                    pl::modmenu::ConfigType::SliderInt,
                    std::to_string(cfg.cpuBudgetPercent),
                    std::to_string(dhmod::kMinCpu),
                    std::to_string(dhmod::kMaxCpu))
            .config("fogBlend", "Fog Blend", pl::modmenu::ConfigType::Toggle,
                    cfg.fogBlend ? "true" : "false")
            .config("tilesPerSecond", "Tiles per Second",
                    pl::modmenu::ConfigType::SliderInt,
                    std::to_string(cfg.tilesPerSecond),
                    std::to_string(dhmod::kMinTilesPerSecond),
                    std::to_string(dhmod::kMaxTilesPerSecond))
            .config("pauseInMenus", "Pause In Menus",
                    pl::modmenu::ConfigType::Toggle,
                    cfg.pauseInMenus ? "true" : "false")
            .config("debugLogging", "Debug Logging",
                    pl::modmenu::ConfigType::Toggle,
                    cfg.debugLogging ? "true" : "false")
            .config("maxRenderDistance", "Max Render Distance (restart)",
                    pl::modmenu::ConfigType::SliderInt,
                    std::to_string(cfg.maxRenderDistance), "0",
                    std::to_string(dhmod::kMaxRenderDistanceLimit))
            .config("clearCache", "Clear LOD Cache (flip to clear)",
                    pl::modmenu::ConfigType::Toggle, "false")
            .onConfigChanged(onConfigChanged)
            .registerModule();
    if (!ok) mSelf.getLogger().error("Mod Menu registration failed");
    return ok;
  }

  bool disable() {
    pl::modmenu::unregisterModule(kModuleId);
    dhmod::RenderDistanceHook::instance().stop();
    stopRuntime();
    return true;
  }

  bool unload() {
    disable();
    std::lock_guard lock(mMutex);
    mConfigFile.reset();
    return true;
  }

private:
  // The cache and the worker thread exist only while the module is switched on,
  // so turning Farion off in the menu frees its memory and stops its thread.
  void startRuntime(const DhConfig &cfg) {
    std::lock_guard lock(mRuntimeMutex);
    if (mCache) return;
    mCache = std::make_unique<dh::LodCache>(budgetBytes(cfg));
    mWorker = std::make_unique<dh::ThrottledWorker>(cfg.cpuBudgetPercent, 256);
    mSelf.getLogger().info("Farion runtime started");
  }

  void stopRuntime() {
    std::unique_ptr<dh::ThrottledWorker> worker;
    std::unique_ptr<dh::LodCache> cache;
    {
      std::lock_guard lock(mRuntimeMutex);
      worker = std::move(mWorker);
      cache = std::move(mCache);
    }
    worker.reset(); // joins the thread outside the lock
    cache.reset();
  }

  DhConfig snapshot() {
    std::lock_guard lock(mMutex);
    if (!mConfigFile) return DhConfig{};
    DhConfig c = mConfigFile->value();
    normalize(c);
    return c;
  }

  void loadSignatures() {
    auto &log = mSelf.getLogger();
    mHooksReady = false;
    std::ifstream in(mSelf.getConfigDir() / "signatures.json");
    if (!in) {
      log.warn("config/signatures.json not found; render hooks disabled");
      return;
    }
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("signatures")) {
      log.warn("signatures.json is invalid; render hooks disabled");
      return;
    }
    int resolved = 0, total = 0;
    for (auto &[name, value] : j["signatures"].items()) {
      ++total;
      const std::string sig = value.is_string() ? value.get<std::string>() : "";
      if (sig.empty()) {
        log.warn("signature '{}' is empty", name);
        continue;
      }
      const uintptr_t addr =
          pl::memory::resolveSignature(sig, "libminecraftpe.so");
      if (addr != 0) {
        ++resolved;
        log.info("signature '{}' resolved", name);
      } else {
        log.warn("signature '{}' did not match this Minecraft build", name);
      }
    }
    // Hooks are only armed when every signature matched; a partial match on
    // the wrong version is how mods crash games.
    mHooksReady = total > 0 && resolved == total;
    log.info("render hooks: {} ({}/{} signatures)",
             mHooksReady ? "ready" : "off", resolved, total);
  }

  static void onToggle(std::string_view id, bool enabled) {
    if (id != kModuleId) return;
    auto &self = instance();
    DhConfig cfg;
    {
      std::lock_guard lock(self.mMutex);
      if (!self.mConfigFile) return;
      self.mConfigFile->value().enabled = enabled;
      self.mConfigFile->save();
      cfg = self.mConfigFile->value();
    }
    // Takes effect immediately, no restart: off frees the thread and memory.
    if (enabled) self.startRuntime(cfg);
    else self.stopRuntime();
    self.mSelf.getLogger().info("Farion {}", enabled ? "enabled" : "disabled");
  }

  static void onConfigChanged(std::string_view id, std::string_view key,
                              std::string_view value) {
    if (id != kModuleId) return;
    auto &self = instance();
    if (key == "clearCache") {
      std::lock_guard lock(self.mRuntimeMutex);
      if (self.mCache) self.mCache->clear();
      return;
    }
    DhConfig cfg;
    {
      std::lock_guard lock(self.mMutex);
      if (!self.mConfigFile) return;
      auto &c = self.mConfigFile->value();
      if (key == "lodDistanceChunks")
        c.lodDistanceChunks = parseInt(value, c.lodDistanceChunks);
      else if (key == "quality")
        c.quality = qualityFromIndex(
            parseInt(value, static_cast<int>(c.quality)), c.quality);
      else if (key == "memoryBudgetMb")
        c.memoryBudgetMb = parseInt(value, c.memoryBudgetMb);
      else if (key == "cpuBudgetPercent")
        c.cpuBudgetPercent = parseInt(value, c.cpuBudgetPercent);
      else if (key == "fogBlend")
        c.fogBlend = parseBool(value, c.fogBlend);
      else if (key == "tilesPerSecond")
        c.tilesPerSecond = parseInt(value, c.tilesPerSecond);
      else if (key == "pauseInMenus")
        c.pauseInMenus = parseBool(value, c.pauseInMenus);
      else if (key == "maxRenderDistance")
        c.maxRenderDistance = parseInt(value, c.maxRenderDistance);
      else if (key == "debugLogging")
        c.debugLogging = parseBool(value, c.debugLogging);
      else
        return;
      normalize(c);
      self.mConfigFile->save();
      cfg = c;
    }
    // Apply live; no restart needed.
    std::lock_guard lock(self.mRuntimeMutex);
    if (self.mCache) self.mCache->setBudget(budgetBytes(cfg));
    if (self.mWorker) self.mWorker->setCpuPercent(cfg.cpuBudgetPercent);
  }

  ll::mod::NativeMod &mSelf;
  std::mutex mMutex;        // guards mConfigFile
  std::mutex mRuntimeMutex; // guards mCache / mWorker
  std::optional<pl::config::ConfigFile<DhConfig>> mConfigFile;
  std::unique_ptr<dh::LodCache> mCache;
  std::unique_ptr<dh::ThrottledWorker> mWorker;
  bool mHooksReady = false;
};

} // namespace

PL_REGISTER_MOD(DistantHorizonsMod, DistantHorizonsMod::instance())
