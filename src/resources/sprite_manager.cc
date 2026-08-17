#include "resources/sprite_manager.h"

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "objects/sprite.h"
#include "resources/resource_utils.h"
#include "resources/texture_manager.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/sprites";

}  // namespace

void ToJson(nlohmann::json& j, const SpriteFrame& frame) {
  j = nlohmann::json{
      {"index", frame.index},         {"texture_x", frame.texture_x},
      {"texture_y", frame.texture_y}, {"texture_w", frame.texture_w},
      {"texture_h", frame.texture_h}, {"render_w", frame.render_w},
      {"render_h", frame.render_h},   {"frames_per_cycle", frame.frames_per_cycle},
      {"offset_x", frame.offset_x},   {"offset_y", frame.offset_y},
  };
}

absl::Status GetSpriteFrameFromJson(const nlohmann::json& j, SpriteFrame& frame) {
  try {
    j.at("index").get_to(frame.index);
    j.at("texture_x").get_to(frame.texture_x);
    j.at("texture_y").get_to(frame.texture_y);
    j.at("texture_w").get_to(frame.texture_w);
    j.at("texture_h").get_to(frame.texture_h);
    j.at("render_w").get_to(frame.render_w);
    j.at("render_h").get_to(frame.render_h);
    j.at("frames_per_cycle").get_to(frame.frames_per_cycle);
    j.at("offset_x").get_to(frame.offset_x);
    j.at("offset_y").get_to(frame.offset_y);
  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(absl::StrCat("JSON parsing error for SpriteFrame: ", e.what()));
  }
  return absl::OkStatus();
}

nlohmann::json ToJson(const Sprite& sprite) {
  nlohmann::json j = nlohmann::json{
      {"id", sprite.id},
      {"name", sprite.name},
      {"texture_id", sprite.texture_id},
  };

  std::vector<nlohmann::json> frames_json;
  for (const auto& frame : sprite.frames) {
    nlohmann::json f;
    ToJson(f, frame);
    frames_json.push_back(f);
  }
  j["frames"] = frames_json;

  return j;
}

absl::StatusOr<Sprite> GetSpriteFromJson(const nlohmann::json& j) {
  Sprite sprite;
  try {
    j.at("id").get_to(sprite.id);
    j.at("name").get_to(sprite.name);
    j.at("texture_id").get_to(sprite.texture_id);

    for (const auto& item : j.at("frames")) {
      SpriteFrame frame;
      RETURN_IF_ERROR(GetSpriteFrameFromJson(item, frame));
      sprite.frames.push_back(frame);
    }
  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(absl::StrCat("JSON parsing error for Sprite: ", e.what()));
  }
  return sprite;
}

absl::StatusOr<std::unique_ptr<SpriteManager>> SpriteManager::Create(TextureManager* tm,
                                                                     std::string root_path) {
  if (tm == nullptr) {
    return absl::InvalidArgumentError("TextureManager must not be null");
  }
  return std::unique_ptr<SpriteManager>(new SpriteManager(tm, root_path));
}

SpriteManager::SpriteManager(TextureManager* tm, std::string root_path)
    : tm_(tm),
      root_path_(root_path),
      definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)) {}

std::string SpriteManager::GetDefinitionsPath(const std::string relative_path) {
  return absl::StrCat(definitions_path_, "/", relative_path);
}

absl::StatusOr<Sprite*> SpriteManager::LoadSprite(const std::string& path_json) {
  const std::string definitions_path = GetDefinitionsPath(path_json);
  if (!std::filesystem::exists(definitions_path)) {
    return absl::NotFoundError(absl::StrCat("File not found: ", definitions_path));
  }

  std::ifstream stream(definitions_path);
  nlohmann::json json;
  stream >> json;

  ASSIGN_OR_RETURN(Sprite sprite, GetSpriteFromJson(json));

  // Check for duplicate ID
  if (sprites_.find(sprite.id) != sprites_.end()) {
    return sprites_[sprite.id].get();
  }

  // The texture must be loaded, but its handle is runtime state and stays with
  // the texture manager; Sprite names its texture by ID only.
  RETURN_IF_ERROR(tm_->GetTexture(sprite.texture_id).status());

  // Create Sprite object.
  std::string id = sprite.id;
  sprites_[id] = std::make_unique<Sprite>(std::move(sprite));
  return sprites_[id].get();
}

