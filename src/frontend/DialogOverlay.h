#pragma once
// Draws the PSP system dialogs (savedata list/confirm/progress, messages) that the HLE runs
// without a picture (psp/host_dialog.h). The dialog itself reads the controller, so the keys
// bound to CROSS/CIRCLE/the d-pad drive it; this only paints what it shows, into an RGBA image
// laid over the game picture.
#include <cstdint>
#include <string>
#include <vector>

#include "psp/host_dialog.h"

namespace ca {

struct DialogKeys {
    std::string confirm, cancel, move, moveYesNo; // e.g. "Enter", "Backspace", "Up/Down", "Left/Right"
};

constexpr int kOverlayW = 960, kOverlayH = 544; // twice the PSP screen

// Paints `view` into `rgba` (kOverlayW x kOverlayH, RGBA8888, row 0 = top, premultiplied
// alpha is not used).
void paintDialog(const psp::host::DialogView& view, const DialogKeys& keys, std::vector<uint32_t>& rgba);

} // namespace ca
