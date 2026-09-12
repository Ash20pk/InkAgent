#include "JpegToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context struct passed through JPEGDEC callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by setUserPointer()).
// The file I/O callbacks receive the HalFile* via pFile->fHandle (set by jpegOpen()).
struct JpegContext {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};

  // Source dimensions after JPEGDEC's built-in scaling
  int scaledSrcWidth{0};
  int scaledSrcHeight{0};

  // Final output dimensions
  int dstWidth{0};
  int dstHeight{0};

  // Fine scale in 16.16 fixed-point (ESP32-C3 has no FPU).
  // X and Y axes use separate scale factors: the aspect ratio of the output (dstWidth/dstHeight)
  // may differ from the source (srcWidth/srcHeight) due to integer rounding of displayHeight.
  // Using a single (X-based) scale for both axes causes the wrong srcRow to be skipped
  // during nearest-neighbor downscaling, potentially losing critical image content.
  int32_t fineScaleFPX{1 << 16};  // X: src -> dst column mapping
  int32_t invScaleFPX{1 << 16};   // X: dst -> src column mapping
  int32_t fineScaleFPY{1 << 16};  // Y: src -> dst row mapping
  int32_t invScaleFPY{1 << 16};   // Y: dst -> src row mapping

  PixelCache cache;
  bool caching{false};

  // EXIF orientation (1..8). Folded into the pixel writer's transform, so the
  // decode loops keep emitting pixels in unrotated image order.
  uint8_t exifOrientation{1};  // 1 == no transform
  bool swapAxes{false};        // true when the orientation transposes the image

  uint32_t lastYieldMs{0};  // throttle state for yieldDuringDecode()
};

