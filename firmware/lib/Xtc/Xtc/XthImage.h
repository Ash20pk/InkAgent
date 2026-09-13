#pragma once

#include <HalStorage.h>
#include <Memory.h>
#include <stdint.h>

#include <memory>
#include <string>

namespace xtc {

// Standalone Xteink page image (.xth 2-bit / .xtg 1-bit), as the stock
// firmware's "Pushed Images" are stored. 22-byte header (XtgPageHeader), then
// the bit planes: two for XTH, one for XTG. Planes are column-major - columns
// run right to left, 8 vertical pixels per byte, bit 7 the topmost pixel.
//
// level() returns the value in this firmware's convention (0=black .. 3=white).
// XTH stores 0=white 1=dark 2=light 3=black, so only the ends are swapped.
class XthImage {
 public:
  // Loads the header and planes. Fails on a bad magic, a compressed page, or a
  // page whose planes will not fit in memory.
  bool load(const std::string& path);

  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  bool twoBit() const { return twoBit_; }

  uint8_t level(uint16_t x, uint16_t y) const {
    const size_t off = static_cast<size_t>(width_ - 1 - x) * colBytes_ + (y >> 3);
    const uint8_t bit = 7 - (y & 7);
    const uint8_t b1 = (planes_[off] >> bit) & 1;
    if (!twoBit_) return b1 ? 3 : 0;  // XTG: 1 = white
    const uint8_t b2 = (planes_[planeBytes_ + off] >> bit) & 1;
    const uint8_t xth = static_cast<uint8_t>((b1 << 1) | b2);
    return xth == 0 ? 3 : xth == 3 ? 0 : xth;
  }

 private:
  std::unique_ptr<uint8_t[]> planes_;
  size_t planeBytes_ = 0;
  size_t colBytes_ = 0;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  bool twoBit_ = false;
};

}  // namespace xtc
