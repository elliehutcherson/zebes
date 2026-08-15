#pragma once

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "objects/texture.h"
#include "resources/texture_resource_store.h"

namespace zebes {

// Resolves a texture definition's declared path to a file under the asset root.
//
// Two conventions are in circulation: older definitions store a bare filename
// relative to the images directory, newer ones store a root-relative path that
// already begins with "textures/". Both are accepted so existing definitions
// keep loading. Exposed so callers that need to check artwork exists resolve it
// the same way loading does, instead of reimplementing the rule.
std::string ResolveTextureImagePath(const std::string& root_path, const std::string& declared_path);

class TextureManager {
 public:
  static absl::StatusOr<std::unique_ptr<TextureManager>> Create(TextureResourceStore* resources,
                                                                std::string root_path);

  virtual ~TextureManager();

  /**
   * @brief Registers a texture metadata and loads the texture from disk.
   *
   * @param meta The metadata of the texture to load (id and path).
   * @return A pointer to the loaded Texture object, or an error status.
   */
  virtual absl::StatusOr<Texture*> LoadTexture(const std::string& path_json);

  /**
   * @brief Scans the texture directory and loads all found textures.
   */
  virtual absl::Status LoadAllTextures();

  /**
   * @brief Creates a new texture from an image file, generating a GUID and JSON metadata.
   *
   * @param Texture Contains path to the source image file and other meta data.
   * @return The ID of the created texture.
   */
  virtual absl::StatusOr<std::string> CreateTexture(Texture texture);

  /**
   * @brief Writes generated artwork into the images directory and registers it.
   *
   * Artwork the editor produces itself -- a procedurally generated terrain
   * atlas, for instance -- has no source file to copy from, so it becomes a
   * real PNG here before a definition can point at it. Refuses to overwrite
   * existing artwork, since a name collision means two tilesets would end up
   * sharing one atlas.
   *
   * @param name Base name for both the PNG and the definition.
   * @return The ID of the created texture.
   */
  virtual absl::StatusOr<std::string> CreateTextureFromPixels(const std::string& name, int width,
                                                              int height,
                                                              absl::Span<const uint8_t> pixels);

  // Atomically replaces generated artwork without changing the texture's ID,
  // definition path, or references. The replacement is decoded before the
  // durable file is committed, so a bad image cannot evict the live texture.
  virtual absl::Status ReplaceTexturePixels(const std::string& id, int width, int height,
                                            absl::Span<const uint8_t> pixels);

  // Shows new artwork without making it durable.
  //
  // Derived terrain grows its atlas as a level is painted, and the artwork must
  // be on screen the moment a cell references it -- a tile the GPU has not seen
  // renders as a hole. Durability is a separate decision with a separate
  // trigger: unsaved paint stays unsaved, its artwork included, so abandoning
  // an edit leaves no tiles behind on disk that nothing references.
  //
  // The file keeps whatever it held. Follow with ReplaceTexturePixels to make
  // the same pixels durable.
  virtual absl::Status ShowTexturePixels(const std::string& id, int width, int height,
                                         absl::Span<const uint8_t> pixels);

  /**
   * @brief Retrieves a loaded texture by its ID.
   *
   * @param id The ID of the texture to retrieve.
   * @return A reference to the Texture object, or an error if not found/loaded.
   */
  virtual absl::StatusOr<Texture*> GetTexture(const std::string& id);

  /**
   * @brief Deletes a texture by its ID, freeing resources and removing the JSON file.
   */
  virtual absl::Status DeleteTexture(const std::string& id);

  /**
   * @brief Returns metadata for all loaded textures.
   */
  virtual std::vector<Texture> GetAllTextures() const;

  /**
   * @brief Updates the metadata of an existing texture.
   */
  virtual absl::Status UpdateTexture(const Texture& texture);

  /**
   * @brief Returns the loaded GPU handle for a texture.
   *
   * The handle is runtime state and deliberately lives here rather than on the
   * Texture definition. An unloaded texture yields an invalid handle rather
   * than an error, since that is an ordinary authoring state.
   */
  virtual absl::StatusOr<TextureHandle> GetTextureHandle(const std::string& id) const;

 protected:
  friend class TextureManagerTestPeer;

  explicit TextureManager(TextureResourceStore* resources, std::string root_path);

  absl::Status SaveTexture(const Texture& texture);

  std::string GetDefinitionsPath(const std::string& relative_path);
  std::string GetImagesPath(const std::string& relative_path);

  const std::string root_path_;
  const std::string definitions_path_;
  const std::string images_path_;
  TextureResourceStore* resources_;
  absl::flat_hash_map<std::string, std::unique_ptr<Texture>> textures_;

  // Runtime GPU handles, keyed by texture ID. Kept beside the definitions
  // rather than inside them so Texture stays backend-independent.
  absl::flat_hash_map<std::string, TextureHandle> handles_;
};

}  // namespace zebes
