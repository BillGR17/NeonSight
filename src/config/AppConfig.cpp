#include "AppConfig.hpp"
#include "../utils/json.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <iostream>

namespace NeonSight {

using json = nlohmann::json;

std::string AppConfig::GetConfigFilePath() const {
  const char *home = std::getenv("HOME");
  std::string path = home ? std::string(home) : ".";
  return path + "/.config/NeonSight/config.json";
}

void AppConfig::Load() {
  std::string path = GetConfigFilePath();
  if (!std::filesystem::exists(path))
    return;

  try {
    std::ifstream f(path);
    json data = json::parse(f);

    if (data.contains("activeCursor"))
      activeCursor = data["activeCursor"];
    if (data.contains("cursorSize"))
      cursorSize = data["cursorSize"];
    if (data.contains("isCursorEnabled"))
      isCursorEnabled = data["isCursorEnabled"];
    if (data.contains("theme"))
      theme = data["theme"];
    if (data.contains("minimizeToTray"))
      minimizeToTray = data["minimizeToTray"];
  } catch (std::exception &e) {
    std::cerr << "AppConfig::Load failed: " << e.what() << '\n';
  }
}

void AppConfig::Save() {
  std::string path = GetConfigFilePath();
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

  try {
    json data;
    data["activeCursor"] = activeCursor;
    data["cursorSize"] = cursorSize;
    data["isCursorEnabled"] = isCursorEnabled;
    data["theme"] = theme;
    data["minimizeToTray"] = minimizeToTray;

    std::ofstream f(path);
    f << data.dump(4) << '\n';
  } catch (std::exception &e) {
    std::cerr << "AppConfig::Save failed: " << e.what() << '\n';
  }
}

} // namespace NeonSight
