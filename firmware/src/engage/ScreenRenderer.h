#pragma once

class GfxRenderer;
class MappedInputManager;

namespace engage {
struct Screen;

// Draws `screen` and pushes it to the panel. Allocation-free: everything it
// needs is already in the Screen struct.
void renderScreen(GfxRenderer& renderer, MappedInputManager& mappedInput, const Screen& screen);

}  // namespace engage
