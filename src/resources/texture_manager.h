#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "SDL.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/sdl_wrapper.h"
#include "objects/texture.h"

namespace zebes {

class TextureManager {
 public:
  static absl::StatusOr<std::unique_ptr<TextureManager>> Create(SdlWrapper* sdl,
                                                                std::string root_path);

  ~TextureManager();

  /**
   * @brief Registers a texture metadata and loads the texture from disk.
   *
   * @param meta The metadata of the texture to load (id and path).
   * @return A pointer to the loaded Texture object, or an error status.
   */
  absl::StatusOr<Texture*> LoadTexture(const std::string& path_json);

  /**
   * @brief Scans the texture directory and loads all found textures.
   */
  absl::Status LoadAllTextures();

  /**
   * @brief Creates a new texture from an image file, generating a GUID and JSON metadata.
   *
   * @param Texture Contains path to the source image file and other meta data.
   * @return The ID of the created texture.
   */
  absl::StatusOr<std::string> CreateTexture(Texture texture);

  /**
   * @brief Retrieves a loaded texture by its ID.
   *
   * @param id The ID of the texture to retrieve.
   * @return A reference to the Texture object, or an error if not found/loaded.
   */
  absl::StatusOr<Texture*> GetTexture(const std::string& id);

  /**
   * @brief Deletes a texture by its ID, freeing resources and removing the JSON file.
   */
  absl::Status DeleteTexture(const std::string& id);

  /**
   * @brief Returns metadata for all loaded textures.
   */
  std::vector<Texture> GetAllTextures() const;

  /**
   * @brief Updates the metadata of an existing texture.
   */
  absl::Status UpdateTexture(const Texture& texture);

 private:
  explicit TextureManager(SdlWrapper* sdl, std::string root_path);

  absl::Status SaveTexture(const Texture& texture);

  std::string GetDefinitionsPath(const std::string& relative_path);
  std::string GetImagesPath(const std::string& relative_path);

  const std::string root_path_;
  const std::string definitions_path_;
  const std::string images_path_;
  SdlWrapper* sdl_;
  absl::flat_hash_map<std::string, std::unique_ptr<Texture>> textures_;
};

}  // namespace zebes
