#include "gfx/vxl.h"
#include "gfx/assets.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "gfx/vxl_normals.inc"

namespace VxlRt {
namespace {

// ---- 常量 ----
// RA2 原作：体素≈1px（侧面朝向相机时）；引擎瓦片 64px。
// 略小于 64/60，避免整车（含炮塔）在 1 格内显得过大。
constexpr float kScale = 0.72f;
constexpr float kLightX = -0.55f, kLightY = -0.55f, kLightZ = 0.63f;

struct Vec3 { float x, y, z; };
struct Voxel { int x, y, z; uint8_t color, normal; };
struct Section {
    char name[17]{};
    int sx = 0, sy = 0, sz = 0;
    int normalsType = 4;
    float scale = 1.f;       // 尾部 scale（通常 1/12）；西木引擎忽略，仅作参考
    float minB[3]{};         // 体素盒 MinXYZ（相对共享原点 0,0,0）
    float maxB[3]{};         // 体素盒 MaxXYZ — 炮塔/炮管靠此叠到车体上
    bool hasBounds = false;
    float tf[12]{};
    bool hasTf = false;
    std::vector<Voxel> voxels;
};
struct VxlFile {
    std::vector<Section> sections;
    bool valid = false;
};

struct PalRGB { uint8_t r, g, b; };
static PalRGB gPal[256]{};
static bool gPalOk = false;
static std::vector<std::vector<uint8_t>> gVpl; // [sec][color]
static bool gVplOk = false;
static bool gInited = false;

static std::unordered_map<std::string, VxlFile> gVxlCache;

static bool readFile(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto n = (size_t)f.tellg();
    if (n == 0 || n > 64u * 1024u * 1024u) return false;
    f.seekg(0, std::ios::beg);
    out.resize(n);
    f.read((char*)out.data(), (std::streamsize)n);
    return (bool)f || f.eof();
}

static void loadPal() {
    std::vector<uint8_t> raw;
    if (!readFile("assets/palettes/unittem.pal", raw) || raw.size() < 768) {
        fprintf(stderr, "VXL: missing assets/palettes/unittem.pal\n");
        return;
    }
    for (int i = 0; i < 256; i++) {
        // Westwood 6-bit → 8-bit
        gPal[i].r = (uint8_t)std::min(255, raw[i * 3 + 0] << 2);
        gPal[i].g = (uint8_t)std::min(255, raw[i * 3 + 1] << 2);
        gPal[i].b = (uint8_t)std::min(255, raw[i * 3 + 2] << 2);
    }
    gPalOk = true;
}

static void loadVpl() {
    std::vector<uint8_t> raw;
    if (!readFile("assets/voxels/voxels.vpl", raw) || raw.size() < 16 + 768 + 256) {
        fprintf(stderr, "VXL: missing assets/voxels/voxels.vpl\n");
        return;
    }
    uint32_t nsec = 0;
    memcpy(&nsec, raw.data() + 8, 4);
    if (nsec < 1) nsec = 1;
    if (nsec > 32) nsec = 32;
    size_t base = 16 + 768;
    gVpl.assign(nsec, std::vector<uint8_t>(256));
    for (uint32_t s = 0; s < nsec; s++) {
        size_t off = base + (size_t)s * 256;
        if (off + 256 > raw.size()) break;
        memcpy(gVpl[s].data(), raw.data() + off, 256);
    }
    gVplOk = !gVpl.empty();
}

static bool parseVxl(const std::vector<uint8_t>& data, VxlFile& out) {
    out = {};
    if (data.size() < 34 || memcmp(data.data(), "Voxel Animation", 15) != 0)
        return false;
    uint32_t nsec = 0, bodysize = 0;
    memcpy(&nsec, data.data() + 20, 4);
    memcpy(&bodysize, data.data() + 28, 4);
    if (nsec == 0 || nsec > 64) return false;
    size_t hdrEnd = 16 + 16 + 768 + 2;
    size_t bodyStart = hdrEnd + 28 * nsec;
    size_t tailerBase = bodyStart + bodysize;
    if (tailerBase + 92 * nsec > data.size()) return false;

    std::vector<std::string> names(nsec);
    for (uint32_t i = 0; i < nsec; i++) {
        char nm[17]{};
        memcpy(nm, data.data() + hdrEnd + i * 28, 16);
        names[i] = nm;
    }
    const uint8_t* body = data.data() + bodyStart;
    for (uint32_t i = 0; i < nsec; i++) {
        size_t tb = tailerBase + i * 92;
        uint32_t sstart = 0, send = 0, sdata = 0;
        memcpy(&sstart, data.data() + tb, 4);
        memcpy(&send, data.data() + tb + 4, 4);
        memcpy(&sdata, data.data() + tb + 8, 4);
        Section sec;
        std::memset(sec.name, 0, sizeof(sec.name));
        std::memcpy(sec.name, names[i].c_str(), std::min(names[i].size(), sizeof(sec.name) - 1));
        // Tailer: +12 scale, +16 transform[3×4], +64 min[3], +76 max[3], +88 size/normals
        memcpy(&sec.scale, data.data() + tb + 12, 4);
        memcpy(sec.tf, data.data() + tb + 16, 48);
        sec.hasTf = true;
        memcpy(sec.minB, data.data() + tb + 64, 12);
        memcpy(sec.maxB, data.data() + tb + 76, 12);
        sec.sx = data[tb + 88];
        sec.sy = data[tb + 89];
        sec.sz = data[tb + 90];
        sec.normalsType = data[tb + 91] ? data[tb + 91] : 4;
        // 盒尺寸须为正且与网格同量级，否则回退网格坐标
        if (sec.sx > 0 && sec.sy > 0 && sec.sz > 0) {
            float bx = sec.maxB[0] - sec.minB[0];
            float by = sec.maxB[1] - sec.minB[1];
            float bz = sec.maxB[2] - sec.minB[2];
            sec.hasBounds = (bx > 0.01f && by > 0.01f && bz > 0.01f);
        }
        if (sstart == 0xFFFFFFFFu || send == 0xFFFFFFFFu || sec.sx == 0 || sec.sy == 0 || sec.sz == 0) {
            out.sections.push_back(std::move(sec));
            continue;
        }
        int ncol = sec.sx * sec.sy;
        for (int c = 0; c < ncol; c++) {
            if (sstart + (uint32_t)c * 4 + 4 > bodysize) break;
            int32_t co = 0;
            memcpy(&co, body + sstart + c * 4, 4);
            if (co < 0 || sdata + (uint32_t)co >= bodysize) continue;
            int x = c % sec.sx;
            int y = c / sec.sx;
            size_t p = sdata + (size_t)co;
            int z = 0;
            while (z < sec.sz && p + 1 < bodysize) {
                z += body[p++]; 
                if (p >= bodysize) break;
                int cnt = body[p++];
                for (int k = 0; k < cnt; k++) {
                    if (p + 1 >= bodysize) break;
                    uint8_t col = body[p++], nrm = body[p++];
                    sec.voxels.push_back({x, y, z, col, nrm});
                    z++;
                }
                if (p < bodysize) p++; // skip span trailer byte
            }
        }
        out.sections.push_back(std::move(sec));
    }
    out.valid = !out.sections.empty();
    return out.valid;
}

static const VxlFile* getVxl(const char* stem) {
    if (!stem || !stem[0]) return nullptr;
    auto it = gVxlCache.find(stem);
    if (it != gVxlCache.end()) return it->second.valid ? &it->second : nullptr;
    char path[256];
    snprintf(path, sizeof(path), "assets/voxels/%s.vxl", stem);
    std::vector<uint8_t> raw;
    VxlFile vf;
    if (!readFile(path, raw) || !parseVxl(raw, vf)) {
        gVxlCache.emplace(stem, VxlFile{});
        return nullptr;
    }
    auto& slot = gVxlCache[stem];
    slot = std::move(vf);
    return &slot;
}

static void applyMat(const float* m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[1] * y + m[2] * z + m[3];
    oy = m[4] * x + m[5] * y + m[6] * z + m[7];
    oz = m[8] * x + m[9] * y + m[10] * z + m[11];
}

static float phiForScreenAlpha(float alphaDeg) {
    float a = alphaDeg * (float)(3.14159265358979323846 / 180.0);
    return (float)(180.0 / 3.14159265358979323846) *
           std::atan2(2.f * std::sin(a) - std::cos(a), 2.f * std::sin(a) + std::cos(a));
}

static void rotZ(float x, float y, float z, float c, float s, float& ox, float& oy, float& oz) {
    ox = x * c - y * s; oy = x * s + y * c; oz = z;
}

static void proj(float x, float y, float z, float& sx, float& sy) {
    sx = x - y;
    sy = (x + y) * 0.5f - z;
}

static uint8_t shadeIndex(uint8_t color, uint8_t normalIdx, int ntype, float cosf, float sinf, float& shadeOut) {
    const float (*norms)[3] = kNormals4;
    int ncount = kNormals4Count;
    if (ntype == 2) { norms = kNormals2; ncount = kNormals2Count; }
    shadeOut = 1.f;
    if (ncount <= 0) return color;
    int ni = normalIdx % ncount;
    float nx = norms[ni][0], ny = norms[ni][1], nz = norms[ni][2];
    float rx, ry, rz;
    rotZ(nx, ny, nz, cosf, sinf, rx, ry, rz);
    float ln = std::sqrt(kLightX * kLightX + kLightY * kLightY + kLightZ * kLightZ);
    float dot = (rx * kLightX + ry * kLightY + rz * kLightZ) / (ln > 1e-6f ? ln : 1.f);
    if (!gVplOk) {
        shadeOut = std::clamp(0.48f + 0.52f * std::max(0.f, dot), 0.42f, 1.f);
        return color;
    }
    int nsec = (int)gVpl.size();
    int sec = (int)(std::clamp((dot + 1.f) * 0.5f, 0.f, 1.f) * (nsec - 1) + 0.5f);
    sec = std::clamp(sec, 0, nsec - 1);
    uint8_t out = gVpl[sec][color];
    // 近白回退：离线/运行时都会把金属冲成雪花
    if (out == 15 || ((color < 16 || color > 31) && sec > 20)) {
        int sec2 = std::min(sec, 14);
        uint8_t out2 = gVpl[sec2][color];
        if (out2 == 15) {
            shadeOut = std::clamp(0.5f + 0.5f * std::max(0.f, dot), 0.45f, 1.f);
            return color;
        }
        out = out2;
    }
    return out;
}

struct Pt {
    float sx, sy, depth;
    uint8_t color;
    float shade;
    // face corners in world (up to 3 visible faces × 4)
    float faces[3][4][3];
    float faceShade[3];
    uint8_t faceColor[3];
    int nfaces;
};

static const int kFaceOff[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
};
static const float kFaceCorn[6][4][3] = {
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}},
    {{0,1,0},{0,1,1},{1,1,1},{1,1,0}},
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},
    {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},
};
static const float kFaceNrm[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
};
static const float kFaceMul[6] = {0.88f, 0.72f, 0.92f, 0.78f, 1.05f, 0.65f};

