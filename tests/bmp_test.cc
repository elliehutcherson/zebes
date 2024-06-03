#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "engine/bitmap.h"

int main() {
  // Create a 5x5 bitmap
  Bitmap bitmap(5, 5);

  // Set some values in the bitmap
  absl::Status result = bitmap.Set(0, 0, 255, 0, 0); // Red
  if (result.ok())
    result = bitmap.Set(2, 2, 0, 255, 0); // Green
  if (result.ok())
    result = bitmap.Set(4, 4, 0, 0, 255); // Blue
  if (!result.ok())
    LOG(FATAL) << "Failed to set bitmap values: " << result.message();

  // Save the bitmap to a BMP file
  if (!bitmap.SaveToBmp("/tmp/bitmap.bmp").ok()) {
    LOG(FATAL) << "Failed to save bitmap!";
  }
  LOG(INFO) << "Bitmap saved successfully!";

  absl::StatusOr<Bitmap> loaded_bitmap = Bitmap::LoadFromBmp("/tmp/bitmap.bmp");
  if (!loaded_bitmap.ok()) {
    LOG(FATAL) << "Failed to load bitmap: " << loaded_bitmap.status().message();
  }
  loaded_bitmap->Print();

  return 0;
}
