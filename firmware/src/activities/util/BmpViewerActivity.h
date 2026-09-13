#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool canSetWallpaper() const;
  const char* confirmLabel() const;
  void doSetWallpaper();
  // True for formats handled by the streaming decoders (PNG/JPEG) rather than
  // the in-house BMP reader.
  bool isStreamDecodedImage() const;
  // Decodes once, streaming 2-bit pixels to imageCachePath so the grayscale
  // passes can re-render without paying for another decode.
  bool renderStreamDecodedImage();
  // Standalone Xteink page image (.xth 2-bit / .xtg 1-bit): unpacks the planes
  // straight into the pixel cache so it takes the same four-grey path.
  bool renderXthImage();
  // Blits the cached 2-bit pixels into the framebuffer in the current render
  // mode (BW, or one of the grayscale planes).
  // bwMidThreshold: in BW mode split the four levels at mid grey (0,1 -> ink)
  // instead of the writer's default of inking everything below white.
  bool blitCachedImage(bool bwMidThreshold = false);
  // Drives the four-grey plane sequence. False if the panel cannot do grayscale
  // or the cache is unusable, leaving the caller on the 1-bit path.
  bool displayCachedImageInFourGrey(const MappedInputManager::Labels& labels);
  void discardImageCache();

  std::string filePath;
  // Placement and unrotated size of the decoded image, kept for the cache blit.
  int imageX = 0;
  int imageY = 0;
  uint8_t imageOrientation = 1;
  bool imageCached = false;
  bool wallpaperSaved = false;  // relabels the confirm hint after a successful save

  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
