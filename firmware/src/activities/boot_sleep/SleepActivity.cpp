#include "SleepActivity.h"

#include <BitmapHelpers.h>
#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "Epub/converters/DirectPixelWriter.h"
#include "Epub/converters/DitherUtils.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "InkAgentSettings.h"
#include "InkAgentState.h"
#include "Xtc/XthImage.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"

namespace {

// Kept separate from /sleep.bmp and /.sleep so alpha-overlay art does not mix with full-screen wallpapers.
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
// Path of the user-chosen lock-screen wallpaper (set from the web file manager).
constexpr char LOCK_WALLPAPER_CONF[] = "/.inkagent/wallpaper.txt";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == InkAgentSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == InkAgentSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
        case TransparentOverlayPass::GrayscaleMsb: {
          const auto planePixel =
              grayPlanePixel(level, pass == TransparentOverlayPass::GrayscaleMsb, renderer.grayPlanesAreAbsolute());
          if (planePixel.write) renderer.drawPixel(screenX, screenY, planePixel.black);
          break;
        }
      }
    }
  }

  return true;
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return AlphaOverlayResult::Error;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // Keep the current display instead of trying another overlay with a
    // framebuffer that now contains an incomplete gray plane.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

enum class SleepRecentKind : uint8_t { Standard, Overlay };

bool isRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx, const uint8_t window) {
  return recentKind == SleepRecentKind::Overlay ? APP_STATE.isRecentOverlaySleep(idx, window)
                                                : APP_STATE.isRecentSleep(idx, window);
}

void pushRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx) {
  if (recentKind == SleepRecentKind::Overlay) {
    APP_STATE.pushRecentOverlaySleep(idx);
  } else {
    APP_STATE.pushRecentSleep(idx);
  }
}

bool findNextValidSleepImage(HalFile& dir, const SleepRecentKind recentKind, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) continue;

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = recentKind == SleepRecentKind::Overlay && FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep image: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

bool selectRandomSleepFile(const char* dirPath, const SleepRecentKind recentKind, std::string& selectedPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return false;

  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    return false;
  }

  uint16_t fileCount = 0;
  while (fileCount < UINT16_MAX && findNextValidSleepImage(dir, recentKind, name.get())) ++fileCount;
  if (fileCount == 0) return false;

  // Pick a random wallpaper, excluding recently shown ones.
  // Window: up to SLEEP_RECENT_COUNT entries, capped at fileCount-1.
  const uint8_t recentFill =
      recentKind == SleepRecentKind::Overlay ? APP_STATE.recentOverlaySleepFill : APP_STATE.recentSleepFill;
  const uint8_t window = static_cast<uint8_t>(std::min<uint16_t>(recentFill, fileCount - 1));
  auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
  for (uint8_t attempt = 0; attempt < 20 && isRecentSleepIndex(recentKind, randomFileIndex, window); attempt++) {
    randomFileIndex = static_cast<uint16_t>(random(fileCount));
  }

  dir.rewindDirectory();
  for (uint16_t index = 0; index <= randomFileIndex; ++index) {
    if (!findNextValidSleepImage(dir, recentKind, name.get())) return false;
  }

  selectedPath.reserve(strlen(dirPath) + 1 + strlen(name.get()));
  selectedPath = dirPath;
  selectedPath += "/";
  selectedPath += name.get();
  pushRecentSleepIndex(recentKind, randomFileIndex);
  APP_STATE.saveToFile();
  return true;
}

