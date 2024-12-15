#include "db_interface.h"

namespace zebes {

class Db : public DbInterface {
 public:
  // Sprite interfaces.
  void InsertSprite(const SpriteConfig& sprite_config) override;
  void DeleteSprite(int id) override;
  absl::StatusOr<SpriteConfig> GetSprite(int id);
};

}  // namespace zebes