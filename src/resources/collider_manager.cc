#include "resources/collider_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/colliders";

}  // namespace

// JSON Serialization helpers locally
void ToJson(nlohmann::json& j, const Vec& vec) { j = nlohmann::json{{"x", vec.x}, {"y", vec.y}}; }

void FromJson(const nlohmann::json& j, Vec& vec) {
  j.at("x").get_to(vec.x);
  j.at("y").get_to(vec.y);
}

void ToJson(nlohmann::json& j, const Collider& collider) {
  nlohmann::json polygons_json = nlohmann::json::array();
  for (const auto& poly : collider.polygons) {
    nlohmann::json poly_json = nlohmann::json::array();
    for (const auto& point : poly) {
      nlohmann::json point_json;
      ToJson(point_json, point);
      poly_json.push_back(point_json);
    }
    polygons_json.push_back(poly_json);
  }

  j = nlohmann::json{
      {"id", collider.id},
      {"polygons", polygons_json},
  };
}

absl::StatusOr<Collider> GetColliderFromJson(const nlohmann::json& j) {
  Collider collider;
  try {
    j.at("id").get_to(collider.id);

    if (j.contains("polygons")) {
      for (const auto& poly_json : j["polygons"]) {
        Polygon poly;
        for (const auto& point_json : poly_json) {
          Vec point;
          FromJson(point_json, point);
          poly.push_back(point);
        }
        collider.polygons.push_back(poly);
      }
    }
  } catch (const nlohmann::json::exception& e) {
    return absl::InternalError(absl::StrCat("JSON parsing error for Collider: ", e.what()));
  }
  return collider;
}

absl::StatusOr<std::unique_ptr<ColliderManager>> ColliderManager::Create(std::string root_path) {
  return std::unique_ptr<ColliderManager>(new ColliderManager(root_path));
}

ColliderManager::ColliderManager(std::string root_path)
    : root_path_(root_path), definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)) {}

std::string ColliderManager::GetDefinitionsPath(const std::string relative_path) {
  return absl::StrCat(definitions_path_, "/", relative_path);
}

absl::StatusOr<Collider*> ColliderManager::LoadCollider(const std::string& path_json) {
  const std::string definitions_path = GetDefinitionsPath(path_json);
  if (!std::filesystem::exists(definitions_path)) {
    return absl::NotFoundError(absl::StrCat("File not found: ", definitions_path));
  }

  std::ifstream stream(definitions_path);
  nlohmann::json json;
  stream >> json;

  ASSIGN_OR_RETURN(Collider collider, GetColliderFromJson(json));

  // Check for duplicate ID
  if (colliders_.find(collider.id) != colliders_.end()) {
    return colliders_[collider.id].get();
  }

  // Create Collider object.
  std::string id = collider.id;
  colliders_[id] = std::make_unique<Collider>(std::move(collider));
  return colliders_[id].get();
}

absl::Status ColliderManager::LoadAllColliders() {
  if (!std::filesystem::exists(definitions_path_)) {
    return absl::NotFoundError(
        absl::StrCat("Collider root directory not found: ", definitions_path_));
  }

  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    auto status = LoadCollider(entry.path().filename().string());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to load collider from " << entry.path() << ": " << status.status();
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ColliderManager::CreateCollider(Collider collider) {
  // Generate ID if empty
  if (collider.id.empty()) {
    collider.id = GenerateGuid();
  }

  RETURN_IF_ERROR(SaveCollider(collider));

  // Load it
  std::string filename = absl::StrCat(collider.id, ".json");
  ASSIGN_OR_RETURN(Collider * loaded_collider, LoadCollider(filename));

  return loaded_collider->id;
}

absl::Status ColliderManager::SaveCollider(Collider collider) {
  if (collider.id.empty()) {
    return absl::InvalidArgumentError("Collider must have an ID to be saved.");
  }

  nlohmann::json json;
  ToJson(json, collider);

  std::string filename = absl::StrCat(collider.id, ".json");
  std::string definitions_path = GetDefinitionsPath(filename);

  // Ensure directory exists
  std::filesystem::create_directories(definitions_path_);

  std::ofstream file(definitions_path);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open file for writing: ", definitions_path));
  }
  file << json.dump(4);

  // Update in-memory cache
  std::string id = collider.id;
  colliders_[id] = std::make_unique<Collider>(std::move(collider));

  return absl::OkStatus();
}

absl::StatusOr<Collider*> ColliderManager::GetCollider(const std::string& id) {
  auto it = colliders_.find(id);
  if (it == colliders_.end()) {
    return absl::NotFoundError(absl::StrCat("Collider with id ", id, " not found in manager."));
  }
  return it->second.get();
}

absl::Status ColliderManager::DeleteCollider(const std::string& id) {
  auto it = colliders_.find(id);
  if (it == colliders_.end()) return absl::NotFoundError("Collider not found");

  // Remove JSON file
  std::string filename = absl::StrCat(id, ".json");
  std::filesystem::remove(GetDefinitionsPath(filename));

  colliders_.erase(it);
  return absl::OkStatus();
}

std::vector<Collider> ColliderManager::GetAllColliders() const {
  std::vector<Collider> colliders;
  colliders.reserve(colliders_.size());
  for (const auto& [id, collider] : colliders_) {
    colliders.push_back(*collider);
  }
  return colliders;
}

}  // namespace zebes