bool drawSleepPopupPreservingFrame(GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int frameThickness = metrics.popupFrameThickness;
  const int popupY = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int popupHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.popupMarginY * 2;
  const int bandTop = std::max(0, popupY - frameThickness);
  const int bandBottom = std::min(renderer.getScreenHeight(), popupY + popupHeight + frameThickness);
  const int bandHeight = bandBottom - bandTop;
  const size_t bandBytes = renderer.getRegionByteSize(0, bandTop, renderer.getScreenWidth(), bandHeight);

  auto savedBand = makeUniqueNoThrow<uint8_t[]>(bandBytes);
  if (!savedBand) {
    LOG_ERR("SLP", "OOM: sleep popup background (%u bytes)", static_cast<unsigned>(bandBytes));
    return false;
  }
  if (!renderer.copyRegionToBuffer(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to save sleep popup background");
    return false;
  }

  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  if (!renderer.copyBufferToRegion(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to restore sleep popup background");
    return false;
  }
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("SLP", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("SLP", "Free heap before sleep image decode: %d bytes", ESP.getFreeHeap());
  }
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool frameWasInverted = display.isInverted();

  // Sleep screens always use normal polarity. This activity draws directly
  // from onEnter (outside ActivityManager's per-render polarity resolution),
  // so clear any inversion left over from a night-mode reader render.
  display.setInverted(false);

  const bool renderQuickResume =
      SETTINGS.sleepScreen == InkAgentSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == InkAgentSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  if (SETTINGS.sleepScreen == InkAgentSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    // Transparent mode retains the current framebuffer. Materialize any
    // output-level inversion first so the retained content keeps its visible
    // polarity after the display driver returns to normal.
    if (frameWasInverted) renderer.invertScreen();
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    drawSleepPopupPreservingFrame(renderer);
    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    releaseSdFontCachesForDecode(renderer);
    return renderTransparentCustomSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (InkAgentSettings::SLEEP_SCREEN_MODE::LOCK):
      return renderLockSleepScreen();
    case (InkAgentSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (InkAgentSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (InkAgentSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (InkAgentSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677 &&
                      SETTINGS.sleepScreenCoverFilter == InkAgentSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      return;
    }
    file.close();
  }

  std::string selectedPath;
  if (!selectRandomSleepFile("/.sleep", SleepRecentKind::Standard, selectedPath)) {
    selectRandomSleepFile("/sleep", SleepRecentKind::Standard, selectedPath);
  }

  if (!selectedPath.empty()) {
    HalFile randFile;
    if (Storage.openFileForRead("SLP", selectedPath, randFile)) {
      LOG_DBG("SLP", "Randomly loading: %s", selectedPath.c_str());
      delay(100);
      Bitmap bitmap(randFile, true,
                    renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                        display.getController() == HalDisplay::Controller::SSD1677 &&
                        SETTINGS.sleepScreenCoverFilter == InkAgentSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        randFile.close();
        return;
      }
      randFile.close();
    }
  }

  renderDefaultSleepScreen();
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
namespace {
// Small padlock as a level predicate rather than a set of draw calls: the four-
// grey path paints the same pixels three times (B/W base, then each plane), and
// a predicate keeps the glyph identical across all of them.
//
// Returns 0 (black) or 3 (white) inside the glyph, -1 outside. Filled white with
// a black outline so it reads over either a light or a dark wallpaper.
int lockGlyphLevel(const int x, const int y, const int cx, const int topY, const int w) {
  const int bodyH = (w * 3) / 4;
  const int bodyY = topY + w / 2;
  const int bodyX = cx - w / 2;
  const int shW = (w * 3) / 5;
  const int shX = cx - shW / 2;

  if (x >= bodyX && x < bodyX + w && y >= bodyY && y < bodyY + bodyH) {
    const bool border = x == bodyX || x == bodyX + w - 1 || y == bodyY || y == bodyY + bodyH - 1;
    return border ? 0 : 3;
  }
  if (y >= topY && y < bodyY) {
    if (x == shX || x == shX + shW) return 0;             // shackle posts
    if (y == topY && x > shX && x < shX + shW) return 0;  // shackle top
  }
  return -1;
}
}  // namespace

void SleepActivity::renderLockSleepScreen() const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;

  // Wallpaper: /lock.{jpg,jpeg,png,bmp} on the SD root if present (drawn into
  // the framebuffer without flushing, so the padlock + hint overlay on top);
  // otherwise a light ground with a thin inner frame.
  // Wallpaper is whatever image the user picked in the web file manager
  // ("Set as wallpaper"); its path lives in /.inkagent/wallpaper.txt. Drawn
  // into the framebuffer without flushing so the padlock + hint overlay on top.
  // Held across the grayscale passes below: an XTH wallpaper is re-read per pass
  // otherwise, and it is the only source that carries real four-level tone.
  xtc::XthImage xthWallpaper;
  bool xthReady = false;
  int xthX = 0;
  int xthY = 0;

  auto drawWallpaper = [&]() -> bool {
    std::string path;
    {
      HalFile cfgFile;
      if (Storage.openFileForRead("SLP", LOCK_WALLPAPER_CONF, cfgFile)) {
        int c;
        while ((c = cfgFile.read()) >= 0 && c != '\n' && c != '\r' && path.size() < 250) path += static_cast<char>(c);
        cfgFile.close();
      }
    }
    if (path.empty()) return false;
    std::string ext = path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.'));
    for (auto& ch : ext) ch = static_cast<char>(tolower(ch));
    if (ext == ".bmp") {
      HalFile bmpFile;
      if (Storage.openFileForRead("SLP", path, bmpFile)) {
        Bitmap bmp(bmpFile, true, false);
        const auto place = calculateBitmapPlacement(bmp.getWidth(), bmp.getHeight(), renderer);
        const bool ok = bmp.parseHeaders() == BmpReaderError::Ok &&
                        renderer.drawBitmap1Bit(bmp, place.x, place.y, pageWidth, pageHeight);
        bmpFile.close();
        if (ok) return true;
      }
    } else if (ext == ".xth" || ext == ".xtg") {
      // Pre-rendered page: draw 1:1, centred. The sleep path keeps its single
      // HALF refresh, so the four levels have to be re-dithered down to ink or
      // paper. Thresholding them instead (levels 0-1 to ink) throws away the
      // brightness the dithering encoded and turns two thirds of a typical photo
      // solid black; an ordered dither on the level's grey value keeps the
      // original tone.
      if (xthWallpaper.load(path) && xthWallpaper.width() <= pageWidth && xthWallpaper.height() <= pageHeight) {
        xthX = (pageWidth - xthWallpaper.width()) / 2;
        xthY = (pageHeight - xthWallpaper.height()) / 2;
        xthReady = true;
        // B/W base only. The four levels are re-dithered to ink or paper here;
        // if the grayscale passes below run, they replace this entirely.
        for (uint16_t y = 0; y < xthWallpaper.height(); y++) {
          for (uint16_t x = 0; x < xthWallpaper.width(); x++) {
            const uint8_t grey = static_cast<uint8_t>(xthWallpaper.level(x, y) * 85);
            if (applyBayerDither1Bit(grey, xthX + x, xthY + y) == 0) renderer.drawPixel(xthX + x, xthY + y, true);
          }
        }
        return true;
      }
    } else if (auto* decoder = ImageDecoderFactory::getDecoder(path)) {
      RenderConfig cfg;
      cfg.x = 0;
      cfg.y = 0;
      cfg.maxWidth = pageWidth;
      cfg.maxHeight = pageHeight;
      cfg.useGrayscale = true;
      cfg.useDithering = true;
      if (decoder->decodeToFramebuffer(path, renderer, cfg)) return true;
    }
    LOG_ERR("SLP", "lock wallpaper failed: %s (free heap %u)", path.c_str(), (unsigned)ESP.getFreeHeap());
    renderer.clearScreen();  // a partial draw may have dirtied the buffer
    return false;
  };
  auto paintBase = [&]() {
    renderer.clearScreen();
    if (drawWallpaper()) return;  // wallpaper drawn; overlay follows
    renderer.clearScreen();
    const int m = 24;
    renderer.drawRect(m, m, pageWidth - 2 * m, pageHeight - 2 * m, 2, true);
    renderer.drawCenteredText(UI_10_FONT_ID, m + 24, tr(STR_INKAGENT), true, EpdFontFamily::BOLD);
  };

  // A single HALF refresh, as every other sleep screen does: it is the only
  // clean waveform before the panel powers down. (An earlier FAST pre-frame
  // "lock animation" left the panel half-updated -> a stuck-looking screen,
  // especially when sleeping from a non-reader screen.)
  constexpr int lockWidth = 14;
  const int lockTopY = pageHeight - lockWidth * 4;
  // B/W base: plain pixels. The grayscale passes need the glyph written through
  // the same DirectPixelWriter as the wallpaper, or it is not encoded into the
  // planes at all and the planes then overwrite the base - which is why the lock
  // was invisible once the four-grey path took over.
  const auto drawLockBw = [&]() {
    for (int y = lockTopY; y < lockTopY + lockWidth * 2 && y < pageHeight; y++) {
      for (int x = cx - lockWidth; x <= cx + lockWidth && x < pageWidth; x++) {
        const int lv = lockGlyphLevel(x, y, cx, lockTopY, lockWidth);
        if (lv >= 0) renderer.drawPixel(x, y, lv == 0);
      }
    }
  };
  const auto drawLockPlane = [&](DirectPixelWriter& pw) {
    for (int y = lockTopY; y < lockTopY + lockWidth * 2 && y < pageHeight; y++) {
      pw.beginRow(y);
      for (int x = cx - lockWidth; x <= cx + lockWidth && x < pageWidth; x++) {
        const int lv = lockGlyphLevel(x, y, cx, lockTopY, lockWidth);
        if (lv >= 0) pw.writePixel(x, static_cast<uint8_t>(lv));
      }
    }
  };

  paintBase();
  drawLockBw();

  // An XTH wallpaper carries four real grey levels. Rendering it 1-bit means
  // re-dithering an already-dithered picture, and the two patterns beat against
  // each other into visible mottling - so drive the panel's four greys instead,
  // the same plane sequence the cover sleep screen uses.
  if (xthReady && renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported()) {
    if (renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) {
      bool planesReady = true;
      for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
        renderer.clearScreen(0xFF);
        renderer.setRenderMode(plane);
        DirectPixelWriter pw;
        pw.init(renderer);
        for (uint16_t y = 0; y < xthWallpaper.height(); y++) {
          pw.beginRow(xthY + y);
          for (uint16_t x = 0; x < xthWallpaper.width(); x++) {
            pw.writePixel(xthX + x, xthWallpaper.level(x, y));
          }
        }
        drawLockPlane(pw);
        if (plane == GfxRenderer::GRAYSCALE_LSB) {
          renderer.copyGrayscaleLsbBuffers();
        } else {
          renderer.copyGrayscaleMsbBuffers();
        }
      }
      if (planesReady) {
        renderer.displayGrayBuffer();
        renderer.setRenderMode(GfxRenderer::BW);
        return;
      }
      renderer.setRenderMode(GfxRenderer::BW);
    }
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_INKAGENT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != InkAgentSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preserveBackground) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() &&
      (preserveBackground || SETTINGS.sleepScreenCoverFilter == InkAgentSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY)) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == InkAgentSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  const bool absolute = hasGreyscale && !preserveBackground &&
                        renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
  } else if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bool ready = true;
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (bitmap.rewindToData() != BmpReaderError::Ok) {
        ready = false;
        break;
      }
      renderer.clearScreen(absolute ? 0xFF : 0x00);
      renderer.setRenderMode(plane);
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY)) {
        ready = false;
        break;
      }
      if (plane == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    if (ready)
      renderer.displayGrayBuffer();
    else
      LOG_ERR("SLP", "Incomplete grayscale image; keeping the current display");
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched; skipping the initial clear makes
  // them transparent while retaining the existing grayscale pipeline.
  renderBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  if (!selectRandomSleepFile(TRANSPARENT_SLEEP_DIR, SleepRecentKind::Overlay, selectedPath)) {
    selectRandomSleepFile(TRANSPARENT_SLEEP_LEGACY_DIR, SleepRecentKind::Overlay, selectedPath);
  }

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  renderDefaultSleepScreen();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (InkAgentSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  // SSD absolute images use the new thresholds; other panels retain legacy tuning.
  const bool originalThresholds =
      renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
      display.getController() == HalDisplay::Controller::SSD1677 &&
      SETTINGS.sleepScreenCoverFilter == InkAgentSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == InkAgentSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.inkagent");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.inkagent");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.inkagent");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped, originalThresholds)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped, originalThresholds);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
