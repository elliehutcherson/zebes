#include "resources/parallax_theme_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/parallax_themes";

}  // namespace

absl::StatusOr<std::unique_ptr<ParallaxThemeManager>> ParallaxThemeManager::Create(
    std::string root_path) {
  if (root_path.empty()) return absl::InvalidArgumentError("parallax theme asset root is empty");
  return std::unique_ptr<ParallaxThemeManager>(new ParallaxThemeManager(std::move(root_path)));
}

ParallaxThemeManager::ParallaxThemeManager(std::string root_path)
    : definitions_path_(absl::StrCat(root_path, "/", kDefinitionsPath)) {}

std::string ParallaxThemeManager::ThemePath(const std::string& id) const {
  return absl::StrCat(definitions_path_, "/", id, ".json");
}

absl::StatusOr<ParallaxTheme> ParallaxThemeManager::LoadThemeFile(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  try {
    nlohmann::json json;
    stream >> json;
    return ParallaxThemeFromJson(json);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid parallax theme JSON in ", path, ": ", error.what()));
  }
}

absl::Status ParallaxThemeManager::LoadAllThemes() {
  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create parallax theme directory: ", error.message()));
  }

  absl::flat_hash_map<std::string, std::unique_ptr<ParallaxTheme>> loaded;
  ResourceLoadFailures failures;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    absl::StatusOr<ParallaxTheme> theme = LoadThemeFile(entry.path().string());
    if (!theme.ok()) {
      failures.Add(entry.path().filename().string(), theme.status());
      continue;
    }
    if (entry.path().stem() != theme->id) {
      failures.Add(entry.path().filename().string(),
                   absl::InvalidArgumentError("filename does not match theme ID"));
      continue;
    }
    const std::string id = theme->id;
    if (!loaded.emplace(id, std::make_unique<ParallaxTheme>(std::move(*theme))).second) {
      failures.Add(entry.path().filename().string(),
                   absl::AlreadyExistsError(absl::StrCat("duplicate theme ID ", id)));
    }
  }
  if (!failures.empty()) return failures.ToStatus("parallax theme");
  themes_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> ParallaxThemeManager::CreateTheme(ParallaxTheme theme) {
  theme.id = GenerateGuid();
  RETURN_IF_ERROR(SaveTheme(theme));
  return theme.id;
}

absl::Status ParallaxThemeManager::SaveTheme(const ParallaxTheme& theme) {
  RETURN_IF_ERROR(ValidateParallaxTheme(theme));
  RETURN_IF_ERROR(WriteTextFileAtomically(ThemePath(theme.id), ParallaxThemeToJson(theme).dump(2)));

  if (auto found = themes_.find(theme.id); found != themes_.end()) {
    *found->second = theme;
  } else {
    themes_[theme.id] = std::make_unique<ParallaxTheme>(theme);
  }
  return absl::OkStatus();
}

absl::StatusOr<ParallaxTheme*> ParallaxThemeManager::GetTheme(const std::string& id) {
  const auto found = themes_.find(id);
  if (found == themes_.end()) {
    return absl::NotFoundError(absl::StrCat("parallax theme ", id, " is not loaded"));
  }
  return found->second.get();
}

std::vector<ParallaxTheme> ParallaxThemeManager::GetAllThemes() const {
  std::vector<ParallaxTheme> themes;
  themes.reserve(themes_.size());
  for (const auto& [id, theme] : themes_) themes.push_back(*theme);
  std::sort(themes.begin(), themes.end(),
            [](const ParallaxTheme& left, const ParallaxTheme& right) {
              if (left.name != right.name) return left.name < right.name;
              return left.id < right.id;
            });
  return themes;
}

absl::Status ParallaxThemeManager::DeleteTheme(const std::string& id) {
  const auto found = themes_.find(id);
  if (found == themes_.end()) return absl::NotFoundError("parallax theme is not loaded");

  std::error_code error;
  if (!std::filesystem::remove(ThemePath(id), error) || error) {
    return absl::InternalError(absl::StrCat("could not delete parallax theme: ", error.message()));
  }
  themes_.erase(found);
  return absl::OkStatus();
}

}  // namespace zebes
