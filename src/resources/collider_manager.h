#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/collider.h"

namespace zebes {

class ColliderManager {
 public:
  static absl::StatusOr<std::unique_ptr<ColliderManager>> Create(std::string root_path);

  ~ColliderManager() = default;

  /**
   * @brief Registers a collider metadata and loads the collider from disk.
   *
   * @param path_json The path to the json file.
   * @return A pointer to the loaded Collider object, or an error status.
   */
  absl::StatusOr<Collider*> LoadCollider(const std::string& path_json);

  /**
   * @brief Scans the collider directory and loads all found colliders.
   */
  absl::Status LoadAllColliders();

  /**
   * @brief Creates a new collider, generating a GUID and JSON metadata.
   *
   * @param collider The metadata for the new collider. ID will be generated.
   * @return The ID of the created collider.
   */
  absl::StatusOr<std::string> CreateCollider(Collider collider);

  /**
   * @brief Updates an existing collider with new metadata and saves it.
   */
  absl::Status SaveCollider(Collider collider);

  /**
   * @brief Retrieves a loaded collider by its ID.
   *
   * @param id The ID of the collider to retrieve.
   * @return A reference to the Collider object, or an error if not found/loaded.
   */
  absl::StatusOr<Collider*> GetCollider(const std::string& id);

  /**
   * @brief Deletes a collider by its ID, removing the JSON file.
   */
  absl::Status DeleteCollider(const std::string& id);

  /**
   * @brief Returns metadata for all loaded colliders.
   */
  std::vector<Collider> GetAllColliders() const;

 private:
  explicit ColliderManager(std::string root_path);

  std::string GetDefinitionsPath(const std::string relative_path);

  const std::string root_path_;
  const std::string definitions_path_;  // Path to definitions/colliders
  absl::flat_hash_map<std::string, std::unique_ptr<Collider>> colliders_;
};

}  // namespace zebes