absl::Status SpriteManager::LoadAllSprites() {
  if (!std::filesystem::exists(definitions_path_)) {
    return absl::NotFoundError(
        absl::StrCat("Sprite root directory not found: ", definitions_path_));
  }

  ResourceLoadFailures failures;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadSprite(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load sprite from " << entry.path() << ": " << status.status();
      failures.Add(entry.path().filename().string(), status.status());
    }
  }
  return failures.ToStatus("sprite");
}

absl::StatusOr<std::string> SpriteManager::CreateSprite(Sprite sprite) {
  // The id should always be generated. Never allow an id to be passed in.
  sprite.id = GenerateGuid();

  RETURN_IF_ERROR(SaveSprite(sprite));

  // Load it (creates the Sprite object)
  // Load it (creates the Sprite object)
  std::string filename = absl::StrCat(sprite.name, "-", sprite.id, ".json");
  ASSIGN_OR_RETURN(Sprite * loaded_sprite, LoadSprite(filename));

  return loaded_sprite->id;
}

absl::Status SpriteManager::CreateSpriteWithId(Sprite sprite) {
  RETURN_IF_ERROR(PreflightSpriteWithId(sprite));
  return SaveSprite(std::move(sprite));
}

absl::Status SpriteManager::PreflightSpriteWithId(const Sprite& sprite) {
  if (!IsPathSafeResourceId(sprite.id) || !IsSafeResourceName(sprite.name)) {
    return absl::InvalidArgumentError("prepared sprite needs a path-safe ID and name");
  }
  if (sprites_.contains(sprite.id)) {
    return absl::AlreadyExistsError(absl::StrCat("Sprite with id ", sprite.id, " exists"));
  }
  const std::string filename = absl::StrCat(sprite.name, "-", sprite.id, ".json");
  if (std::filesystem::exists(GetDefinitionsPath(filename))) {
    return absl::AlreadyExistsError("prepared sprite definition already exists");
  }
  return absl::OkStatus();
}

absl::Status SpriteManager::SaveSprite(Sprite sprite) {
  if (sprite.id.empty()) {
    return absl::InvalidArgumentError("Sprite must have an ID to be saved.");
  }

  // The texture must be loaded, but its handle is runtime state and stays with
  // the texture manager; Sprite names its texture by ID only.
  RETURN_IF_ERROR(tm_->GetTexture(sprite.texture_id).status());

  nlohmann::json json = ToJson(sprite);

  std::string filename = absl::StrCat(sprite.name, "-", sprite.id, ".json");
  std::string definitions_path = GetDefinitionsPath(filename);

  RETURN_IF_ERROR(WriteTextFileAtomically(definitions_path, json.dump(4)));

  // Publish the new definition before removing the old name. A failed write
  // must leave the previously loaded sprite durable.
  auto it = sprites_.find(sprite.id);
  if (it != sprites_.end()) {
    RemoveOldFileIfExists(sprite.id, it->second->name, sprite.name, definitions_path_);
  }

  // Assigned through the existing allocation rather than replacing it: callers
  // hold Sprite* from GetSprite, and swapping the unique_ptr frees what they
  // point at. The pointer indirection exists so an address survives a save.
  std::string id = sprite.id;
  if (auto it = sprites_.find(id); it != sprites_.end()) {
    *it->second = std::move(sprite);
    return absl::OkStatus();
  }
  sprites_[id] = std::make_unique<Sprite>(std::move(sprite));

  return absl::OkStatus();
}

absl::StatusOr<Sprite*> SpriteManager::GetSprite(const std::string& id) {
  auto it = sprites_.find(id);
  if (it == sprites_.end()) {
    return absl::NotFoundError(absl::StrCat("Sprite with id ", id, " not found in manager."));
  }
  return it->second.get();
}

absl::Status SpriteManager::DeleteSprite(const std::string& id) {
  auto it = sprites_.find(id);
  if (it == sprites_.end()) return absl::NotFoundError("Sprite not found");

  std::string filename = absl::StrCat(it->second->name, "-", id, ".json");
  std::error_code error;
  std::filesystem::remove(GetDefinitionsPath(filename), error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not delete sprite definition: ", error.message()));
  }

  sprites_.erase(it);
  return absl::OkStatus();
}

std::vector<Sprite> SpriteManager::GetAllSprites() const {
  std::vector<Sprite> sprites;
  sprites.reserve(sprites_.size());
  for (const auto& [id, sprite] : sprites_) {
    sprites.push_back(*sprite);
  }
  return sprites;
}

}  // namespace zebes
