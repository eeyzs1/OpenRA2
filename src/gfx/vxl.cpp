#include "gfx/vxl.h"
#include "gfx/assets.h"
#include "core/content.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
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
// Match voxel projection to the 64px terrain grid; 0.72 made vehicles
// disproportionately small beside the unchanged SHP infantry/buildings.
constexpr float kScale = 1.0f;
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

struct HvaFile {
    int nframes = 0;
    int nsec = 0;
    std::vector<std::string> names;
    // mats[frame][section] = 12 floats (row-major 3x4)
    std::vector<std::vector<std::array<float, 12>>> mats;
    bool valid = false;
};

struct PalRGB { uint8_t r, g, b; };
static PalRGB gPal[256]{};
static bool gPalOk = false;
static std::vector<std::vector<uint8_t>> gVpl; // [sec][color]
static bool gVplOk = false;
static bool gInited = false;

static std::unordered_map<std::string, VxlFile> gVxlCache;
static std::unordered_map<std::string, HvaFile> gHvaCache;

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
    std::string palPath = contentResolve("assets/palettes/unittem.pal");
    if (palPath.empty()) palPath = "assets/palettes/unittem.pal";
    if (!readFile(palPath.c_str(), raw) || raw.size() < 768) {
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
    std::string vplPath = contentResolve("assets/voxels/voxels.vpl");
    if (vplPath.empty()) vplPath = "assets/voxels/voxels.vpl";
    if (!readFile(vplPath.c_str(), raw) || raw.size() < 16 + 768 + 256) {
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
    const std::string virt=(std::string(stem).starts_with("assets/")?std::string(stem):"assets/voxels/"+std::string(stem))+".vxl";
    std::string path = contentResolve(virt.c_str());
    if (path.empty()) path = virt;
    std::vector<uint8_t> raw;
    VxlFile vf;
    if (!readFile(path.c_str(), raw) || !parseVxl(raw, vf)) {
        gVxlCache.emplace(stem, VxlFile{});
        return nullptr;
    }
    auto& slot = gVxlCache[stem];
    slot = std::move(vf);
    return &slot;
}

static bool parseHva(const std::vector<uint8_t>& data, HvaFile& out) {
    out = {};
    if (data.size() < 24) return false;
    uint32_t nframes = 0, nsec = 0;
    memcpy(&nframes, data.data() + 16, 4);
    memcpy(&nsec, data.data() + 20, 4);
    if (nframes == 0 || nframes > 1024 || nsec == 0 || nsec > 64) return false;
    size_t pos = 24;
    if (pos + nsec * 16 > data.size()) return false;
    out.names.resize(nsec);
    for (uint32_t i = 0; i < nsec; i++) {
        char nm[17]{};
        memcpy(nm, data.data() + pos, 16);
        out.names[i] = nm;
        pos += 16;
    }
    size_t need = pos + (size_t)nframes * nsec * 48;
    if (data.size() < need) return false;
    out.nframes = (int)nframes;
    out.nsec = (int)nsec;
    out.mats.assign(nframes, std::vector<std::array<float, 12>>(nsec));
    for (uint32_t f = 0; f < nframes; f++) {
        for (uint32_t s = 0; s < nsec; s++) {
            memcpy(out.mats[f][s].data(), data.data() + pos, 48);
            for(float value:out.mats[f][s])if(!std::isfinite(value))return false;
            pos += 48;
        }
    }
    out.valid = true;
    return true;
}

static const HvaFile* getHva(const char* stem) {
    if (!stem || !stem[0]) return nullptr;
    auto it = gHvaCache.find(stem);
    if (it != gHvaCache.end()) return it->second.valid ? &it->second : nullptr;
    const std::string virt=(std::string(stem).starts_with("assets/")?std::string(stem):"assets/voxels/"+std::string(stem))+".hva";
    std::string path = contentResolve(virt.c_str());
    if (path.empty()) path = virt;
    std::vector<uint8_t> raw;
    HvaFile hf;
    if (!readFile(path.c_str(), raw) || !parseHva(raw, hf)) {
        gHvaCache.emplace(stem, HvaFile{});
        return nullptr;
    }
    auto& slot = gHvaCache[stem];
    slot = std::move(hf);
    return &slot;
}

static void applyMat(const float* m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0] * x + m[1] * y + m[2] * z + m[3];
    oy = m[4] * x + m[5] * y + m[6] * z + m[7];
    oz = m[8] * x + m[9] * y + m[10] * z + m[11];
}

