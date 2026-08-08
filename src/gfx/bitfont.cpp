// Westwood Unicode BitFont loader (RA2 game.fnt / fonT)
// Spec: https://moddingwiki.shikadi.net/wiki/Westwood_Unicode_BitFont_Format
// 原作字库偏繁体；简体缺字常指向空心「口」占位 → 用系统 TTF 同高度阈值补齐。
#include "gfx/bitfont.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace {

constexpr uint32_t kUnicodeRange = 0x10000;
constexpr size_t kHeaderSize = 0x1C;
constexpr size_t kUnicodeTableBytes = kUnicodeRange * sizeof(uint16_t);
constexpr size_t kImageDataBase = 0x2001C;

struct FntHeader {
    uint32_t ideographWidth = 0;
    uint32_t stride = 0;
    uint32_t lines = 0;
    uint32_t fontHeight = 0;
    uint32_t count = 0;
    uint32_t symbolDataSize = 0;
};

static uint32_t readU32(const unsigned char* p) {
    uint32_t v; std::memcpy(&v, p, 4); return v;
}
static uint16_t readU16(const unsigned char* p) {
    uint16_t v; std::memcpy(&v, p, 2); return v;
}

static bool parseHeader(const unsigned char* data, size_t len, FntHeader& h) {
    if (len < kHeaderSize + kUnicodeTableBytes) return false;
    if (std::memcmp(data, "fonT", 4) != 0) return false;
    h.ideographWidth = readU32(data + 0x04);
    h.stride = readU32(data + 0x08);
    h.lines = readU32(data + 0x0C);
    h.fontHeight = readU32(data + 0x10);
    h.count = readU32(data + 0x14);
    h.symbolDataSize = readU32(data + 0x18);
    if (h.stride == 0 || h.lines == 0 || h.fontHeight == 0 || h.symbolDataSize == 0) return false;
    if (h.symbolDataSize != 1u + h.stride * h.lines) return false;
    if (h.count == 0) return false;
    return len >= kImageDataBase + (size_t)h.symbolDataSize * (size_t)h.count;
}

static int symbolIndex(const unsigned char* data, int codepoint) {
    if (codepoint < 0 || codepoint >= (int)kUnicodeRange) return -1;
    return (int)readU16(data + kHeaderSize + codepoint * 2) - 1;
}

static bool bitAt(const unsigned char* bits, uint32_t stride, int x, int y) {
    uint32_t bi = (uint32_t)y * stride + (uint32_t)(x / 8);
    return (bits[bi] & (1u << (7 - (x % 8)))) != 0;
}

// 缺字占位：原作把大量未收录简体指到同一空心「口」字模（约 3900 次引用）
static int findPlaceholderSymbolIndex(const unsigned char* data, const FntHeader& h) {
    std::vector<int> counts((size_t)h.count, 0);
    for (int cp = 0; cp < (int)kUnicodeRange; cp++) {
        int idx = symbolIndex(data, cp);
        if (idx >= 0 && idx < (int)h.count) counts[(size_t)idx]++;
    }
    int best = -1, bestN = 0;
    for (int i = 0; i < (int)h.count; i++) {
        if (counts[(size_t)i] > bestN) { bestN = counts[(size_t)i]; best = i; }
    }
    return bestN >= 500 ? best : -1;
}

static Image makeGrayImage(int w, int h) {
    Image img{};
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
    img.data = MemAlloc((unsigned int)(w * h));
    if (img.data) std::memset(img.data, 0, (size_t)w * (size_t)h);
    return img;
}

static Image makeGlyphImage(const unsigned char* bits, uint32_t stride, uint32_t lines,
                            uint32_t fontHeight, uint8_t width, int bakeScale) {
    int w = width > 0 ? (int)width : 1;
    int h = (int)fontHeight;
    // GenImageFontAtlas 按 1bpp 灰度拷贝；必须用 GRAYSCALE，不能用 RGBA
    Image img = makeGrayImage(w, h);
    if (!img.data) return img;
    unsigned char* px = (unsigned char*)img.data;
    for (uint32_t y = 0; y < lines && (int)y < h; y++) {
        for (int x = 0; x < (int)width; x++) {
            if (bitAt(bits, stride, x, (int)y))
                px[y * w + x] = 255;
        }
    }
    if (bakeScale > 1)
        ImageResizeNN(&img, w * bakeScale, h * bakeScale);
    return img;
}

