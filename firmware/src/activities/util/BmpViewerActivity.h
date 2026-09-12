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
  // True for formats handled by the streaming decoders (PNG/JPEG) rather than
  // the in-house BMP reader.
  bool isStreamDecodedImage() const;
  bool renderStreamDecodedImage();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
