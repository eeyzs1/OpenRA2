#pragma once
// Westwood Unicode BitFont (fonT / game.fnt) → raylib Font
#include "raylib.h"

// Load RA2 game.fnt (or any fonT Unicode BitFont). Packs only the given
// Unicode codepoints into a POINT-filtered atlas. On failure returns Font{}
// with texture.id == 0 (caller should fall back to TTF / default).
Font LoadFontFromRa2Fnt(const char* path, const int* codepoints, int codepointCount);