// 常见界面简→繁（命中原作繁体点阵，避免 TTF）
static int toTraditionalCp(int cp) {
    switch (cp) {
#include "bitfont_s2t.inc"
        default: return cp;
    }
}


struct RawGlyph {
    int idx = -1;
    uint8_t width = 0;
    const unsigned char* bits = nullptr;
    bool ok = false;
};

static RawGlyph lookupRaw(const unsigned char* data, const FntHeader& h, int codepoint) {
    RawGlyph r;
    r.idx = symbolIndex(data, codepoint);
    if (r.idx < 0 || r.idx >= (int)h.count) return r;
    const unsigned char* block = data + kImageDataBase + (size_t)h.symbolDataSize * (size_t)r.idx;
    r.width = block[0];
    r.bits = block + 1;
    r.ok = true;
    return r;
}

static GlyphInfo makeEmpty(int codepoint, int height, int advance, int bakeScale) {
    GlyphInfo g{};
    g.value = codepoint;
    g.advanceX = (advance > 0 ? advance : 8) * bakeScale;
    g.image = makeGrayImage(bakeScale, height * bakeScale);
    return g;
}

static GlyphInfo fromRaw(int codepoint, const RawGlyph& raw, const FntHeader& h, int spaceAdv, int bakeScale) {
    uint8_t width = raw.width;
    if (width == 0 && codepoint == 0x3000 && h.ideographWidth > 0)
        width = (uint8_t)(h.ideographWidth > 255 ? 255 : h.ideographWidth);
    GlyphInfo g{};
    g.value = codepoint;
    g.image = makeGlyphImage(raw.bits, h.stride, h.lines, h.fontHeight, width, bakeScale);
    int adv = (width > 0 ? (int)width : spaceAdv) + 1;
    g.advanceX = (adv > 0 ? adv : 1) * bakeScale;
    return g;
}

} // namespace

