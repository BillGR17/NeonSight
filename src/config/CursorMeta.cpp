#include "CursorMeta.hpp"
#include "../utils/json.hpp"

#include <fstream>
#include <iostream>

namespace NeonSight {

bool CursorMeta::Load(const std::string &jsonPath) {
  std::ifstream f(jsonPath);
  if (!f.is_open())
    return false;
  try {
    auto data = nlohmann::json::parse(f);
    if (data.contains("hotspotX") && data.contains("hotspotY")) {
      hotspotX = data["hotspotX"];
      hotspotY = data["hotspotY"];
      return true;
    }
    return false;
  } catch (...) {
    return false;
  }
}

void CursorMeta::Save(const std::string &jsonPath) const {
  nlohmann::json data;
  data["hotspotX"] = hotspotX;
  data["hotspotY"] = hotspotY;

  std::ofstream f(jsonPath);
  if (f.is_open()) {
    f << data.dump(4) << '\n';
  } else {
    std::cerr << "CursorMeta: could not write " << jsonPath << '\n';
  }
}

} // namespace NeonSight
