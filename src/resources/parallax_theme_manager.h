#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/parallax_theme.h"

namespace zebes {

// Owns reusable parallax-theme definitions under
// definitions/parallax_themes/<id>.json.
class ParallaxThemeManager {
 public:
  static absl::StatusOr<std::unique_ptr<ParallaxThemeManager>> Create(std::string root_path);

  virtual ~ParallaxThemeManager() = default;
  virtual absl::Status LoadAllThemes();
  virtual absl::StatusOr<std::string> CreateTheme(ParallaxTheme theme);
  virtual absl::Status SaveTheme(const ParallaxTheme& theme);
  virtual absl::StatusOr<ParallaxTheme*> GetTheme(const std::string& id);
  virtual std::vector<ParallaxTheme> GetAllThemes() const;
  virtual absl::Status DeleteTheme(const std::string& id);

 protected:
  ParallaxThemeManager() = default;

 private:
  explicit ParallaxThemeManager(std::string root_path);

  static absl::StatusOr<ParallaxTheme> LoadThemeFile(const std::string& path);
  std::string ThemePath(const std::string& id) const;

  std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<ParallaxTheme>> themes_;
};

}  // namespace zebes
