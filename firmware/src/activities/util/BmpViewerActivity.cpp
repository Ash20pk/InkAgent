#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/DirectPixelWriter.h>
#include <Epub/converters/DitherUtils.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc/XthImage.h>

#include <algorithm>
#include <cstring>

#include "InkAgentSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char CUSTOM_SLEEP_ROOT_BMP[] = "/sleep.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
// Lock-screen wallpaper selection. Same file the web file manager's "Set as
// wallpaper" writes and SleepActivity::renderLockSleepScreen reads.
constexpr char LOCK_WALLPAPER_CONF[] = "/.inkagent/wallpaper.txt";
constexpr size_t COPY_BUFFER_SIZE = 2048;
// Scratch pixel cache for the viewer. Decoding a photo costs seconds, and the
// four-grey sequence needs the same pixels three times (two planes plus the BW
// framebuffer the popups draw over), so the decode result is parked here rather
// than repeated.
constexpr char IMAGE_CACHE_PATH[] = "/.inkagent/.imgview.pxc";
constexpr int PXC_HEADER_BYTES = 4;
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (FsHelpers::hasBmpExtension(fname) || FsHelpers::hasPngExtension(fname) ||
            FsHelpers::hasJpgExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  const auto image = std::find(siblingImages.begin(), siblingImages.end(), fileName);
  if (image != siblingImages.end()) {
    currentImageIndex = static_cast<int>(image - siblingImages.begin());
  }
}

bool BmpViewerActivity::canSetSleepCover() const {
  return FsHelpers::hasBmpExtension(filePath) ||
         (SETTINGS.sleepScreen == InkAgentSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM &&
          FsHelpers::hasPngExtension(filePath));
}

// Streamed images (PNG/JPEG/XTH) become the lock-screen wallpaper rather than a
// copied sleep cover: the lock screen renders them in place from their path.
bool BmpViewerActivity::canSetWallpaper() const { return isStreamDecodedImage(); }

const char* BmpViewerActivity::confirmLabel() const {
  if (canSetWallpaper()) return wallpaperSaved ? tr(STR_DONE) : tr(STR_SET_WALLPAPER);
  if (canSetSleepCover()) return tr(STR_SET_SLEEP_COVER);
  return "";
}

void BmpViewerActivity::doSetWallpaper() {
  // Saving is two small SD writes, so there is nothing to wait for and no
  // loading popup. Popups are expensive here: each one is a full-screen refresh,
  // and restoring the picture afterwards meant re-decoding it and running the
  // whole four-grey sequence again - three flashes for an action that changes
  // nothing on screen. Instead the image is repainted once from the cache with
  // the hint relabelled, which is the single refresh the feedback costs.
  const bool ok = Storage.writeFile(LOCK_WALLPAPER_CONF, String(filePath.c_str()));
  if (ok) {
    // Selecting a wallpaper from the device also switches the sleep screen to
    // the mode that shows it; otherwise the choice would be invisible.
    SETTINGS.sleepScreen = InkAgentSettings::SLEEP_SCREEN_MODE::LOCK;
    SETTINGS.saveToFile();
    wallpaperSaved = true;
  }

  const bool hasPrevious = siblingImages.size() > 1 && currentImageIndex > 0;
  const bool hasNext = siblingImages.size() > 1 && currentImageIndex != -1 &&
                       currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ok ? tr(STR_DONE) : tr(STR_FAILED_LOWER),
                                            hasPrevious ? "<" : "", hasNext ? ">" : "");
  if (!displayCachedImageInFourGrey(labels)) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    if (imageCached) blitCachedImage(/*bwMidThreshold=*/true);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

bool BmpViewerActivity::isStreamDecodedImage() const {
  return FsHelpers::hasPngExtension(filePath) || FsHelpers::hasJpgExtension(filePath) ||
         FsHelpers::hasXthExtension(filePath);
}

