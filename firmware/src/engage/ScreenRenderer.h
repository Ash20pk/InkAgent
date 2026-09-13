#pragma once

class GfxRenderer;
class MappedInputManager;

namespace engage {
struct Screen;

// Draws the rows of `screen` starting at `startY`, and returns the y the next
// element would occupy. Draws no header, no hints and does not touch the panel,
// so a caller that owns its own chrome and refresh (the sleep screen, which
// runs its own grayscale passes) can compose with it.
// Allocation-free: everything it needs is already in the Screen struct.
int drawScreenBody(GfxRenderer& renderer, const Screen& screen, int startY);

// Measures what drawScreenBody would consume, without drawing. Lets a caller
// centre a block vertically before committing to it.
int measureScreenBody(const GfxRenderer& renderer, const Screen& screen);

// The full app-screen treatment: header, body, Back hint, and a fast-waveform
// push to the panel.
void renderScreen(GfxRenderer& renderer, MappedInputManager& mappedInput, const Screen& screen);

}  // namespace engage
