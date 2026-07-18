#include "resources/texture_manager.h"

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/common.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "objects/texture.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/textures";
constexpr char kImagesPath[] = "textures";

}  // namespace

absl::StatusOr<std::unique_ptr<TextureManager>> TextureManager::Create(
    TextureResourceStore* resources,
                                                                       std::string root_path) {
  if (resources == nullptr) {
    return absl::InvalidArgumentError("TextureResourceStore must not be null");
  }
  return std::unique_ptr<TextureManager>(new TextureManager(resources, root_path));
}

TextureManager::TextureManager(TextureResourceStore* resources, std::string root_path)
    : resources_(resources),
      root_path_(root_path),
      definitions_path_(absl::StrCat(root_path, "/", kDefinitionsPath)),
      images_path_(absl::StrCat(root_path, "/", kImagesPath)) {}

TextureManager::~TextureManager() {
  for (auto& [id, texture] : textures_) {
    if (texture != nullptr && texture->texture_handle) {
      resources_->Unload(texture->texture_handle).IgnoreError();
    }
  }
}

std::string TextureManager::GetDefinitionsPath(const std::string& relative_path) {
  return absl::StrCat(definitions_path_, "/", relative_path);
}

std::string TextureManager::GetImagesPath(const std::string& relative_path) {
  if (relative_path.rfind("textures/", 0) == 0) {
    return absl::StrCat(root_path_, "/", relative_path);
  }
  return absl::StrCat(images_path_, "/", relative_path);
}

absl::Status TextureManager::LoadAllTextures() {
  if (!std::filesystem::exists(definitions_path_)) {
    return absl::NotFoundError(
        absl::StrCat("Texture root directory not found: ", definitions_path_));
  }

  for (const auto& entry : std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadTexture(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load texture from " << entry.path() << ": " << status.status();
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<Texture*> TextureManager::LoadTexture(const std::string& path_json) {
  const std::string definitions_path = GetDefinitionsPath(path_json);
  if (!std::filesystem::exists(definitions_path)) {
    return absl::NotFoundError(absl::StrCat("File not found: ", definitions_path));
  }

  std::ifstream stream(definitions_path);
  nlohmann::json json;
  stream >> json;

  if (!json.contains("id") || !json.contains("path")) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid texture JSON: ", path_json, ". Missing 'id' or 'path'."));
  }

  std::string id = json["id"];
  std::string path = json["path"];
  std::string name = json.value("name", std::filesystem::path(path).stem().string());

  if (name.length() > kMaxTextureNameLength) {
    return absl::InvalidArgumentError(
        absl::StrCat("Texture name too long: ", name, ". Max length is ", kMaxTextureNameLength));
  }

  // Check for duplicate ID
  if (textures_.find(id) != textures_.end()) {
    // If already loaded, just return it.
    return textures_[id].get();
  }

  ASSIGN_OR_RETURN(TextureHandle texture_handle, resources_->Load(GetImagesPath(path)));

  std::unique_ptr<Texture> texture = std::make_unique<Texture>();
  texture->id = id;
  texture->name = name;
  texture->path = path;
  texture->texture_handle = texture_handle;

  textures_[id] = std::move(texture);
  return textures_[id].get();
}

absl::StatusOr<std::string> TextureManager::CreateTexture(Texture texture) {
  // Generate GUID
  std::string id = GenerateGuid();
  std::string source_path = texture.path;

  if (!std::filesystem::exists(source_path)) {
    return absl::NotFoundError(absl::StrCat("Source image file not found: ", source_path));
  }

  std::filesystem::path src(source_path);
  std::string filename = src.filename().string();
  std::string destination_path = absl::StrCat(images_path_, "/", filename);
  std::filesystem::path dest(destination_path);

  // Copy file if it's not already there (or if source != dest)
  bool is_same_file = false;
  std::error_code ec;
  if (std::filesystem::exists(dest, ec)) {
    if (std::filesystem::equivalent(src, dest, ec)) {
      is_same_file = true;
    }
  }

  if (!is_same_file) {
    try {
      std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing);
    } catch (std::filesystem::filesystem_error& e) {
      return absl::InternalError(absl::StrCat("Failed to copy image file: ", e.what()));
    }
  }

  texture.id = id;
  // Update path to be relative for storage
  texture.path = absl::StrCat("textures/", filename);

  if (texture.name.empty()) {
    texture.name = src.stem().string();
  }
  if (texture.name.length() > kMaxTextureNameLength) {
    return absl::InvalidArgumentError(absl::StrCat("Texture name too long: ", texture.name,
                                                   ". Max length is ", kMaxTextureNameLength));
  }

  ASSIGN_OR_RETURN(TextureHandle texture_handle, resources_->Load(destination_path));
  texture.texture_handle = texture_handle;

  // Save metadata to JSON
  absl::Status save_status = SaveTexture(texture);
  if (!save_status.ok()) {
    resources_->Unload(texture_handle).IgnoreError();
    return save_status;
  }

  // Store in map
  textures_[id] = std::make_unique<Texture>(texture);

  return id;
}

absl::Status TextureManager::SaveTexture(const Texture& texture) {
  if (texture.id.empty()) {
    return absl::InvalidArgumentError("Cannot save texture with empty id.");
  }
  nlohmann::json json;
  json["id"] = texture.id;
  json["name"] = texture.name;
  json["path"] = texture.path;

  std::string filename = absl::StrCat(texture.name, "-", texture.id, ".json");
  std::string absolute_path = GetDefinitionsPath(filename);

  std::ofstream file(absolute_path);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open file for writing: ", absolute_path));
  }
  file << json.dump(4);
  return absl::OkStatus();
}