// Standalone Xteink page image: unpack the planes into the cache's row-major
// 2bpp layout so it takes the same four-grey path as everything else. The page
// is pre-rendered for a specific panel, so it is shown 1:1 and centred; a page
// larger than the screen is rejected rather than resampled.
bool BmpViewerActivity::renderXthImage() {
  xtc::XthImage image;
  if (!image.load(filePath)) return false;
  const uint16_t srcWidth = image.width();
  const uint16_t srcHeight = image.height();
  if (srcWidth > renderer.getScreenWidth() || srcHeight > renderer.getScreenHeight()) {
    LOG_ERR("XTH", "Page %ux%u is larger than the %dx%d screen", srcWidth, srcHeight, renderer.getScreenWidth(),
            renderer.getScreenHeight());
    return false;
  }

  // A page rendered for the full screen (pre-viewerHeight uploads, stock
  // "Pushed Images") is a few rows taller than the area above the hint bar.
  // Resampling would destroy its dithering, so centre-crop instead: pixels stay
  // 1:1 and only the outermost rows/columns are trimmed.
  const Rect area = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const uint16_t width = std::min<uint16_t>(srcWidth, static_cast<uint16_t>(area.width));
  const uint16_t height = std::min<uint16_t>(srcHeight, static_cast<uint16_t>(area.height));
  const uint16_t cropX = static_cast<uint16_t>((srcWidth - width) / 2);
  const uint16_t cropY = static_cast<uint16_t>((srcHeight - height) / 2);
  if (cropX || cropY)
    LOG_DBG("XTH", "Cropping %ux%u page by %u,%u to fit above the hints", srcWidth, srcHeight, cropX, cropY);

  discardImageCache();
  HalFile cache;
  if (!Storage.openFileForWrite("XTH", IMAGE_CACHE_PATH, cache)) return false;
  if (cache.write(reinterpret_cast<const uint8_t*>(&width), 2) != 2 ||
      cache.write(reinterpret_cast<const uint8_t*>(&height), 2) != 2) {
    return false;
  }
  const int bytesPerRow = (width + 3) / 4;
  auto row = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(bytesPerRow));
  if (!row) {
    LOG_ERR("XTH", "OOM: %d bytes for cache row", bytesPerRow);
    return false;
  }
  for (uint16_t y = 0; y < height; y++) {
    memset(row.get(), 0, static_cast<size_t>(bytesPerRow));
    for (uint16_t x = 0; x < width; x++) {
      row[x >> 2] |= static_cast<uint8_t>(image.level(cropX + x, cropY + y) << (6 - (x & 3) * 2));
    }
    if (cache.write(row.get(), static_cast<size_t>(bytesPerRow)) != static_cast<size_t>(bytesPerRow)) return false;
  }
  cache.close();

  imageX = area.x + (area.width - width) / 2;
  imageY = area.y + (area.height - height) / 2;
  imageOrientation = 1;
  imageCached = true;
  return true;
}

bool BmpViewerActivity::renderStreamDecodedImage() {
  if (FsHelpers::hasXthExtension(filePath)) return renderXthImage();
  const bool isJpeg = FsHelpers::hasJpgExtension(filePath);

  ImageDimensions dimensions;
  const bool gotDimensions = isJpeg ? JpegToFramebufferConverter::getDimensionsStatic(filePath, dimensions)
                                    : PngToFramebufferConverter::getDimensionsStatic(filePath, dimensions);
  if (!gotDimensions) return false;
  if (dimensions.width <= 0 || dimensions.height <= 0) return false;

  // Fit inside the area the button hints leave free, not the whole screen:
  // otherwise the hint bar paints over the bottom of the image.
  const Rect area = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const float scale =
      std::min(static_cast<float>(area.width) / dimensions.width, static_cast<float>(area.height) / dimensions.height);
  const int width = std::min(area.width, static_cast<int>(dimensions.width * std::min(scale, 1.0f)));
  const int height = std::min(area.height, static_cast<int>(dimensions.height * std::min(scale, 1.0f)));
  RenderConfig config{area.x + (area.width - width) / 2, area.y + (area.height - height) / 2, width, height};

  // The decoders emit 2-bit pixels; parking them lets the grayscale passes
  // re-render without decoding again. A cache failure is not fatal - the decode
  // still paints the framebuffer, it just cannot be replayed in four grey.
  discardImageCache();
  config.cachePath = IMAGE_CACHE_PATH;
  imageX = config.x;
  imageY = config.y;
  imageOrientation = isJpeg ? JpegToFramebufferConverter::readOrientation(filePath) : 1;

  // Both decoders stream (JPEG in MCU bands at a 1/2..1/8 coarse scale, PNG
  // scanline-by-scanline), so a multi-megabyte photo costs decode time, not RAM.
  bool decoded;
  if (isJpeg) {
    JpegToFramebufferConverter converter;
    decoded = converter.decodeToFramebuffer(filePath, renderer, config);
  } else {
    PngToFramebufferConverter converter;
    decoded = converter.decodeToFramebuffer(filePath, renderer, config);
  }

  imageCached = decoded && Storage.exists(IMAGE_CACHE_PATH);
  return decoded;
}

