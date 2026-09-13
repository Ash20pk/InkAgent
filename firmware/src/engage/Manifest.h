#pragma once

// Manifest -> Screen. Parses once, resolves every binding immediately, and
// lets the JSON document die before the caller draws anything.

#include <cstddef>

#include "Screen.h"

class GfxRenderer;

namespace engage {

// Fills `out` from a JSON manifest. Returns false only when the document does
// not parse; rows beyond kMaxRows and strings beyond kMaxTextBytes are dropped
// or truncated with a log line rather than failing the screen.
bool parseScreen(const char* json, size_t len, const GfxRenderer& renderer, Screen& out);

}  // namespace engage