static void projectVxl(const VxlFile& vxl, float facingPhiDeg, std::vector<Pt>& pts,
                       float& mnx, float& mxx, float& mny, float& mxy) {
    pts.clear();
    mnx = 1e9f; mxx = -1e9f; mny = 1e9f; mxy = -1e9f;
    float cosf = std::cos(facingPhiDeg * (float)(3.14159265358979323846 / 180.0));
    float sinf = std::sin(facingPhiDeg * (float)(3.14159265358979323846 / 180.0));

    for (const Section& sec : vxl.sections) {
        // 占用表（原网格面剔除）
        std::unordered_set<uint64_t> occ;
        occ.reserve(sec.voxels.size() * 2);
        auto key = [](int x, int y, int z) -> uint64_t {
            return ((uint64_t)(uint16_t)x << 32) | ((uint64_t)(uint16_t)y << 16) | (uint16_t)z;
        };
        for (const Voxel& v : sec.voxels)
            occ.insert(key(v.x, v.y, v.z));

        // 网格 → 共享世界盒（ModEnc Voxel Bounds）：炮塔 minZ≈车体 maxZ，炮管在炮塔前方。
        // 西木忽略 tailer.scale；OpenRA 用 scale 且跳过 TF。我们按原作：只用 bounds，不用 1/12 scale。
        auto xform = [&](float lx, float ly, float lz, float& ox, float& oy, float& oz) {
            float x1, y1, z1;
            if (sec.hasBounds) {
                float sx = (sec.maxB[0] - sec.minB[0]) / (float)sec.sx;
                float sy = (sec.maxB[1] - sec.minB[1]) / (float)sec.sy;
                float sz = (sec.maxB[2] - sec.minB[2]) / (float)sec.sz;
                x1 = sec.minB[0] + lx * sx;
                y1 = sec.minB[1] + ly * sy;
                z1 = sec.minB[2] + lz * sz;
            } else if (sec.hasTf) {
                applyMat(sec.tf, lx, ly, lz, x1, y1, z1);
            } else {
                x1 = lx; y1 = ly; z1 = lz;
            }
            rotZ(x1, y1, z1, cosf, sinf, ox, oy, oz);
        };

        for (const Voxel& v : sec.voxels) {
            float cx, cy, cz;
            xform(v.x + 0.5f, v.y + 0.5f, v.z + 0.5f, cx, cy, cz);
            float sh0 = 1.f;
            uint8_t colLit = shadeIndex(v.color, v.normal, sec.normalsType, cosf, sinf, sh0);
            Pt pt{};
            pt.nfaces = 0;
            for (int fi = 0; fi < 6 && pt.nfaces < 3; fi++) {
                int nx = v.x + kFaceOff[fi][0];
                int ny = v.y + kFaceOff[fi][1];
                int nz = v.z + kFaceOff[fi][2];
                if (occ.count(key(nx, ny, nz))) continue;
                float rnx, rny, rnz;
                rotZ(kFaceNrm[fi][0], kFaceNrm[fi][1], kFaceNrm[fi][2], cosf, sinf, rnx, rny, rnz);
                if (rnx + rny + rnz <= 0.02f) continue;
                int fiOut = pt.nfaces++;
                for (int c = 0; c < 4; c++) {
                    float wx, wy, wz;
                    // 轻微外扩，闭合相邻体素面缝
                    float fx = kFaceCorn[fi][c][0];
                    float fy = kFaceCorn[fi][c][1];
                    float fz = kFaceCorn[fi][c][2];
                    fx = fx < 0.5f ? fx - 0.02f : fx + 0.02f;
                    fy = fy < 0.5f ? fy - 0.02f : fy + 0.02f;
                    fz = fz < 0.5f ? fz - 0.02f : fz + 0.02f;
                    xform(v.x + fx, v.y + fy, v.z + fz, wx, wy, wz);
                    pt.faces[fiOut][c][0] = wx;
                    pt.faces[fiOut][c][1] = wy;
                    pt.faces[fiOut][c][2] = wz;
                }
                pt.faceShade[fiOut] = sh0 * kFaceMul[fi];
                pt.faceColor[fiOut] = colLit;
            }
            if (pt.nfaces == 0) continue;
            proj(cx, cy, cz, pt.sx, pt.sy);
            pt.depth = cx + cy + cz;
            pt.color = colLit;
            pt.shade = sh0;
            pts.push_back(pt);
            mnx = std::min(mnx, pt.sx); mxx = std::max(mxx, pt.sx);
            mny = std::min(mny, pt.sy); mxy = std::max(mxy, pt.sy);
        }
    }
}