void BmpViewerActivity::discardImageCache() {
  imageCached = false;
  if (Storage.exists(IMAGE_CACHE_PATH)) Storage.remove(IMAGE_CACHE_PATH);
}

// Sum of the framebuffer bytes: a cheap fingerprint to compare what each
// grayscale pass actually staged, without dumping 48KB over serial.
static uint32_t frameSum(const GfxRenderer& r) {
  const uint8_t* fb = r.getFrameBuffer();
  const size_t n = r.getBufferSize();
  uint32_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += fb[i];
  return sum;
}

bool BmpViewerActivity::blitCachedImage(bool bwMidThreshold) {
  HalFile cacheFile;
  if (!Storage.openFileForRead("BMP", IMAGE_CACHE_PATH, cacheFile)) return false;

  uint16_t cachedWidth = 0;
  uint16_t cachedHeight = 0;
  if (cacheFile.read(reinterpret_cast<uint8_t*>(&cachedWidth), 2) != 2 ||
      cacheFile.read(reinterpret_cast<uint8_t*>(&cachedHeight), 2) != 2) {
    return false;
  }
  if (cachedWidth == 0 || cachedHeight == 0) return false;

  const int bytesPerRow = (cachedWidth + 3) / 4;  // 2 bits per pixel, MSB first
  auto rowBuffer = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(bytesPerRow));
  if (!rowBuffer) {
    LOG_ERR("BMP", "OOM: %d bytes for cache row", bytesPerRow);
    return false;
  }

  DirectPixelWriter pw;
  pw.init(renderer);
  // Cached pixels are unrotated image-space, so the EXIF rotation is applied
  // here rather than having been baked into the cache.
  pw.composeImageRotation(imageOrientation, imageX, imageY, cachedWidth, cachedHeight);

  for (int row = 0; row < cachedHeight; row++) {
    if (cacheFile.read(rowBuffer.get(), static_cast<size_t>(bytesPerRow)) != bytesPerRow) {
      LOG_ERR("BMP", "Cache read error at row %d", row);
      return false;
    }
    pw.beginRow(imageY + row);
    int colStart, colEnd;
    pw.bandColRange(imageX, cachedWidth, colStart, colEnd);
    for (int col = colStart; col < colEnd; col++) {
      const uint8_t packed = rowBuffer[col >> 2];
      uint8_t pixelValue = (packed >> (6 - (col & 3) * 2)) & 0x03;
      // The BW writer inks every level below white, which turns a four-grey
      // picture almost entirely black. Re-dither to 1 bit instead: a plain
      // threshold would discard the brightness the four levels encode.
      if (bwMidThreshold) pixelValue = applyBayerDither1Bit(static_cast<uint8_t>(pixelValue * 85), imageX + col, row);
      pw.writePixel(imageX + col, pixelValue);
    }
  }
  return true;
}