absl::Status TextureManager::UpdateTexture(const Texture& texture) {
  auto it = textures_.find(texture.id);
  if (it == textures_.end()) {
    return absl::NotFoundError(absl::StrCat("Texture with id ", texture.id, " not found."));
  }

  // Update in-memory
  if (texture.name.length() > kMaxTextureNameLength) {
    return absl::InvalidArgumentError(absl::StrCat("Texture name too long: ", texture.name,
                                                   ". Max length is ", kMaxTextureNameLength));
  }

  std::string old_name = it->second->name;
  it->second->name = texture.name;
  // Note: path and id are generally immutable for an existing texture reference in this context,
  // or at least we don't want to change the file path here without reloading.
  // For now, only name is mutable via this method.

  // Rename file if name changed
  if (old_name != texture.name) {
    std::string old_filename = absl::StrCat(old_name, "-", texture.id, ".json");
    std::string new_filename = absl::StrCat(texture.name, "-", texture.id, ".json");
    std::filesystem::rename(GetDefinitionsPath(old_filename), GetDefinitionsPath(new_filename));
  }

  // Save to disk
  return SaveTexture(*it->second);
}

absl::StatusOr<Texture*> TextureManager::GetTexture(const std::string& id) {
  auto it = textures_.find(id);
  if (it == textures_.end()) {
    return absl::NotFoundError(absl::StrCat("Texture with id ", id, " not found in manager."));
  }
  return textures_[id].get();
}

absl::Status TextureManager::DeleteTexture(const std::string& id) {
  auto it = textures_.find(id);
  if (it == textures_.end()) return absl::NotFoundError("Texture not found");

  // Release the runtime renderer resource.
  if (it->second->texture_handle) {
    RETURN_IF_ERROR(resources_->Unload(it->second->texture_handle));
  }

  // Remove JSON file
  std::string filename = absl::StrCat(it->second->name, "-", id, ".json");
  std::string absolute_path = GetDefinitionsPath(filename);
  std::filesystem::remove(absolute_path);

  textures_.erase(it);
  return absl::OkStatus();
}

std::vector<Texture> TextureManager::GetAllTextures() const {
  std::vector<Texture> textures;
  textures.reserve(textures_.size());
  for (const auto& [id, texture] : textures_) {
    textures.push_back(*texture);
  }

  return textures;
}

}  // namespace zebes