static Color rgbOf(uint8_t c, float shade) {
    if (c >= 16 && c <= 31) {
        float t = (c - 16) / 15.f;
        int r = (int)(140 + 100 * t * std::clamp(shade, 0.55f, 1.f));
        return Color{(uint8_t)std::clamp(r, 0, 255), 0, 0, 255};
    }
    int r = gPal[c].r, g = gPal[c].g, b = gPal[c].b;
    if (r + g + b > 700) { r = g = b = 200; }
    float m = std::clamp(shade, 0.38f, 1.05f);
    return Color{(uint8_t)std::clamp((int)(r * m), 0, 255),
                 (uint8_t)std::clamp((int)(g * m), 0, 255),
                 (uint8_t)std::clamp((int)(b * m), 0, 255), 255};
}

struct ZCell { float d; Color c; bool set; };

static void fillTri(std::vector<ZCell>& zbuf, int W, int H,
                    float x0, float y0, float x1, float y1, float x2, float y2,
                    float depth, Color col) {
    int minx = std::max(0, (int)std::floor(std::min({x0, x1, x2})));
    int maxx = std::min(W - 1, (int)std::ceil(std::max({x0, x1, x2})));
    int miny = std::max(0, (int)std::floor(std::min({y0, y1, y2})));
    int maxy = std::min(H - 1, (int)std::ceil(std::max({y0, y1, y2})));
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-6f) return;
    for (int yy = miny; yy <= maxy; yy++) {
        for (int xx = minx; xx <= maxx; xx++) {
            float w0 = (x1 - xx) * (y2 - yy) - (x2 - xx) * (y1 - yy);
            float w1 = (x2 - xx) * (y0 - yy) - (x0 - xx) * (y2 - yy);
            float w2 = (x0 - xx) * (y1 - yy) - (x1 - xx) * (y0 - yy);
            bool inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                   : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            int i = yy * W + xx;
            if (!zbuf[i].set || depth > zbuf[i].d) {
                zbuf[i] = {depth, col, true};
            }
        }
    }
}

