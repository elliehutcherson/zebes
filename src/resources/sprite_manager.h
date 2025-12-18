#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/sprite.h"
#include "resources/texture_manager.h"

namespace zebes {

class SpriteManager {
 public:
  static absl::StatusOr<std::unique_ptr<SpriteManager>> Create(TextureManager* texture_manager,
                                                               std::string root_path);

  virtual ~SpriteManager() = default;

  /**
   * @brief Registers a sprite metadata and loads the sprite from disk.
   *
   * @param path_json The path to the json file.
   * @return A pointer to the loaded Sprite object, or an error status.
   */
  virtual absl::StatusOr<Sprite*> LoadSprite(const std::string& path_json);

  /**
   * @brief Scans the sprite directory and loads all found sprites.
   */
  virtual absl::Status LoadAllSprites();

  /**
   * @brief Creates a new sprite, generating a GUID and JSON metadata.
   *
   * @param meta The metadata for the new sprite. ID will be generated.
   * @return The ID of the created sprite.
   */
  virtual absl::StatusOr<std::string> CreateSprite(Sprite sprite);

  /**
   * @brief Updates an existing sprite with new metadata and saves it.
   */
  virtual absl::Status SaveSprite(Sprite sprite);

  /**
   * @brief Retrieves a loaded sprite by its ID.
   *
   * @param id The ID of the sprite to retrieve.
   * @return A reference to the Sprite object, or an error if not found/loaded.
   */
  virtual absl::StatusOr<Sprite*> GetSprite(const std::string& id);

  /**
   * @brief Deletes a sprite by its ID, removing the JSON file.
   */
  virtual absl::Status DeleteSprite(const std::string& id);

  /**
   * @brief Checks if any sprite is using the given texture ID.
   *
   * @param texture_id The ID of the texture to check.
   * @return true if the texture is used by any sprite, false otherwise.
   */
  virtual bool IsTextureUsed(const std::string& texture_id) const;

  /**
   * @brief Returns metadata for all loaded sprites.
   */
  virtual std::vector<Sprite> GetAllSprites() const;

 protected:
  explicit SpriteManager(TextureManager* tm, std::string root_path);

  std::string GetDefinitionsPath(const std::string relative_path);

  const std::string root_path_;
  const std::string definitions_path_;
  TextureManager* tm_;
  absl::flat_hash_map<std::string, std::unique_ptr<Sprite>> sprites_;
};

}  // namespace zebes
