#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <pl/Config.hpp>

namespace dhmod {

enum class Quality { Low, Medium, High };

struct DhConfig {
  int version = 1;
  bool enabled = true;
  int lodDistanceChunks = 96;
  Quality quality = Quality::Medium;
  int memoryBudgetMb = 128;
  int cpuBudgetPercent = 10;
  bool fogBlend = true;
  int tilesPerSecond = 16;
  bool pauseInMenus = true;
  bool debugLogging = false;
  bool renderDistanceEnabled = false;
  int maxRenderDistance = 32;
};

inline constexpr int kMinDistance = 16;
inline constexpr int kMaxDistance = 512;
inline constexpr int kMinMemoryMb = 32;
inline constexpr int kMaxMemoryMb = 1024;
inline constexpr int kMinCpu = 1;
inline constexpr int kMaxCpu = 50;
inline constexpr int kMinTilesPerSecond = 1;
inline constexpr int kMaxTilesPerSecond = 64;
inline constexpr int kMaxRenderDistanceLimit = 96;
inline constexpr std::string_view kQualityMenuOptions = "Low,Medium,High";

} // namespace dhmod

namespace pl::config {

template <> struct Schema<dhmod::DhConfig> {
  static constexpr std::string_view title = "Farion";
  static constexpr std::string_view description =
      "Level-of-detail distant terrain settings.";

  static constexpr FieldSchema field(std::string_view name) {
    if (name == "version")
      return {"Version", "Config schema version managed by the mod.",
              std::nullopt, std::nullopt, true};
    if (name == "enabled")
      return {"Enabled", "Master switch for distant terrain.", std::nullopt,
              std::nullopt, false};
    if (name == "lodDistanceChunks")
      return {"LOD Distance (chunks)", "How far distant terrain extends.",
              dhmod::kMinDistance, dhmod::kMaxDistance, false};
    if (name == "quality")
      return {"Quality", "Low / Medium / High tile resolution.", std::nullopt,
              std::nullopt, false};
    if (name == "memoryBudgetMb")
      return {"Memory Budget (MB)", "Hard cap for the LOD tile cache.",
              dhmod::kMinMemoryMb, dhmod::kMaxMemoryMb, false};
    if (name == "cpuBudgetPercent")
      return {"CPU Budget (%)",
              "Max share of one core the background LOD thread may use.",
              dhmod::kMinCpu, dhmod::kMaxCpu, false};
    if (name == "fogBlend")
      return {"Fog Blend", "Fade distant terrain into the fog (needs render hooks).",
              std::nullopt, std::nullopt, false};
    if (name == "tilesPerSecond")
      return {"Tiles per Second", "Max LOD tiles generated per second.",
              dhmod::kMinTilesPerSecond, dhmod::kMaxTilesPerSecond, false};
    if (name == "pauseInMenus")
      return {"Pause In Menus", "Stop background work while a menu is open.",
              std::nullopt, std::nullopt, false};
    if (name == "debugLogging")
      return {"Debug Logging", "Write extra diagnostics to the log.",
              std::nullopt, std::nullopt, false};
    if (name == "renderDistanceEnabled")
      return {"Render Distance Changer",
              "Switch the render distance slider unlock on or off.",
              std::nullopt, std::nullopt, false};
    if (name == "maxRenderDistance")
      return {"Max Render Distance (restart)",
              "Raises the top of the in-game Render Distance slider. Only "
              "unlocks the slider; 0 = off. Higher distances load more chunks "
              "and can lag weak phones.",
              0, dhmod::kMaxRenderDistanceLimit, false};
    return {};
  }
};

} // namespace pl::config