static void fillQuad(std::vector<ZCell>& zbuf, int W, int H, const float scr[4][2], float depth, Color col) {
    fillTri(zbuf, W, H, scr[0][0], scr[0][1], scr[1][0], scr[1][1], scr[2][0], scr[2][1], depth, col);
    fillTri(zbuf, W, H, scr[0][0], scr[0][1], scr[2][0], scr[2][1], scr[3][0], scr[3][1], depth, col);
}

static void rasterize(const std::vector<Pt>& pts, float scale, float orgx, float orgy,
                      int canvasW, int canvasH, PixBuf& out) {
    const int ss = 4; // 4× 超采样，减轻体素面缝/条纹
    int W = canvasW * ss, H = canvasH * ss;
    std::vector<ZCell> zbuf((size_t)W * H);
    float s = scale * ss;
    float ox = orgx * ss, oy = orgy * ss;

    std::vector<const Pt*> order;
    order.reserve(pts.size());
    for (const Pt& p : pts) order.push_back(&p);
    std::sort(order.begin(), order.end(), [](const Pt* a, const Pt* b) { return a->depth < b->depth; });

    for (const Pt* p : order) {
        for (int fi = 0; fi < p->nfaces; fi++) {
            float scr[4][2];
            for (int c = 0; c < 4; c++) {
                float sx, sy;
                proj(p->faces[fi][c][0], p->faces[fi][c][1], p->faces[fi][c][2], sx, sy);
                scr[c][0] = ox + sx * s;
                scr[c][1] = oy + sy * s;
            }
            Color col = rgbOf(p->faceColor[fi], p->faceShade[fi]);
            fillQuad(zbuf, W, H, scr, p->depth + 0.001f * fi, col);
        }
    }

    // BOX 降采样：块内不透明像素均值（避免 NEAREST 抽到缝）
    out.resize(canvasW, canvasH);
    for (int y = 0; y < canvasH; y++) {
        for (int x = 0; x < canvasW; x++) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < ss; dy++) {
                for (int dx = 0; dx < ss; dx++) {
                    const ZCell& z = zbuf[(size_t)(y * ss + dy) * W + (x * ss + dx)];
                    if (!z.set) continue;
                    r += z.c.r; g += z.c.g; b += z.c.b; n++;
                }
            }
            if (n > 0)
                out.set(x, y, Color{(uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n), 255});
        }
    }
}

