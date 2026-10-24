#include "DhConfig.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dh_config_gen <mod-package-dir>\n";
    return 2;
  }
  const std::filesystem::path configDir = std::filesystem::path(argv[1]) / "config";
  pl::config::ConfigFile<dhmod::DhConfig> config(
      dhmod::DhConfig{}, configDir / "config.json",
      configDir / "config.schema.json");
  if (!config.save() || !config.writeSchema()) {
    std::cerr << "failed to write config files in " << configDir << '\n';
    return 1;
  }
  std::cout << "generated " << config.configPath() << '\n';
  return 0;
}
