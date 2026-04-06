#pragma once

#include <string>

namespace NeonSight {

class AppConfig {
public:
  static AppConfig &Get() {
    static AppConfig instance;
    return instance;
  }

  void Load();
  void Save();

  std::string activeCursor; // filename of the selected cursor (e.g. "<uuid>.svg")
  int cursorSize{48};
  bool isCursorEnabled{true};
  std::string theme{"storm"}; // "storm" or "day"
  bool minimizeToTray{true};

private:
  AppConfig() = default;
  ~AppConfig() = default;

  std::string GetConfigFilePath() const;
};

} // namespace NeonSight