struct Layout {
    float scale = kScale;
    int w = 72, h = 72;
    float orgx = 0, orgy = 0;
    bool floating = false;
};

static Layout makeLayout(const std::vector<Pt>& pts, float mnx, float mxx, float mny, float mxy, bool floating) {
    Layout L;
    L.scale = kScale;
    L.floating = floating;
    float bw = (mxx - mnx) + 1.3f;
    float bh = (mxy - mny) + 1.3f;
    int margin = 2;
    int needW = (int)(bw * L.scale) + 2 * margin;
    int needH = floating ? (int)(bh * L.scale) + 2 * margin
                         : std::max((int)(bh * L.scale / 0.72f) + margin, (int)(bh * L.scale) + 2 * margin);
    L.w = std::max(48, needW);
    L.h = std::max(48, needH);
    if (L.w > 160) L.w = 160;
    if (L.h > 160) L.h = 160;
    float anchorY = floating ? L.h / 2.f + 4.f : L.h * 0.72f;
    float gx, gy;
    if (floating) {
        gx = (mnx + mxx) * 0.5f; gy = (mny + mxy) * 0.5f;
    } else {
        float ycut = mxy - 1.2f;
        float sx = 0; int n = 0;
        for (const Pt& p : pts) {
            if (p.sy >= ycut) { sx += p.sx; n++; }
        }
        gx = n ? sx / n : (mnx + mxx) * 0.5f;
        gy = mxy;
    }
    L.orgx = L.w / 2.f - gx * L.scale;
    L.orgy = anchorY - gy * L.scale + 0.5f * L.scale;
    return L;
}

