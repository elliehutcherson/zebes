#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/level.h"
#include "resources/collider_manager.h"
#include "resources/sprite_manager.h"

namespace zebes {

class LevelManager {
 public:
  static absl::StatusOr<std::unique_ptr<LevelManager>> Create(SpriteManager* sprite_manager,
                                                              ColliderManager* collider_manager,
                                                              std::string root_path);

  virtual ~LevelManager() = default;

  /**
   * @brief Registers a level metadata and loads the level from disk.
   *
   * @param path_json The path to the json file.
   * @return A pointer to the loaded Level object, or an error status.
   */
  virtual absl::StatusOr<Level*> LoadLevel(const std::string& path_json);

  /**
   * @brief Scans the level directory and loads all found levels.
   */
  virtual absl::Status LoadAllLevels();

  /**
   * @brief Creates a new level, generating a GUID and JSON metadata.
   *
   * @param level The metadata for the new level. ID will be generated internally.
   * @return The ID of the created level.
   */
  virtual absl::StatusOr<std::string> CreateLevel(Level level);

  /**
   * @brief Updates an existing level with new metadata and saves it.
   */
  virtual absl::Status SaveLevel(const Level& level);

  /**
   * @brief Retrieves a loaded level by its ID.
   *
   * @param id The ID of the level to retrieve.
   * @return A reference to the Level object, or an error if not found/loaded.
   */
  virtual absl::StatusOr<Level*> GetLevel(const std::string& id);

  /**
   * @brief Deletes a level by its ID, removing the JSON file.
   */
  virtual absl::Status DeleteLevel(const std::string& id);

  /**
   * @brief Returns metadata for all loaded levels.
   */
  virtual std::vector<Level> GetAllLevels() const;

 protected:
  LevelManager(SpriteManager& sm, ColliderManager& cm, std::string root_path);

  std::string GetDefinitionsPath(const std::string relative_path);

  std::string root_path_;
  std::string definitions_path_;
  SpriteManager& sm_;
  ColliderManager& cm_;
  absl::flat_hash_map<std::string, std::unique_ptr<Level>> levels_;
};

}  // namespace zebes