static void applyMatScaledT(const float* m, float hs, float x, float y, float z,
                            float& ox, float& oy, float& oz) {
    // 旋转用线性部；平移 × section.scale（与 Python ra2lib.vxl_project 一致）
    ox = m[0] * x + m[1] * y + m[2] * z + m[3] * hs;
    oy = m[4] * x + m[5] * y + m[6] * z + m[7] * hs;
    oz = m[8] * x + m[9] * y + m[10] * z + m[11] * hs;
}

static void rotZ(float x, float y, float z, float c, float s, float& ox, float& oy, float& oz) {
    ox = x * c - y * s; oy = x * s + y * c; oz = z;
}

static void proj(float x, float y, float z, float& sx, float& sy) {
    sx = x - y;
    sy = (x + y) * 0.5f - z;
}

static void transformNormal(const float* matrix,float& x,float& y,float& z) {
    if(!matrix)return;
    // HVA is affine. Normals use the inverse transpose, without translation.
    const float a=matrix[0],b=matrix[1],c=matrix[2],d=matrix[4],e=matrix[5],f=matrix[6],g=matrix[8],h=matrix[9],i=matrix[10];
    const float determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(std::abs(determinant)<1e-8f)return;
    const float nx=((e*i-f*h)*x+(f*g-d*i)*y+(d*h-e*g)*z)/determinant;
    const float ny=((c*h-b*i)*x+(a*i-c*g)*y+(b*g-a*h)*z)/determinant;
    const float nz=((b*f-c*e)*x+(c*d-a*f)*y+(a*e-b*d)*z)/determinant;
    const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
    if(length>1e-8f){x=nx/length;y=ny/length;z=nz/length;}
}