static void mergeVxl(const VxlFile* a, const VxlFile* b, VxlFile& out) {
    out = {};
    if (a) for (const auto& s : a->sections) out.sections.push_back(s);
    if (b) for (const auto& s : b->sections) out.sections.push_back(s);
    out.valid = !out.sections.empty();
}

// ---- UnitType → VXL stem（对齐 gen_assets.py / art.ini Image）----
struct Stems { const char* hull; const char* tur; const char* barl; const char* unload; bool floating; };

static Stems stemsOf(UnitType t) {
    switch (t) {
        case UnitType::Grizzly:
            return {"gtnk", "gtnktur", "gtnkbarl", nullptr, false};
        case UnitType::Rhino: case UnitType::Type99:
            return {"htnk", "htnktur", "htnkbarl", nullptr, false};
        case UnitType::Apocalypse:
            return {"mtnk", "mtnktur", "mtnkbarl", nullptr, false};
        // YR 专属无本包 VXL：勿借用天启/灰熊模型，交给 PNG/程序化
        case UnitType::RobotTank: case UnitType::BattleFortress: case UnitType::MasterMind:
            return {nullptr, nullptr, nullptr, nullptr, false};
        case UnitType::Harvester: case UnitType::WarMiner: case UnitType::SlaveMiner:
            return {"harv", nullptr, nullptr, "horv", false};
        case UnitType::ChronoMiner:
            return {"cmin", nullptr, nullptr, "cmon", false};
        case UnitType::MCV:
            return {"mcv", nullptr, nullptr, nullptr, false};
        case UnitType::IFV:
            return {"fv", "fvtur", nullptr, nullptr, false};
        case UnitType::FlakTrack: case UnitType::GatlingTank:
            return {"htk", "htktur", "htkbarl", nullptr, false};
        case UnitType::PrismTank:
            return {"sref", "sreftur", nullptr, nullptr, false};
        case UnitType::TeslaTank: case UnitType::Magnetron:
            return {"ttnk", "ttnktur", nullptr, nullptr, false};
        case UnitType::MirageTank:
            return {"rtnk", "rtnktur", "rtnkbarl", nullptr, false};
        case UnitType::V3Launcher:
            return {"v3", nullptr, nullptr, nullptr, false};
        case UnitType::DemoTruck:
            return {"trucka", nullptr, nullptr, nullptr, false};
        case UnitType::TankDestroyer:
            return {"tnkd", nullptr, nullptr, nullptr, false};
        case UnitType::LasherTank:
            return {"ltnk", "ltnktur", "ltnkbarl", nullptr, false};
        // 原作 Voxel=no，用 DRON.shp；勿走缺失的 dron.vxl
        case UnitType::TerrorDrone: case UnitType::ChaosDrone:
            return {nullptr, nullptr, nullptr, nullptr, false};
        case UnitType::Intruder:
            return {"falc", nullptr, nullptr, nullptr, true};
        case UnitType::BlackEagle: case UnitType::MiG:
            return {"beag", nullptr, nullptr, nullptr, true};
        case UnitType::Kirov:
            return {"zep", nullptr, nullptr, nullptr, true};
        case UnitType::FloatingDisc: // YR 飞碟 ≠ 基洛夫
            return {nullptr, nullptr, nullptr, nullptr, true};
        case UnitType::Nighthawk: case UnitType::SiegeChopper:
            return {"shad", nullptr, nullptr, nullptr, true};
        case UnitType::Hornet:
            return {"hornet", nullptr, nullptr, nullptr, true};
        case UnitType::Destroyer:
            return {"dest", nullptr, nullptr, nullptr, true};
        case UnitType::Typhoon: case UnitType::Boomer:
            return {"sub", nullptr, nullptr, nullptr, true};
        case UnitType::Aegis:
            return {"aegis", nullptr, nullptr, nullptr, true};
        case UnitType::SeaScorpion:
            return {"hyd", nullptr, nullptr, nullptr, true};
        case UnitType::Dreadnought:
            return {"dred", nullptr, nullptr, nullptr, true};
        case UnitType::AircraftCarrier:
            return {"carrier", nullptr, nullptr, nullptr, true};
        case UnitType::AmphTransport:
            return {"trs", nullptr, nullptr, nullptr, true};
        default:
            return {nullptr, nullptr, nullptr, nullptr, false};
    }
}

