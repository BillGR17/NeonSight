#pragma once
#include <string>

namespace NeonSight {

struct CursorMeta {
  float hotspotX{0.5f};
  float hotspotY{0.5f};

  bool Load(const std::string &jsonPath);
  void Save(const std::string &jsonPath) const;
};

} // namespace NeonSight
