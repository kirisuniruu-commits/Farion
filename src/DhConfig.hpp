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
};

inline constexpr int kMinDistance = 16;
inline constexpr int kMaxDistance = 512;
inline constexpr int kMinMemoryMb = 32;
inline constexpr int kMaxMemoryMb = 1024;
inline constexpr int kMinCpu = 1;
inline constexpr int kMaxCpu = 50;
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
    return {};
  }
};

} // namespace pl::config