// File I/O callbacks use pFile->fHandle to access the HalFile*,
// avoiding the need for global file state.
void* jpegOpen(const char* filename, int32_t* size) {
  HalFile* f = new HalFile();
  if (!Storage.openFileForRead("JPG", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void jpegClose(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

// JPEGDEC tracks file position via pFile->iPos internally (e.g. JPEGGetMoreData
// checks iPos < iSize to decide whether more data is available). The callbacks
// MUST maintain iPos to match the actual file position, otherwise progressive
// JPEGs with large headers fail during parsing.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t bytesRead = f->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t jpegSeek(JPEGFILE* pFile, int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(pos)) return -1;
  pFile->iPos = pos;
  return pos;
}

// Read the EXIF orientation tag (0x0112) from a JPEG's APP1 segment. Returns 1
// (no transform) when absent or unparseable, so an unreadable tag renders as it
// always did. Phone cameras store a portrait shot as a landscape frame plus this
// tag, so ignoring it displays the photo on its side.
//
// Only the first 4 KB of the APP1 payload is examined: IFD0 and its orientation
// entry sit at the front in every camera file, and the bound keeps the scratch
// allocation small on a device with no memory to spare.
constexpr size_t EXIF_SCAN_LIMIT = 4096;
constexpr uint8_t EXIF_ORIENTATION_DEFAULT = 1;

uint16_t readU16(const uint8_t* p, bool littleEndian) {
  return littleEndian ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}

uint32_t readU32(const uint8_t* p, bool littleEndian) {
  return littleEndian ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24))
                      : (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

uint8_t parseExifOrientation(const uint8_t* buf, size_t len) {
  // "Exif\0\0" then a TIFF header the rest of the offsets are relative to.
  if (len < 14 || memcmp(buf, "Exif\0\0", 6) != 0) return EXIF_ORIENTATION_DEFAULT;
  const uint8_t* tiff = buf + 6;
  const size_t tiffLen = len - 6;

  bool littleEndian;
  if (tiff[0] == 'I' && tiff[1] == 'I') {
    littleEndian = true;
  } else if (tiff[0] == 'M' && tiff[1] == 'M') {
    littleEndian = false;
  } else {
    return EXIF_ORIENTATION_DEFAULT;
  }
  if (readU16(tiff + 2, littleEndian) != 42) return EXIF_ORIENTATION_DEFAULT;

  const uint32_t ifdOffset = readU32(tiff + 4, littleEndian);
  if (ifdOffset + 2 > tiffLen) return EXIF_ORIENTATION_DEFAULT;

  const uint16_t entryCount = readU16(tiff + ifdOffset, littleEndian);
  for (uint16_t i = 0; i < entryCount; i++) {
    const size_t entry = ifdOffset + 2 + (size_t)i * 12;
    if (entry + 12 > tiffLen) break;
    if (readU16(tiff + entry, littleEndian) != 0x0112) continue;
    // Type 3 (SHORT): the value sits in the first 2 bytes of the value field.
    const uint16_t value = readU16(tiff + entry + 8, littleEndian);
    if (value >= 1 && value <= 8) return (uint8_t)value;
    break;
  }
  return EXIF_ORIENTATION_DEFAULT;
}

uint8_t readExifOrientation(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("JPG", path, file)) return EXIF_ORIENTATION_DEFAULT;

  uint8_t head[4];
  if (file.read(head, 2) != 2 || head[0] != 0xFF || head[1] != 0xD8) return EXIF_ORIENTATION_DEFAULT;

  // Walk marker segments until APP1 turns up or the entropy-coded scan begins.
  for (int segment = 0; segment < 16; segment++) {
    if (file.read(head, 2) != 2 || head[0] != 0xFF) return EXIF_ORIENTATION_DEFAULT;
    const uint8_t marker = head[1];
    if (marker == 0xDA || marker == 0xD9) return EXIF_ORIENTATION_DEFAULT;  // scan/end: no EXIF

    if (file.read(head, 2) != 2) return EXIF_ORIENTATION_DEFAULT;
    const int payload = ((head[0] << 8) | head[1]) - 2;
    if (payload <= 0) return EXIF_ORIENTATION_DEFAULT;

    if (marker == 0xE1) {
      const size_t want = (size_t)payload < EXIF_SCAN_LIMIT ? (size_t)payload : EXIF_SCAN_LIMIT;
      auto buf = makeUniqueNoThrow<uint8_t[]>(want);
      if (!buf) {
        LOG_ERR("JPG", "OOM: %u bytes for EXIF scan", (unsigned)want);
        return EXIF_ORIENTATION_DEFAULT;
      }
      if ((size_t)file.read(buf.get(), want) != want) return EXIF_ORIENTATION_DEFAULT;
      return parseExifOrientation(buf.get(), want);
    }

    if (!file.seek(file.position() + payload)) return EXIF_ORIENTATION_DEFAULT;
  }
  return EXIF_ORIENTATION_DEFAULT;
}

// JPEGDEC object is ~17 KB due to internal decode buffers.
// Heap-allocate on demand so memory is only used during active decode.
constexpr size_t JPEG_DECODER_APPROX_SIZE = 20 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_JPEG = JPEG_DECODER_APPROX_SIZE + 16 * 1024;

// Choose JPEGDEC's built-in scale factor for coarse downscaling.
// Returns the scale denominator (1, 2, 4, or 8) and sets jpegScaleOption.
int chooseJpegScale(float targetScale, int& jpegScaleOption) {
  if (targetScale <= 0.125f) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    return 8;
  }
  if (targetScale <= 0.25f) {
    jpegScaleOption = JPEG_SCALE_QUARTER;
    return 4;
  }
  if (targetScale <= 0.5f) {
    jpegScaleOption = JPEG_SCALE_HALF;
    return 2;
  }
  jpegScaleOption = 0;
  return 1;
}

// Fixed-point 16.16 arithmetic avoids software float emulation on ESP32-C3 (no FPU).
constexpr int FP_SHIFT = 16;
constexpr int32_t FP_ONE = 1 << FP_SHIFT;
constexpr int32_t FP_MASK = FP_ONE - 1;

int jpegDrawCallback(JPEGDRAW* pDraw) {
  JpegContext* ctx = reinterpret_cast<JpegContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer) return 0;

  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);

  // In EIGHT_BIT_GRAYSCALE mode, pPixels contains 8-bit grayscale values
  // Buffer is densely packed: stride = pDraw->iWidth, valid columns = pDraw->iWidthUsed
  uint8_t* pixels = reinterpret_cast<uint8_t*>(pDraw->pPixels);
  const int stride = pDraw->iWidth;
  const int validW = pDraw->iWidthUsed;
  const int blockH = pDraw->iHeight;

  if (stride <= 0 || blockH <= 0 || validW <= 0) return 1;

  const bool useDithering = ctx->config->useDithering;
  bool caching = ctx->caching;
  const int32_t fineScaleFPX = ctx->fineScaleFPX;
  const int32_t invScaleFPX = ctx->invScaleFPX;
  const int32_t fineScaleFPY = ctx->fineScaleFPY;
  const int32_t invScaleFPY = ctx->invScaleFPY;
  GfxRenderer& renderer = *ctx->renderer;
  const int cfgX = ctx->config->x;
  const int cfgY = ctx->config->y;
  const int blockX = pDraw->x;
  const int blockY = pDraw->y;

  // Determine destination pixel range covered by this source block
  const int srcYEnd = blockY + blockH;
  const int srcXEnd = blockX + validW;

  int dstYStart = (int)((int64_t)blockY * fineScaleFPY >> FP_SHIFT);
  int dstYEnd = (srcYEnd >= ctx->scaledSrcHeight) ? ctx->dstHeight : (int)((int64_t)srcYEnd * fineScaleFPY >> FP_SHIFT);
  int dstXStart = (int)((int64_t)blockX * fineScaleFPX >> FP_SHIFT);
  int dstXEnd = (srcXEnd >= ctx->scaledSrcWidth) ? ctx->dstWidth : (int)((int64_t)srcXEnd * fineScaleFPX >> FP_SHIFT);

  // Pre-clamp destination ranges to screen bounds (eliminates per-pixel screen
  // checks). A transposing orientation sends image X to screen Y and vice versa,
  // so each image axis is clamped against the screen axis it actually lands on;
  // clamping the wrong one could place a pixel outside the framebuffer row.
  const int limitX = ctx->swapAxes ? (ctx->screenHeight - cfgY) : (ctx->screenWidth - cfgX);
  const int limitY = ctx->swapAxes ? (ctx->screenWidth - cfgX) : (ctx->screenHeight - cfgY);
  const int floorX = ctx->swapAxes ? -cfgY : -cfgX;
  const int floorY = ctx->swapAxes ? -cfgX : -cfgY;

  int clampYMax = ctx->dstHeight;
  if (limitY < clampYMax) clampYMax = limitY;
  if (dstYStart < floorY) dstYStart = floorY;
  if (dstYEnd > clampYMax) dstYEnd = clampYMax;

  int clampXMax = ctx->dstWidth;
  if (limitX < clampXMax) clampXMax = limitX;
  if (dstXStart < floorX) dstXStart = floorX;
  if (dstXEnd > clampXMax) dstXEnd = clampXMax;

  if (dstYStart >= dstYEnd || dstXStart >= dstXEnd) return 1;

  // Pre-compute orientation and render-mode state once per callback invocation
  DirectPixelWriter pw;
  pw.init(renderer);
  // Folds the EXIF rotation into the transform so the loops below stay in
  // unrotated image space.
  pw.composeImageRotation(ctx->exifOrientation, cfgX, cfgY, ctx->dstWidth, ctx->dstHeight);
  // A 1-bit framebuffer needs the grey thresholded directly; the 4-level value is
  // still what goes to the cache, which stores 2 bits per pixel.
  const bool bwTarget = pw.mode == GfxRenderer::BW && useDithering;

  // The cache streams to disk one MCU-row band at a time. Flushing rows below
  // this block (raster order guarantees they are final) repositions the band;
  // cacheOriginY then maps screen rows to the band-local buffer rows. If a flush
  // write fails, stop caching for the rest of this decode (and let finalize drop
  // the partial file) rather than writing past the band buffer.
  DirectCacheWriter cw;
  int cacheOriginY = 0;
  if (caching) {
    if (!ctx->cache.advanceTo(dstYStart)) {
      caching = false;
      ctx->caching = false;
    } else {
      cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows, ctx->cache.originX);
      cacheOriginY = ctx->config->y + ctx->cache.bandStart;
    }
  }

  // === 1:1 fast path: no scaling math ===
  if (fineScaleFPX == FP_ONE && fineScaleFPY == FP_ONE) {
    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, cacheOriginY);
      const uint8_t* row = &pixels[(dstY - blockY) * stride];
      for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        uint8_t gray = row[dstX - blockX];
        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, bwTarget ? applyBayerDither1Bit(gray, outX, outY) : dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    return 1;
  }

  // === Bilinear interpolation (upscale: fineScale > 1.0) ===
  // Smooths block boundaries that would otherwise create visible banding
  // on progressive JPEG DC-only decode (1/8 resolution upscaled to target).
  if (fineScaleFPX > FP_ONE && fineScaleFPY > FP_ONE) {
    // Pre-compute safe X range where lx0 and lx0+1 are both in [0, validW-1].
    // Only the left/right edge pixels (typically 0-2 and 1-8 respectively) need clamping.
    int safeXStart = (int)(((int64_t)blockX * fineScaleFPX + FP_MASK) >> FP_SHIFT);
    int safeXEnd = (int)((int64_t)(blockX + validW - 1) * fineScaleFPX >> FP_SHIFT);
    if (safeXStart < dstXStart) safeXStart = dstXStart;
    if (safeXEnd > dstXEnd) safeXEnd = dstXEnd;
    if (safeXStart > safeXEnd) safeXEnd = safeXStart;

    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, cacheOriginY);
      const int32_t srcFyFP = dstY * invScaleFPY;
      const int32_t fy = srcFyFP & FP_MASK;
      const int32_t fyInv = FP_ONE - fy;
      int ly0 = (srcFyFP >> FP_SHIFT) - blockY;
      int ly1 = ly0 + 1;
      if (ly0 < 0) ly0 = 0;
      if (ly0 >= blockH) ly0 = blockH - 1;
      if (ly1 >= blockH) ly1 = blockH - 1;

      const uint8_t* row0 = &pixels[ly0 * stride];
      const uint8_t* row1 = &pixels[ly1 * stride];

      // Left edge (with X boundary clamping)
      for (int dstX = dstXStart; dstX < safeXStart; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 < 0) lx0 = 0;
        if (lx1 < 0) lx1 = 0;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, bwTarget ? applyBayerDither1Bit(gray, outX, outY) : dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Interior (no X boundary checks — lx0 and lx0+1 guaranteed in bounds)
      for (int dstX = safeXStart; dstX < safeXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        const int lx0 = (srcFxFP >> FP_SHIFT) - blockX;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx0 + 1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx0 + 1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, bwTarget ? applyBayerDither1Bit(gray, outX, outY) : dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Right edge (with X boundary clamping)
      for (int dstX = safeXEnd; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, bwTarget ? applyBayerDither1Bit(gray, outX, outY) : dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    return 1;
  }

  // === Box average (downscale: fineScale < 1.0) ===
  // Each output pixel averages the source pixels that map onto it. Point-sampling
  // one of them instead aliases fine detail into noise, which the dither then
  // amplifies into visible speckle. The coarse JPEG scale keeps fineScale above
  // 0.5, so the box stays around 2x2 source pixels and the cost is bounded.
  //
  // A box may extend past the current block at its bottom/right edge, because the
  // neighbouring block has not been decoded yet in raster order. Clamping to the
  // block averages slightly fewer pixels on those seams, which is invisible next
  // to the aliasing it replaces.
  for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
    const int outY = cfgY + dstY;
    pw.beginRow(outY);
    if (caching) cw.beginRow(outY, cacheOriginY);

    int ly0 = (int)(((int64_t)dstY * invScaleFPY) >> FP_SHIFT) - blockY;
    int ly1 = (int)(((int64_t)(dstY + 1) * invScaleFPY) >> FP_SHIFT) - blockY;
    if (ly0 < 0) ly0 = 0;
    if (ly1 <= ly0) ly1 = ly0 + 1;
    if (ly1 > blockH) ly1 = blockH;
    if (ly0 >= ly1) ly0 = ly1 - 1;
    const int boxH = ly1 - ly0;

    for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
      const int outX = cfgX + dstX;
      int lx0 = (int)(((int64_t)dstX * invScaleFPX) >> FP_SHIFT) - blockX;
      int lx1 = (int)(((int64_t)(dstX + 1) * invScaleFPX) >> FP_SHIFT) - blockX;
      if (lx0 < 0) lx0 = 0;
      if (lx1 <= lx0) lx1 = lx0 + 1;
      if (lx1 > validW) lx1 = validW;
      if (lx0 >= lx1) lx0 = lx1 - 1;

      uint32_t sum = 0;
      for (int sy = ly0; sy < ly1; sy++) {
        const uint8_t* row = &pixels[sy * stride];
        for (int sx = lx0; sx < lx1; sx++) sum += row[sx];
      }
      const uint8_t gray = (uint8_t)(sum / (uint32_t)(boxH * (lx1 - lx0)));

      uint8_t dithered;
      if (useDithering) {
        dithered = applyBayerDither4Level(gray, outX, outY);
      } else {
        dithered = gray / 85;
        if (dithered > 3) dithered = 3;
      }
      pw.writePixel(outX, bwTarget ? applyBayerDither1Bit(gray, outX, outY) : dithered);
      if (caching) cw.writePixel(outX, dithered);
    }
  }

  return 1;
}

}  // namespace

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder for dimensions");
    return false;
  }

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, nullptr);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG for dimensions (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  int width = jpeg->getWidth();
  int height = jpeg->getHeight();

  // Report the size as displayed: callers lay out against these, and a rotated
  // photo occupies the transposed box.
  const uint8_t exif = readExifOrientation(imagePath);
  if (exif >= 5 && exif <= 8) std::swap(width, height);

  if (!validateAndStoreDimensions(width, height, out, "JPEG")) return false;
  LOG_DBG("JPG", "Image dimensions: %dx%d (exif orientation %u)", width, height, exif);

  return true;
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  LOG_DBG("JPG", "Decoding JPEG: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder");
    return false;
  }

  JpegContext ctx;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  ImageDimensions sourceDimensions;
  if (!validateAndStoreDimensions(jpeg->getWidth(), jpeg->getHeight(), sourceDimensions, "JPEG")) return false;
  const int srcWidth = sourceDimensions.width;
  const int srcHeight = sourceDimensions.height;

  bool isProgressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  if (isProgressive) {
    LOG_INF("JPG", "Progressive JPEG detected - decoding DC coefficients only (lower quality)");
  }

  // The config box is in screen space. A transposing orientation means the box's
  // width constrains the source's height, so swap it back into source space
  // before working out the scale.
  ctx.exifOrientation = readExifOrientation(imagePath);
  ctx.swapAxes = ctx.exifOrientation >= 5 && ctx.exifOrientation <= 8;
  const int boxWidth = ctx.swapAxes ? config.maxHeight : config.maxWidth;
  const int boxHeight = ctx.swapAxes ? config.maxWidth : config.maxHeight;

  // Calculate overall target scale
  float targetScale;
  int destWidth, destHeight;

  if (config.useExactDimensions && boxWidth > 0 && boxHeight > 0) {
    destWidth = boxWidth;
    destHeight = boxHeight;
    targetScale = (float)destWidth / srcWidth;
  } else {
    float scaleX = (boxWidth > 0 && srcWidth > boxWidth) ? (float)boxWidth / srcWidth : 1.0f;
    float scaleY = (boxHeight > 0 && srcHeight > boxHeight) ? (float)boxHeight / srcHeight : 1.0f;
    targetScale = (scaleX < scaleY) ? scaleX : scaleY;
    if (targetScale > 1.0f) targetScale = 1.0f;

    destWidth = (int)(srcWidth * targetScale);
    destHeight = (int)(srcHeight * targetScale);
  }

  // Choose JPEGDEC built-in scaling for coarse downscaling.
  // Progressive JPEGs: JPEGDEC forces JPEG_SCALE_EIGHTH internally (DC-only
  // decode produces 1/8 resolution). We must match this to avoid the if/else
  // priority chain in DecodeJPEG selecting a different scale.
  int jpegScaleOption;
  int jpegScaleDenom;
  if (isProgressive) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    jpegScaleDenom = 8;
  } else {
    jpegScaleDenom = chooseJpegScale(targetScale, jpegScaleOption);
  }

  if (destWidth <= 0 || destHeight <= 0) {
    LOG_ERR("JPG", "Degenerate output dimensions %dx%d for %s, skipping render", destWidth, destHeight,
            imagePath.c_str());
    return false;
  }

  ctx.scaledSrcWidth = (srcWidth + jpegScaleDenom - 1) / jpegScaleDenom;
  ctx.scaledSrcHeight = (srcHeight + jpegScaleDenom - 1) / jpegScaleDenom;
  ctx.dstWidth = destWidth;
  ctx.dstHeight = destHeight;
  ctx.fineScaleFPX = (int32_t)((int64_t)destWidth * FP_ONE / ctx.scaledSrcWidth);
  ctx.invScaleFPX = (int32_t)((int64_t)ctx.scaledSrcWidth * FP_ONE / destWidth);
  ctx.fineScaleFPY = (int32_t)((int64_t)destHeight * FP_ONE / ctx.scaledSrcHeight);
  ctx.invScaleFPY = (int32_t)((int64_t)ctx.scaledSrcHeight * FP_ONE / destHeight);

  LOG_DBG("JPG", "JPEG %dx%d -> %dx%d (scale %.2f, jpegScale 1/%d, fineScale %.2f, exif %u)%s", srcWidth, srcHeight,
          destWidth, destHeight, targetScale, jpegScaleDenom, (float)destWidth / ctx.scaledSrcWidth,
          ctx.exifOrientation, isProgressive ? " [progressive]" : "");

  // Set pixel type to 8-bit grayscale (must be after open())
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&ctx);

  // Start streaming the pixel cache to disk. The band only needs to hold the
  // tallest single decode block: a JPEGDEC MCU cell is at most 16 scaled-source
  // rows tall, which our fine scale maps to this many output rows.
  // The pixel cache streams raster bands of screen rows; a rotated image writes
  // across those bands rather than along them, so it is decoded uncached.
  ctx.caching = !config.cachePath.empty() && ctx.exifOrientation == 1;
  if (!config.cachePath.empty() && !ctx.caching) {
    LOG_DBG("JPG", "Skipping pixel cache: EXIF orientation %u", ctx.exifOrientation);
  }
  if (ctx.caching) {
    const int maxBlockDstRows = (int)(((int64_t)16 * ctx.fineScaleFPY) >> FP_SHIFT) + 2;
    if (!ctx.cache.begin(config.cachePath, destWidth, destHeight, config.x, config.y, maxBlockDstRows)) {
      LOG_ERR("JPG", "Failed to start cache stream, continuing without caching");
      ctx.caching = false;
    }
  }

  unsigned long decodeStart = millis();
  ctx.lastYieldMs = decodeStart;
  rc = jpeg->decode(0, 0, jpegScaleOption);
  unsigned long decodeTime = millis() - decodeStart;

  if (rc != 1) {
    LOG_ERR("JPG", "Decode failed (rc=%d, lastError=%d)", rc, jpeg->getLastError());
    if (ctx.caching) ctx.cache.abort();
    return false;
  }

  LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", decodeTime);

  // Finalize the streamed cache file. Note: a flush failure mid-decode clears
  // ctx.caching (the partial file is dropped), so re-read the flag here.
  if (ctx.caching) {
    ctx.cache.finalize();
  }

  return true;
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasJpgExtension(extension);
}
