#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/blueprint.h"

namespace zebes {

class BlueprintManager {
 public:
  static absl::StatusOr<std::unique_ptr<BlueprintManager>> Create(std::string root_path);

  virtual ~BlueprintManager() = default;

  /**
   * @brief Registers a blueprint metadata and loads the blueprint from disk.
   *
   * @param path_json The path to the json file.
   * @return A pointer to the loaded Blueprint object, or an error status.
   */
  virtual absl::StatusOr<Blueprint*> LoadBlueprint(const std::string& path_json);

  /**
   * @brief Scans the blueprint directory and loads all found blueprints.
   */
  virtual absl::Status LoadAllBlueprints();

  /**
   * @brief Creates a new blueprint, generating a GUID and JSON metadata.
   *
   * @param blueprint The metadata for the new blueprint. ID will be generated if empty.
   * @return The ID of the created blueprint.
   */
  virtual absl::StatusOr<std::string> CreateBlueprint(Blueprint blueprint);

  /**
   * @brief Updates an existing blueprint with new metadata and saves it.
   */
  virtual absl::Status SaveBlueprint(Blueprint blueprint);

  /**
   * @brief Retrieves a loaded blueprint by its ID.
   *
   * @param id The ID of the blueprint to retrieve.
   * @return A reference to the Blueprint object, or an error if not found/loaded.
   */
  virtual absl::StatusOr<Blueprint*> GetBlueprint(const std::string& id);

  /**
   * @brief Deletes a blueprint by its ID, removing the JSON file.
   */
  virtual absl::Status DeleteBlueprint(const std::string& id);

  /**
   * @brief Returns metadata for all loaded blueprints.
   */
  virtual std::vector<Blueprint> GetAllBlueprints() const;

  /**
   * @brief Checks if a sprite is used by any blueprint.
   */
  virtual bool IsSpriteUsed(const std::string& sprite_id) const;

  /**
   * @brief Checks if a collider is used by any blueprint.
   */
  virtual bool IsColliderUsed(const std::string& collider_id) const;

 protected:
  explicit BlueprintManager(std::string root_path);

  std::string GetDefinitionsPath(const std::string relative_path);

  const std::string root_path_;
  const std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<Blueprint>> blueprints_;
};

}  // namespace zebes
