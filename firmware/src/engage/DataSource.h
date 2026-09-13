#pragma once

// The whitelist of values a manifest may bind to.
//
// A manifest cannot invent a source: it names one of these strings or it gets
// nothing. That is the memory bound (every source writes into a caller buffer)
// and the distraction bound (nothing here polls, subscribes or notifies).

#include <cstddef>

class GfxRenderer;

namespace engage {

// Copies the value of `name` into `out` (NUL terminated, truncated to cap-1).
// Returns false and leaves `out` empty when the name is not on the whitelist.
bool resolveSource(const char* name, const GfxRenderer& renderer, char* out, size_t cap);

}  // namespace engage