bool BmpViewerActivity::displayCachedImageInFourGrey(const MappedInputManager::Labels& labels) {
  if (!imageCached) return false;

  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (!absolute && !renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Overlay).supported()) return false;

  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  LOG_DBG("BMP", "gray pass: absolute=%d planesAbs=%d inverted=%d at %d,%d exif=%u", absolute,
          renderer.grayPlanesAreAbsolute(), display.isInverted(), imageX, imageY, imageOrientation);
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!blitCachedImage()) {
      renderer.setRenderMode(GfxRenderer::BW);
      return false;
    }
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    LOG_DBG("BMP", "  %s plane sum=%lu", mode == GfxRenderer::GRAYSCALE_LSB ? "LSB" : "MSB",
            static_cast<unsigned long>(frameSum(renderer)));
    if (mode == GfxRenderer::GRAYSCALE_LSB) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
  }
  renderer.displayGrayBuffer();

  // Rebuild the BW framebuffer so popups and later differential updates have a
  // sane base to draw over. Split at mid grey so that base still reads as the
  // picture rather than a mostly-black silhouette.
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  blitCachedImage(/*bwMidThreshold=*/true);
  LOG_DBG("BMP", "  BW base sum=%lu", static_cast<unsigned long>(frameSum(renderer)));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.cleanupGrayscaleWithFrameBuffer();
  return true;
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // The streamed path paints no loading popup. Each popup costs a full-screen
  // refresh and its flash, and the grayscale sequence that follows already shows
  // the panel working through the base and the two planes; leaving the previous
  // screen up until the image lands reads as calmer than flashing twice first.
  Rect popupRect{};
  if (!isStreamDecodedImage()) {
    popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    GUI.fillPopupProgress(renderer, popupRect, 20);
  }
  if (isStreamDecodedImage()) {
    renderer.clearScreen();
    const bool hasPrevious = siblingImages.size() > 1 && currentImageIndex > 0;
    const bool hasNext = siblingImages.size() > 1 && currentImageIndex != -1 &&
                         currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel(), hasPrevious ? "<" : "", hasNext ? ">" : "");
    if (renderStreamDecodedImage()) {
      // The panel resolves four grey levels, and the decoders already quantise
      // to exactly those. Replay the cached pixels through the grayscale planes;
      // only fall back to the 1-bit framebuffer when that is unavailable.
      if (!displayCachedImageInFourGrey(labels)) {
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (imageCached) blitCachedImage(/*bwMidThreshold=*/true);
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        // HALF, not FAST: this is a full-screen swap from arbitrary prior content
        // (the file browser, and the loading popup drawn over it). On X3 a FAST
        // refresh takes the differential path and leaves that content ghosting
        // through the image; HALF requests a resync so the panel clears first.
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    return;
  }

  HalFile file;
  // 1. Open the BMP file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), confirmLabel(), (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      if (bitmap.hasGreyscale()) {
        const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
        if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
        if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
        bool planesReady = true;
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          if (bitmap.rewindToData() != BmpReaderError::Ok) {
            LOG_ERR("BMP", "Failed to rewind bitmap for grayscale rendering");
            planesReady = false;
            break;
          }
          renderer.clearScreen(absolute ? 0xFF : 0x00);
          renderer.setRenderMode(mode);
          if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
            planesReady = false;
            break;
          }
          GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          if (mode == GfxRenderer::GRAYSCALE_LSB) {
            renderer.copyGrayscaleLsbBuffers();
          } else {
            renderer.copyGrayscaleMsbBuffers();
          }
        }
        if (planesReady) renderer.displayGrayBuffer();

        // Rebuild the BW framebuffer for popups and subsequent differential updates.
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (bitmap.rewindToData() != BmpReaderError::Ok ||
            !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
          LOG_ERR("BMP", "Failed to rewind bitmap to restore the BW framebuffer");
          renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
          planesReady = false;
        }
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        renderer.cleanupGrayscaleWithFrameBuffer();
        if (!planesReady) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      } else {
        // Full-screen swap from the file browser: see the note on the streamed
        // path above for why this is not a FAST refresh.
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  discardImageCache();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  const bool transparentMode = SETTINGS.sleepScreen == InkAgentSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM;
  if (!canSetSleepCover()) return;

  const char* destination =
      transparentMode ? (FsHelpers::hasPngExtension(filePath) ? TRANSPARENT_SLEEP_ROOT_PNG : TRANSPARENT_SLEEP_ROOT_BMP)
                      : CUSTOM_SLEEP_ROOT_BMP;
  bool success = filePath == destination;

  if (!success) {
    auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_BUFFER_SIZE);
    if (!buffer) {
      LOG_ERR("BMP", "OOM: sleep cover copy buffer");
    } else {
      HalFile inFile, outFile;
      if (Storage.openFileForRead("BMP", filePath, inFile) && Storage.openFileForWrite("BMP", destination, outFile)) {
        int bytesRead;
        success = true;
        while ((bytesRead = inFile.read(buffer.get(), COPY_BUFFER_SIZE)) > 0) {
          if (outFile.write(buffer.get(), static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
            success = false;
            break;
          }
        }
        if (bytesRead < 0) success = false;
        outFile.close();
      }
    }
  }

  if (success) {
    if (!transparentMode) SETTINGS.sleepScreen = InkAgentSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    wallpaperSaved = false;
    onEnter();
    return true;
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (canSetWallpaper()) {
      doSetWallpaper();
    } else if (canSetSleepCover()) {
      doSetSleepCover();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}