Font LoadFontFromRa2Fnt(const char* path, const int* codepoints, int codepointCount) {
    Font font{};
    if (!path || !FileExists(path)) return font;

    int dataSize = 0;
    unsigned char* fileData = LoadFileData(path, &dataSize);
    if (!fileData || dataSize <= 0) return font;

    FntHeader hdr{};
    if (!parseHeader(fileData, (size_t)dataSize, hdr)) {
        TraceLog(LOG_WARNING, "RA2 bitfont: bad header %s", path);
        UnloadFileData(fileData);
        return font;
    }

    std::vector<int> cps;
    cps.reserve((size_t)(codepointCount > 0 ? codepointCount : 0) + 100);
    std::vector<uint8_t> seen(kUnicodeRange, 0);
    auto addCp = [&](int cp) {
        if (cp < 0 || cp >= (int)kUnicodeRange || seen[(size_t)cp]) return;
        seen[(size_t)cp] = 1;
        cps.push_back(cp);
    };
    for (int c = 32; c < 127; c++) addCp(c);
    addCp((int)'?');
    if (codepoints)
        for (int i = 0; i < codepointCount; i++) addCp(codepoints[i]);
    if (cps.empty()) { UnloadFileData(fileData); return font; }

    int spaceAdv = 8;
    RawGlyph space = lookupRaw(fileData, hdr, (int)' ');
    if (space.ok && space.width > 0) spaceAdv = space.width;
    const int placeholderIdx = findPlaceholderSymbolIndex(fileData, hdr);
    TraceLog(LOG_INFO, "RA2 bitfont: placeholder symbol index=%d", placeholderIdx);

    std::vector<int> needTtf;
    needTtf.reserve(64);

    const int bakeScale = 1;
    font.baseSize = (int)hdr.fontHeight * bakeScale;
    font.glyphCount = (int)cps.size();
    font.glyphPadding = 1;
    font.glyphs = (GlyphInfo*)MemAlloc((unsigned int)font.glyphCount * sizeof(GlyphInfo));
    if (!font.glyphs) { UnloadFileData(fileData); return Font{}; }
    std::memset(font.glyphs, 0, (size_t)font.glyphCount * sizeof(GlyphInfo));

    int fromTrad = 0;
    for (int i = 0; i < font.glyphCount; i++) {
        int cp = cps[i];
        RawGlyph raw = lookupRaw(fileData, hdr, cp);
        bool usable = raw.ok && raw.width > 0 && raw.bits && raw.idx != placeholderIdx;

        if (!usable) {
            int trad = toTraditionalCp(cp);
            if (trad != cp) {
                RawGlyph tr = lookupRaw(fileData, hdr, trad);
                if (tr.ok && tr.width > 0 && tr.bits && tr.idx != placeholderIdx) {
                    raw = tr;
                    usable = true;
                    fromTrad++;
                }
            }
        }

        if (usable) font.glyphs[i] = fromRaw(cp, raw, hdr, spaceAdv, bakeScale);
        else {
            font.glyphs[i] = makeEmpty(cp, (int)hdr.fontHeight, spaceAdv, bakeScale);
            needTtf.push_back(i);
        }
    }

    UnloadFileData(fileData);

    // 剩余缺字（箭头/数学符号等）：复用 '?' 点阵，避免空白/豆腐
    if (!needTtf.empty()) {
        int q = -1;
        for (int i = 0; i < font.glyphCount; i++) {
            if (cps[(size_t)i] == (int)'?' && font.glyphs[i].image.data && font.glyphs[i].image.width > 1) {
                q = i;
                break;
            }
        }
        int filled = 0;
        if (q >= 0) {
            for (int gi : needTtf) {
                UnloadImage(font.glyphs[gi].image);
                font.glyphs[gi].image = ImageCopy(font.glyphs[q].image);
                font.glyphs[gi].advanceX = font.glyphs[q].advanceX;
                font.glyphs[gi].offsetX = font.glyphs[q].offsetX;
                font.glyphs[gi].offsetY = font.glyphs[q].offsetY;
                filled++;
            }
        }
        TraceLog(LOG_INFO, "RA2 bitfont: filled %d missing glyphs from '?'", filled);
    }

    TraceLog(LOG_INFO, "RA2 bitfont: packing atlas glyphs=%d...", font.glyphCount);
    Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, font.glyphCount, font.baseSize, font.glyphPadding, 0);
    TraceLog(LOG_INFO, "RA2 bitfont: atlas image %s", atlas.data ? "ok" : "FAIL");
    if (!atlas.data) {
        for (int i = 0; i < font.glyphCount; i++) UnloadImage(font.glyphs[i].image);
        MemFree(font.glyphs);
        font.glyphs = nullptr;
        return Font{};
    }

    TraceLog(LOG_INFO, "RA2 bitfont: uploading texture %dx%d", atlas.width, atlas.height);
    font.texture = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    TraceLog(LOG_INFO, "RA2 bitfont: unloading glyph images");
    for (int i = 0; i < font.glyphCount; i++) {
        UnloadImage(font.glyphs[i].image);
        font.glyphs[i].image = {};
    }
    TraceLog(LOG_INFO, "RA2 bitfont: glyph images unloaded");

    if (!font.texture.id) {
        if (font.recs) { MemFree(font.recs); font.recs = nullptr; }
        MemFree(font.glyphs);
        font.glyphs = nullptr;
        return Font{};
    }

    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    TraceLog(LOG_INFO, "RA2 bitfont: %s baseSize=%d glyphs=%d trad=%d missing=%d atlas=%dx%d",
             path, font.baseSize, font.glyphCount, fromTrad, (int)needTtf.size(),
             font.texture.width, font.texture.height);
    return font;
}
