#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/tileset.h"

namespace zebes {

class TilesetManager {
 public:
  static absl::StatusOr<std::unique_ptr<TilesetManager>> Create(std::string root_path);

  virtual ~TilesetManager() = default;

  /**
   * @brief Loads a tileset from a JSON file on disk.
   *
   * @param path_json Filename relative to the tilesets definitions directory.
   * @return A pointer to the loaded Tileset object, or an error status.
   */
  virtual absl::StatusOr<Tileset*> LoadTileset(const std::string& path_json);

  /**
   * @brief Scans the tilesets directory and loads all found tilesets.
   */
  virtual absl::Status LoadAllTilesets();

  /**
   * @brief Creates a new tileset, generating a GUID and writing the JSON file.
   *
   * @param tileset The tileset to create. The id field is always overwritten.
   * @return The ID of the created tileset.
   */
  virtual absl::StatusOr<std::string> CreateTileset(Tileset tileset);

  /**
   * @brief Validates and persists an existing tileset to disk.
   *
   * Handles renaming: if the name has changed, the old JSON file is removed.
   */
  virtual absl::Status SaveTileset(const Tileset& tileset);

  /**
   * @brief Retrieves a loaded tileset by its ID.
   *
   * @param id The UUID of the tileset.
   * @return A pointer to the Tileset object, or NotFoundError.
   */
  virtual absl::StatusOr<Tileset*> GetTileset(const std::string& id);

  /**
   * @brief Deletes a tileset by ID, removing the JSON file from disk.
   */
  virtual absl::Status DeleteTileset(const std::string& id);

  /**
   * @brief Returns copies of all loaded tilesets.
   */
  virtual std::vector<Tileset> GetAllTilesets() const;

  /**
   * @brief Returns true if any loaded tileset references the given texture ID.
   */
  virtual bool IsTextureUsed(const std::string& texture_id) const;

 protected:
  explicit TilesetManager(std::string root_path);

  std::string GetDefinitionsPath(const std::string& relative_path);

  const std::string root_path_;
  const std::string definitions_path_;
  absl::flat_hash_map<std::string, std::unique_ptr<Tileset>> tilesets_;
};

}  // namespace zebes