static bool renderStem(const char* stem, const char* barl, int dir, bool floating,
                       const Layout* forced, PixBuf& out, Layout* outLayout) {
    const VxlFile* v = getVxl(stem);
    if (!v) return false;
    VxlFile merged;
    const VxlFile* use = v;
    if (barl) {
        const VxlFile* b = getVxl(barl);
        if (b) { mergeVxl(v, b, merged); use = &merged; }
    }
    float phi = phiForScreenAlpha(45.f * (dir & 7));
    std::vector<Pt> pts;
    float mnx, mxx, mny, mxy;
    projectVxl(*use, phi, pts, mnx, mxx, mny, mxy);
    if (pts.empty()) return false;
    Layout L = forced ? *forced : makeLayout(pts, mnx, mxx, mny, mxy, floating);
    rasterize(pts, L.scale, L.orgx, L.orgy, L.w, L.h, out);
    if (outLayout) *outLayout = L;
    return true;
}

} // namespace

void init() {
    if (gInited) return;
    loadPal();
    loadVpl();
    gInited = true;
    fprintf(stderr, "VXL: runtime renderer ready (pal=%d vpl=%d)\n", (int)gPalOk, (int)gVplOk);
}

bool hasBody(UnitType t) {
    init();
    Stems s = stemsOf(t);
    return s.hull && getVxl(s.hull);
}

static bool assemblyLayout(const Stems& s, int dir, Layout& outL) {
    const VxlFile* hull = getVxl(s.hull);
    if (!hull) return false;
    VxlFile merged;
    const VxlFile* use = hull;
    const VxlFile* tur = s.tur ? getVxl(s.tur) : nullptr;
    const VxlFile* barl = s.barl ? getVxl(s.barl) : nullptr;
    if (tur || barl) {
        merged = {};
        for (const auto& sec : hull->sections) merged.sections.push_back(sec);
        if (tur) for (const auto& sec : tur->sections) merged.sections.push_back(sec);
        if (barl) for (const auto& sec : barl->sections) merged.sections.push_back(sec);
        merged.valid = !merged.sections.empty();
        use = &merged;
    }
    float phi = phiForScreenAlpha(45.f * (dir & 7));
    std::vector<Pt> pts;
    float mnx, mxx, mny, mxy;
    projectVxl(*use, phi, pts, mnx, mxx, mny, mxy);
    if (pts.empty()) return false;
    outL = makeLayout(pts, mnx, mxx, mny, mxy, s.floating);
    return true;
}

bool renderBody(UnitType t, int dir, int frame, PixBuf& out) {
    init();
    if (!gPalOk) return false;
    Stems s = stemsOf(t);
    if (!s.hull) return false;
    (void)frame;
    Layout L;
    // 有炮塔时用整车盒做画布，保证车体/炮塔同锚点且炮塔不被裁切
    if (s.tur && assemblyLayout(s, dir, L))
        return renderStem(s.hull, nullptr, dir, s.floating, &L, out, nullptr);
    return renderStem(s.hull, nullptr, dir, s.floating, nullptr, out, nullptr);
}

bool renderTurret(UnitType t, int dir, PixBuf& out) {
    init();
    if (!gPalOk) return false;
    Stems s = stemsOf(t);
    if (!s.tur || !s.hull) return false;
    Layout L;
    if (!assemblyLayout(s, dir, L)) return false;
    return renderStem(s.tur, s.barl, dir, s.floating, &L, out, nullptr);
}

bool renderUnload(UnitType t, int dir, PixBuf& out) {
    init();
    if (!gPalOk) return false;
    Stems s = stemsOf(t);
    if (!s.unload) return false;
    return renderStem(s.unload, nullptr, dir, s.floating, nullptr, out, nullptr);
}

} // namespace VxlRt
