#include "XthImage.h"

#include <Logging.h>

#include <cstring>

#include "XtcTypes.h"

namespace xtc {

bool XthImage::load(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("XTH", path, file)) return false;

  uint8_t header[22];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  uint32_t magic;
  memcpy(&magic, header, 4);
  memcpy(&width_, header + 4, 2);
  memcpy(&height_, header + 6, 2);
  twoBit_ = magic == XTH_MAGIC;
  if (!twoBit_ && magic != XTG_MAGIC) {
    LOG_ERR("XTH", "Bad page magic 0x%08lX in %s", static_cast<unsigned long>(magic), path.c_str());
    return false;
  }
  if (width_ == 0 || height_ == 0) return false;
  if (header[9] != 0) {
    LOG_ERR("XTH", "Compressed page (mode %u) not supported", header[9]);
    return false;
  }

  planeBytes_ = (static_cast<size_t>(width_) * height_ + 7) / 8;
  colBytes_ = (height_ + 7) / 8;
  const size_t want = planeBytes_ * (twoBit_ ? 2 : 1);
  planes_ = makeUniqueNoThrow<uint8_t[]>(want);
  if (!planes_) {
    LOG_ERR("XTH", "OOM: %u bytes for planes", static_cast<unsigned>(want));
    return false;
  }
  if (static_cast<size_t>(file.read(planes_.get(), want)) != want) {
    LOG_ERR("XTH", "Short read of page data: %s", path.c_str());
    planes_.reset();
    return false;
  }
  return true;
}

}  // namespace xtc