static uint8_t shadeIndex(uint8_t color, uint8_t normalIdx, int ntype, float cosf, float sinf, float& shadeOut,const float* transform) {
    const float (*norms)[3] = kNormals4;
    int ncount = kNormals4Count;
    if (ntype == 2) { norms = kNormals2; ncount = kNormals2Count; }
    shadeOut = 1.f;
    if (ncount <= 0) return color;
    int ni = normalIdx % ncount;
    float nx = norms[ni][0], ny = norms[ni][1], nz = norms[ni][2];
    transformNormal(transform,nx,ny,nz);
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
                       float& mnx, float& mxx, float& mny, float& mxy,
                       const HvaFile* hva = nullptr, int hvaFrame = 0, std::optional<Vector2>* barrelTip = nullptr) {
    pts.clear();
    mnx = 1e9f; mxx = -1e9f; mny = 1e9f; mxy = -1e9f;
    float cosf = std::cos(facingPhiDeg * (float)(3.14159265358979323846 / 180.0));
    float sinf = std::sin(facingPhiDeg * (float)(3.14159265358979323846 / 180.0));
    int frame = 0;
    if (hva && hva->valid && hva->nframes > 0)
        frame = std::max(0,hvaFrame)%hva->nframes;
    float furthest = std::numeric_limits<float>::lowest();
    Vector2 tipSum{};
    int tipSamples = 0;

    for (const Section& sec : vxl.sections) {
        const float* hm = nullptr;
        if (hva && hva->valid) {
            for (int i = 0; i < hva->nsec; i++) {
                if (hva->names[i] == sec.name) {
                    hm = hva->mats[frame][i].data();
                    break;
                }
            }
            // 单节 VXL 与单条 HVA 名称偶发不一致（如 HTK tur: MDUMMY01 vs DUMMY01）
            if (!hm && (int)vxl.sections.size() == 1 && hva->nsec == 1)
                hm = hva->mats[frame][0].data();
        }
        float hs = (sec.scale > 1e-6f) ? sec.scale : 1.f;

        // 占用表（原网格面剔除）
        std::unordered_set<uint64_t> occ;
        occ.reserve(sec.voxels.size() * 2);
        auto key = [](int x, int y, int z) -> uint64_t {
            return ((uint64_t)(uint16_t)x << 32) | ((uint64_t)(uint16_t)y << 16) | (uint16_t)z;
        };
        for (const Voxel& v : sec.voxels)
            occ.insert(key(v.x, v.y, v.z));

        // 网格 → 共享世界盒；再乘 HVA（平移 × scale）把旋翼等节抬到机身。
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
            if (hm) applyMatScaledT(hm, hs, x1, y1, z1, x1, y1, z1);
            rotZ(x1, y1, z1, cosf, sinf, ox, oy, oz);
        };

        for (const Voxel& v : sec.voxels) {
            float cx, cy, cz;
            xform(v.x + 0.5f, v.y + 0.5f, v.z + 0.5f, cx, cy, cz);
            if (barrelTip) {
                // These barrels extend along local +X. Average the end face before view culling.
                float x, y, z;
                xform(v.x + 1.f, v.y + .5f, v.z + .5f, x, y, z);
                const float forward = x * cosf + y * sinf;
                constexpr float tolerance = .001f;
                if (forward > furthest + tolerance) {
                    furthest = forward;
                    tipSum = {};
                    tipSamples = 0;
                }
                if (std::abs(forward - furthest) <= tolerance) {
                    Vector2 point;
                    proj(x, y, z, point.x, point.y);
                    tipSum.x += point.x;
                    tipSum.y += point.y;
                    ++tipSamples;
                }
            }
            float sh0 = 1.f;
            uint8_t colLit = shadeIndex(v.color, v.normal, sec.normalsType, cosf, sinf, sh0,hm);
            Pt pt{};
            pt.nfaces = 0;
            for (int fi = 0; fi < 6 && pt.nfaces < 3; fi++) {
                int nx = v.x + kFaceOff[fi][0];
                int ny = v.y + kFaceOff[fi][1];
                int nz = v.z + kFaceOff[fi][2];
                if (occ.count(key(nx, ny, nz))) continue;
                float rnx, rny, rnz;
                float fnx=kFaceNrm[fi][0],fny=kFaceNrm[fi][1],fnz=kFaceNrm[fi][2];
                transformNormal(hm,fnx,fny,fnz);
                rotZ(fnx,fny,fnz,cosf,sinf,rnx,rny,rnz);
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
    if (barrelTip && tipSamples > 0)
        *barrelTip = Vector2{tipSum.x * kScale / tipSamples, tipSum.y * kScale / tipSamples};
}

static Color rgbOf(uint8_t c, float shade, const Color* houseColor) {
    if (c >= 16 && c <= 31) {
        const float brightness=std::max({gPal[c].r,gPal[c].g,gPal[c].b})/255.f*std::clamp(shade,0.55f,1.f);
        const Color house=houseColor?*houseColor:Color{255,0,0,255};
        return Color{(uint8_t)(house.r*brightness),(uint8_t)(house.g*brightness),(uint8_t)(house.b*brightness),255};
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
                      int canvasW, int canvasH, PixBuf& out, const Color* houseColor) {
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
            Color col = rgbOf(p->faceColor[fi], p->faceShade[fi], houseColor);
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

static Layout originLayout(float minX,float maxX,float minY,float maxY) {
    Layout layout;
    constexpr int margin=4;
    layout.orgx=std::ceil(margin-std::min(0.f,minX)*kScale);
    layout.orgy=std::ceil(margin-std::min(0.f,minY)*kScale);
    layout.w=(int)std::ceil(layout.orgx+std::max(0.f,maxX)*kScale)+margin+2;
    layout.h=(int)std::ceil(layout.orgy+std::max(0.f,maxY)*kScale)+margin+2;
    return layout;
}

static Layout makeLayout(const std::vector<Pt>& pts, float mnx, float mxx, float mny, float mxy, bool floating) {
    Layout L;
    L.scale = kScale;
    L.floating = floating;
    float bw = (mxx - mnx) + 1.3f;
    float bh = (mxy - mny) + 1.3f;
    int margin = 4;
    int needW = (int)(bw * L.scale) + 2 * margin;
    int needH = floating ? (int)(bh * L.scale) + 2 * margin
                         : std::max((int)(bh * L.scale / 0.72f) + margin, (int)(bh * L.scale) + 2 * margin);
    L.w = std::max(48, needW);
    L.h = std::max(48, needH);
    if (L.w > 192) L.w = 192;
    if (L.h > 192) L.h = 192;
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
    // 地面锚点偏心时扩边，避免车体顶穿画布（遥控坦克等）
    float left = L.orgx + mnx * L.scale;
    float right = L.orgx + mxx * L.scale;
    float top = L.orgy + mny * L.scale;
    float bottom = L.orgy + mxy * L.scale;
    int padL = (int)std::ceil(std::max(0.f, (float)margin - left));
    int padR = (int)std::ceil(std::max(0.f, right - (L.w - margin)));
    int padT = (int)std::ceil(std::max(0.f, (float)margin - top));
    int padB = (int)std::ceil(std::max(0.f, bottom - (L.h - margin)));
    L.w += padL + padR;
    L.h += padT + padB;
    L.orgx += (float)padL;
    L.orgy += (float)padT;
    if (L.w > 192) L.w = 192;
    if (L.h > 192) L.h = 192;
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
        case UnitType::Rhino:
            return {"htnk", "htnktur", "htnkbarl", nullptr, false};
        // 99式：融合阵营自制 PNG（非官方 VXL）；勿共用犀牛体素
        case UnitType::Type99:
            return {nullptr, nullptr, nullptr, nullptr, false};
        case UnitType::Apocalypse:
            // 官方 rules Image=MTNK（无独立 apoc.vxl）
            return {"mtnk", "mtnktur", "mtnkbarl", nullptr, false};
        case UnitType::RobotTank:
            // 盟军气垫遥控坦克：悬浮锚点（非履带触地）
            return {"robo", "robotur", nullptr, nullptr, true};
        case UnitType::BattleFortress:
            return {"bfrt", nullptr, nullptr, nullptr, false};
        case UnitType::MasterMind:
            return {"mind", nullptr, nullptr, nullptr, false};
        case UnitType::Harvester: case UnitType::WarMiner:
            return {"harv", nullptr, nullptr, "horv", false};
        case UnitType::SlaveMiner:
            return {"smin", "smintur", nullptr, nullptr, false};
        case UnitType::ChronoMiner:
            return {"cmin", nullptr, nullptr, "cmon", false};
        case UnitType::MCV:
            return {"mcv", nullptr, nullptr, nullptr, false};
        case UnitType::IFV:
            return {"fv", "fvtur", nullptr, nullptr, false};
        case UnitType::FlakTrack:
            return {"htk", "htktur", nullptr, nullptr, false};
        case UnitType::GatlingTank:
            return {"ytnk", "ytnktur", nullptr, nullptr, false};
        case UnitType::PrismTank:
            return {"sref", "sreftur", nullptr, nullptr, false};
        case UnitType::TeslaTank:
            return {"ttnk", "ttnktur", nullptr, nullptr, false};
        case UnitType::Magnetron:
            return {"tele", "teletur", nullptr, nullptr, false};
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
        // 原作 Voxel=no：恐怖机器人 DRON.shp；混乱机器人 CAOS.vxl（地面）
        case UnitType::TerrorDrone:
            return {nullptr, nullptr, nullptr, nullptr, false};
        case UnitType::ChaosDrone:
            return {"caos", nullptr, nullptr, nullptr, false};
        case UnitType::Intruder:
            return {"falc", nullptr, nullptr, nullptr, true};
        case UnitType::BlackEagle:
            return {"beag", nullptr, nullptr, nullptr, true};
        case UnitType::MiG:
            return {"bpln", nullptr, nullptr, nullptr, true};
        case UnitType::Kirov:
            return {"zep", nullptr, nullptr, nullptr, true};
        case UnitType::FloatingDisc:
            return {"disk", "disktur", nullptr, nullptr, true};
        case UnitType::Nighthawk:
            return {"shad", nullptr, nullptr, nullptr, true};
        case UnitType::SiegeChopper:
            return {"schp", nullptr, nullptr, nullptr, true};
        case UnitType::Hornet:
            return {"hornet", nullptr, nullptr, nullptr, true};
        case UnitType::ASW:
            return {"asw", nullptr, nullptr, nullptr, true};
        case UnitType::Destroyer:
            return {"dest", nullptr, nullptr, nullptr, true};
        case UnitType::Typhoon:
            return {"sub", nullptr, nullptr, nullptr, true};
        case UnitType::Boomer:
            return {"bsub", nullptr, nullptr, nullptr, true};
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

static bool projectStemPts(const char* stem, float phi, std::vector<Pt>& pts,
                           float& mnx, float& mxx, float& mny, float& mxy,int frame=0,
                           std::optional<Vector2>* barrelTip=nullptr) {
    const VxlFile* v = getVxl(stem);
    if (!v) return false;
    std::vector<Pt> part;
    float a, b, c, d;
    const auto* animation=getHva(stem);
    if(!animation)return false;
    projectVxl(*v, phi, part, a, b, c, d, animation, frame, barrelTip);
    if (part.empty()) return false;
    if (pts.empty()) {
        mnx = a; mxx = b; mny = c; mxy = d;
    } else {
        mnx = std::min(mnx, a); mxx = std::max(mxx, b);
        mny = std::min(mny, c); mxy = std::max(mxy, d);
    }
    pts.insert(pts.end(), part.begin(), part.end());
    return true;
}

static bool renderStemAt(const char* stem, const char* barl, float phi, bool floating,
                       const Layout* forced, PixBuf& out, Layout* outLayout, const Color* houseColor,int frame=0,
                       std::optional<Vector2>* barrelTip=nullptr) {
    const VxlFile* v = getVxl(stem);
    if (!v) return false;
    std::vector<Pt> pts;
    float mnx, mxx, mny, mxy;
    // 炮塔/炮管分开投影、各自 HVA，再合并点集（避免 MDUMMY01/DUMMY01 抢错矩阵）
    if (!projectStemPts(stem, phi, pts, mnx, mxx, mny, mxy,frame)) return false;
    if (barl && !projectStemPts(barl, phi, pts, mnx, mxx, mny, mxy,frame,barrelTip))return false;
    if (pts.empty()) return false;
    Layout L = forced ? *forced : makeLayout(pts, mnx, mxx, mny, mxy, floating);
    rasterize(pts, L.scale, L.orgx, L.orgy, L.w, L.h, out, houseColor);
    if (outLayout) *outLayout = L;
    return true;
}

static bool renderStem(const char* stem,const char* barl,int dir,bool floating,
                       const Layout* forced,PixBuf& out,Layout* outLayout,const Color* houseColor,int frame=0) {
    return renderStemAt(stem,barl,voxelFacingDegrees(dir),floating,forced,out,outLayout,houseColor,frame);
}

static bool fineAssemblyLayout(const Stems& stems,Layout& layout) {
    if(!stems.hull)return false;
    static std::unordered_map<std::string,Layout> layouts;
    if(auto found=layouts.find(stems.hull);found!=layouts.end()){layout=found->second;return true;}
    float minX=0,maxX=0,minY=0,maxY=0;
    const auto includePose=[&](const char* stem,float angle,int phase) {
        std::vector<Pt> points;float a,b,c,d;
        if(!projectStemPts(stem,angle,points,a,b,c,d,phase))return false;
        minX=std::min(minX,a);maxX=std::max(maxX,b);minY=std::min(minY,c);maxY=std::max(maxY,d);
        return true;
    };
    // Stream bounds, rather than retaining every facing's projected voxels.
    for(const char* stem:{stems.hull,stems.tur,stems.barl})if(stem) {
        const auto* animation=getHva(stem);if(!animation || !getVxl(stem))return false;
        for(int facing=0;facing<32;++facing)for(int phase=0;phase<animation->nframes;++phase)
            if(!includePose(stem,11.25f*(facing-4),phase))return false;
    }
    // Optional unload models share the rolling vehicle's physical origin.
    if(stems.unload && getVxl(stems.unload) && getHva(stems.unload))
        for(int dir=0;dir<8;++dir)includePose(stems.unload,voxelFacingDegrees(dir),0);
    layout=originLayout(minX,maxX,minY,maxY);
    layouts.emplace(stems.hull,layout);
    return true;
}

static bool assemblyLayout(const Stems& s, int dir, Layout& outL) {
    if (!s.hull || !getVxl(s.hull)) return false;
    static std::unordered_map<std::string,Layout> cache;
    const std::string key=std::string(s.hull)+"/"+std::to_string(dir&7);
    if(auto found=cache.find(key);found!=cache.end()){outL=found->second;return true;}
    float phi = voxelFacingDegrees(dir);
    std::vector<Pt> pts;
    float mnx = 0, mxx = 0, mny = 0, mxy = 0;
    // 各 stem 用自己的 HVA 投影，再合并包围盒（避免合并后同名节抢错矩阵）
    if (!projectStemPts(s.hull, phi, pts, mnx, mxx, mny, mxy)) return false;
    if (s.tur && !projectStemPts(s.tur, phi, pts, mnx, mxx, mny, mxy))return false;
    if (s.barl && !projectStemPts(s.barl, phi, pts, mnx, mxx, mny, mxy))return false;
    if (pts.empty()) return false;
    // One canvas and origin for every phase: rotors/barrels must not move the hull.
    // Retain only phase-zero anchor points; stream later phase bounds.
    for(const char* stem:{s.hull,s.tur,s.barl})if(stem) {
        const auto* animation=getHva(stem);if(!animation)return false;
        for(int frame=1;frame<animation->nframes;++frame) {
            std::vector<Pt> phase;float a,b,c,d;
            if(!projectStemPts(stem,phi,phase,a,b,c,d,frame))return false;
            mnx=std::min(mnx,a);mxx=std::max(mxx,b);mny=std::min(mny,c);mxy=std::max(mxy,d);
        }
    }
    outL = !strcmp(s.hull,"schp")?originLayout(mnx,mxx,mny,mxy):makeLayout(pts,mnx,mxx,mny,mxy,s.floating);
    cache.emplace(key,outL);
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

int bodyFrames(UnitType t) {
    const char* stem=stemsOf(t).hull;if(!stem || !getVxl(stem))return 1;
    const auto* animation=getHva(stem);
    return animation?animation->nframes:0;
}

int turretFrames(UnitType t) {
    const char* stem=stemsOf(t).tur;if(!stem || !getVxl(stem))return 1;
    const auto* animation=getHva(stem);
    return animation?animation->nframes:0;
}

bool renderBody(UnitType t, int dir, int frame, PixBuf& out, const Color* houseColor,Vec2i* originAnchor) {
    init();
    if (!gPalOk) return false;
    Stems s = stemsOf(t);
    if (!s.hull) return false;
    if(t==UnitType::Destroyer && frame==1){s.hull="destwo";frame=0;}
    Layout L;
    if(t==UnitType::Destroyer && assemblyLayout(stemsOf(t),dir,L))
        return renderStem(s.hull,nullptr,dir,s.floating,&L,out,nullptr,houseColor,frame);
    // 有炮塔时用整车盒做画布，保证车体/炮塔同锚点且炮塔不被裁切
    if ((s.tur || bodyFrames(t)>1) && assemblyLayout(s, dir, L)) {
        if(originAnchor)*originAnchor={(int)L.orgx,(int)L.orgy};
        return renderStem(s.hull, nullptr, dir, s.floating, &L, out, nullptr, houseColor,frame);
    }
    return renderStem(s.hull, nullptr, dir, s.floating, nullptr, out, nullptr, houseColor,frame);
}

bool renderTurret(UnitType t, int dir, PixBuf& out, const Color* houseColor,int frame) {
    init();
    if (!gPalOk) return false;
    Stems s = stemsOf(t);
    if (!s.tur || !s.hull) return false;
    Layout L;
    if (!assemblyLayout(s, dir, L)) return false;
    return renderStem(s.tur, s.barl, dir, s.floating, &L, out, nullptr, houseColor,frame);
}

bool renderBodyFacing(UnitType t,int facing,int frame,PixBuf& out,Vec2i& anchor,const Color* houseColor) {
    init();const auto stems=stemsOf(t);Layout layout;
    if(!gPalOk || !fineAssemblyLayout(stems,layout))return false;
    anchor={(int)layout.orgx,(int)layout.orgy};
    return renderStemAt(stems.hull,nullptr,11.25f*((facing&31)-4),stems.floating,&layout,out,nullptr,houseColor,frame);
}

bool renderTurretFacing(UnitType t,int facing,int frame,PixBuf& out,Vec2i& anchor,const Color* houseColor,
                        std::optional<Vector2>* barrelTip) {
    if (barrelTip) barrelTip->reset();
    init();const auto stems=stemsOf(t);Layout layout;
    if(!gPalOk || !stems.tur || !fineAssemblyLayout(stems,layout))return false;
    anchor={(int)layout.orgx,(int)layout.orgy};
    const bool singleBarrel = t == UnitType::Grizzly || t == UnitType::Rhino || t == UnitType::MirageTank;
    return renderStemAt(stems.tur,stems.barl,11.25f*((facing&31)-4),stems.floating,&layout,out,nullptr,houseColor,frame,
                        singleBarrel ? barrelTip : nullptr);
}

int ifvFrames(const std::string& root,int turret) {
    const auto stem=root+(turret>=0?"tur"+(turret?std::to_string(turret):std::string{}):std::string{});
    const auto* animation=getHva(stem.c_str());
    return getVxl(stem.c_str()) && animation?animation->nframes:0;
}

bool renderIFV(const std::string& root,int turrets,int turret,int dir,int frame,
               PixBuf& out,Vec2i& anchor,const Color* houseColor) {
    init();if(!gPalOk || turrets<1 || turrets>32 || turret < -1 || turret>=turrets)return false;
    static std::unordered_map<std::string,Layout> layouts;
    const auto key=root+":"+std::to_string(turrets);auto found=layouts.find(key);
    if(found==layouts.end()) {
        float minX=0,maxX=0,minY=0,maxY=0;bool any=false;
        for(int part=-1;part<turrets;++part) {
            const auto stem=root+(part>=0?"tur"+(part?std::to_string(part):std::string{}):std::string{});
            const int frames=ifvFrames(root,part);if(!frames)return false;
            for(int facing=0;facing<8;++facing)for(int phase=0;phase<frames;++phase) {
                std::vector<Pt> points;float a,b,c,d;
                if(!projectStemPts(stem.c_str(),voxelFacingDegrees(facing),points,a,b,c,d,phase))return false;
                minX=any?std::min(minX,a):a;maxX=any?std::max(maxX,b):b;
                minY=any?std::min(minY,c):c;maxY=any?std::max(maxY,d):d;any=true;
            }
        }
        const auto layout=originLayout(minX,maxX,minY,maxY);
        if(!any || layout.w<1 || layout.h<1 || layout.w>384 || layout.h>384)return false;
        found=layouts.emplace(key,layout).first;
    }
    const auto stem=root+(turret>=0?"tur"+(turret?std::to_string(turret):std::string{}):std::string{});
    const auto& layout=found->second;anchor={(int)layout.orgx,(int)layout.orgy};
    return renderStem(stem.c_str(),nullptr,dir&7,false,&layout,out,nullptr,houseColor,frame);
}

bool renderDeployed(UnitType t,int bodyDir,int turretDir,PixBuf& out,Vec2i& anchor,const Color* houseColor) {
    init();if(t!=UnitType::SiegeChopper || !gPalOk)return false;
    std::vector<Pt> points;float minX,maxX,minY,maxY;
    if(!projectStemPts("schd",voxelFacingDegrees(bodyDir),points,minX,maxX,minY,maxY))return false;
    if(!projectStemPts("schdtur",voxelFacingDegrees(turretDir),points,minX,maxX,minY,maxY))return false;
    const Layout layout=originLayout(minX,maxX,minY,maxY);
    if(layout.w<=0 || layout.h<=0 || layout.w>384 || layout.h>384)return false;
    anchor={(int)layout.orgx,(int)layout.orgy};
    rasterize(points,kScale,layout.orgx,layout.orgy,layout.w,layout.h,out,houseColor);
    return true;
}

bool renderUnload(UnitType t,int dir,PixBuf& out,Vec2i& anchor,const Color* houseColor) {
    init();const auto stems=stemsOf(t);Layout layout;
    if(!gPalOk || !stems.unload || !fineAssemblyLayout(stems,layout))return false;
    anchor={(int)layout.orgx,(int)layout.orgy};
    return renderStem(stems.unload,nullptr,dir,stems.floating,&layout,out,nullptr,houseColor);
}

} // namespace VxlRt
