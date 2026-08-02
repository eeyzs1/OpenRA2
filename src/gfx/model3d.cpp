#include "gfx/model3d.h"
#include "core/util.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// 投影常量：仰角 30°（RA2 军棋式投影视角）
constexpr float SIN_E = 0.5f, COS_E = 0.8660254f;
// 光源（指向光源，模型-相机共用空间）：左上前方
constexpr float LX = -0.42f, LY = -0.50f, LZ = 0.759f; // 已归一
constexpr float AMB = 0.38f, DIFF = 0.95f; // 更强对比朗伯：明暗分界更锐利，接近 RA2 预渲染手绘质感

void rotZ(float& x, float& y, float cs, float sn) {
    float nx = x * cs - y * sn, ny = x * sn + y * cs;
    x = nx; y = ny;
}

} // namespace

// ---------------- 图元 ----------------
void M3Builder::hexa(const float c[8][3], Color col, uint32_t remapMask, uint32_t emitMask) {
    // 面：顶(4,5,6,7) 底(0,3,2,1) +x(1,2,6,5) -x(3,0,4,7) +y(2,3,7,6) -y(0,1,5,4)
    static const int F[6][4] = {
        {4, 5, 6, 7}, {0, 3, 2, 1}, {1, 2, 6, 5}, {3, 0, 4, 7}, {2, 3, 7, 6}, {0, 1, 5, 4}
    };
    for (int f = 0; f < 6; f++) {
        M3Quad q;
        q.color = col;
        q.part = part;
        q.flags = 0;
        if (remapMask & (1u << f)) q.flags |= M3F_REMAP;
        if (emitMask & (1u << f)) q.flags |= M3F_EMIT;
        // 面法线 = (v1-v0)×(v3-v0)
        const int* iv = F[f];
        for (int k = 0; k < 4; k++) memcpy(q.v[k], c[iv[k]], 3 * sizeof(float));
        float e1[3] = {q.v[1][0] - q.v[0][0], q.v[1][1] - q.v[0][1], q.v[1][2] - q.v[0][2]};
        float e2[3] = {q.v[3][0] - q.v[0][0], q.v[3][1] - q.v[0][1], q.v[3][2] - q.v[0][2]};
        float nx = e1[1] * e2[2] - e1[2] * e2[1];
        float ny = e1[2] * e2[0] - e1[0] * e2[2];
        float nz = e1[0] * e2[1] - e1[1] * e2[0];
        float l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l < 1e-6f) continue;
        nx /= l; ny /= l; nz /= l;
        // 保证朝外（与几何中心反向检测）
        float cc[3] = {0, 0, 0};
        for (int k = 0; k < 8; k++) { cc[0] += c[k][0]; cc[1] += c[k][1]; cc[2] += c[k][2]; }
        cc[0] /= 8; cc[1] /= 8; cc[2] /= 8;
        float out = (q.v[0][0] - cc[0]) * nx + (q.v[0][1] - cc[1]) * ny + (q.v[0][2] - cc[2]) * nz;
        if (out < 0) { nx = -nx; ny = -ny; nz = -nz; }
        for (int k = 0; k < 4; k++) { q.n[k][0] = nx; q.n[k][1] = ny; q.n[k][2] = nz; }
        quads.push_back(q);
    }
}

void M3Builder::box(float cx, float cy, float cz, float sx, float sy, float sz, Color col,
                    uint32_t remapMask, uint32_t emitMask) {
    float x0 = cx - sx / 2, x1 = cx + sx / 2;
    float y0 = cy - sy / 2, y1 = cy + sy / 2;
    float z0 = cz - sz / 2, z1 = cz + sz / 2;
    float c[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                     {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    hexa(c, col, remapMask, emitMask);
}

void M3Builder::rbox(float cx, float cy, float cz, float sx, float sy, float sz, float yaw, Color col,
                     uint32_t remapMask, uint32_t emitMask) {
    float cs = cosf(yaw), sn = sinf(yaw);
    float hx = sx / 2, hy = sy / 2, hz = sz / 2;
    auto corner = [&](int xi, int yi, int zi, float out[3]) {
        float lx = xi ? hx : -hx, ly = yi ? hy : -hy;
        out[0] = cx + lx * cs - ly * sn;
        out[1] = cy + lx * sn + ly * cs;
        out[2] = cz + (zi ? hz : -hz);
    };
    float c[8][3];
    corner(0, 0, 0, c[0]); corner(1, 0, 0, c[1]); corner(1, 1, 0, c[2]); corner(0, 1, 0, c[3]);
    corner(0, 0, 1, c[4]); corner(1, 0, 1, c[5]); corner(1, 1, 1, c[6]); corner(0, 1, 1, c[7]);
    hexa(c, col, remapMask, emitMask);
}

void M3Builder::wedge(float x0, float x1, float hw, float z0, float zT0, float zT1, Color col,
                      uint32_t remapMask, uint32_t emitMask) {
    float c[8][3] = {{x0, -hw, z0}, {x1, -hw, z0}, {x1, hw, z0}, {x0, hw, z0},
                     {x0, -hw, zT0}, {x1, -hw, zT1}, {x1, hw, zT1}, {x0, hw, zT0}};
    hexa(c, col, remapMask, emitMask);
}

void M3Builder::cylZ(float cx, float cy, float cz, float r, float h, Color col, int seg,
                     bool remapTop, bool emitTop) {
    // 侧面（平滑法线）
    for (int i = 0; i < seg; i++) {
        float a0 = (float)i / seg * 6.2831853f, a1 = (float)(i + 1) / seg * 6.2831853f;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        M3Quad q;
        q.color = col; q.part = part; q.flags = 0;
        q.v[0][0] = cx + c0 * r; q.v[0][1] = cy + s0 * r; q.v[0][2] = cz;
        q.v[1][0] = cx + c1 * r; q.v[1][1] = cy + s1 * r; q.v[1][2] = cz;
        q.v[2][0] = cx + c1 * r; q.v[2][1] = cy + s1 * r; q.v[2][2] = cz + h;
        q.v[3][0] = cx + c0 * r; q.v[3][1] = cy + s0 * r; q.v[3][2] = cz + h;
        q.n[0][0] = c0; q.n[0][1] = s0; q.n[0][2] = 0;
        q.n[1][0] = c1; q.n[1][1] = s1; q.n[1][2] = 0;
        q.n[2][0] = c1; q.n[2][1] = s1; q.n[2][2] = 0;
        q.n[3][0] = c0; q.n[3][1] = s0; q.n[3][2] = 0;
        quads.push_back(q);
    }
    // 顶面扇（平法线 +z）
    for (int i = 0; i < seg; i++) {
        float a0 = (float)i / seg * 6.2831853f, a1 = (float)(i + 1) / seg * 6.2831853f;
        float am = (a0 + a1) / 2;
        M3Quad q;
        q.color = col; q.part = part;
        q.flags = (remapTop ? M3F_REMAP : 0) | (emitTop ? M3F_EMIT : 0);
        q.v[0][0] = cx; q.v[0][1] = cy; q.v[0][2] = cz + h;
        q.v[1][0] = cx + cosf(a0) * r; q.v[1][1] = cy + sinf(a0) * r; q.v[1][2] = cz + h;
        q.v[2][0] = cx + cosf(am) * r; q.v[2][1] = cy + sinf(am) * r; q.v[2][2] = cz + h;
        q.v[3][0] = cx + cosf(a1) * r; q.v[3][1] = cy + sinf(a1) * r; q.v[3][2] = cz + h;
        for (int k = 0; k < 4; k++) { q.n[k][0] = 0; q.n[k][1] = 0; q.n[k][2] = 1; }
        quads.push_back(q);
    }
}

void M3Builder::cylXY(float cx, float cy, float cz, float r, float len, int axis, Color col, int seg,
                      bool emit) {
    // 起端圆心 (cx,cy,cz)，向 axis 正向（len<0 则反向）延伸
    for (int i = 0; i < seg; i++) {
        float a0 = (float)i / seg * 6.2831853f, a1 = (float)(i + 1) / seg * 6.2831853f;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        M3Quad q;
        q.color = col; q.part = part; q.flags = emit ? M3F_EMIT : 0;
        float p0[3], p1[3], p2[3], p3[3], n0[3], n1[3];
        if (axis == 0) {
            p0[0] = cx;       p0[1] = cy + c0 * r; p0[2] = cz + s0 * r;
            p1[0] = cx;       p1[1] = cy + c1 * r; p1[2] = cz + s1 * r;
            p2[0] = cx + len; p2[1] = cy + c1 * r; p2[2] = cz + s1 * r;
            p3[0] = cx + len; p3[1] = cy + c0 * r; p3[2] = cz + s0 * r;
            n0[0] = 0; n0[1] = c0; n0[2] = s0;
            n1[0] = 0; n1[1] = c1; n1[2] = s1;
        } else {
            p0[0] = cx + c0 * r; p0[1] = cy;       p0[2] = cz + s0 * r;
            p1[0] = cx + c1 * r; p1[1] = cy;       p1[2] = cz + s1 * r;
            p2[0] = cx + c1 * r; p2[1] = cy + len; p2[2] = cz + s1 * r;
            p3[0] = cx + c0 * r; p3[1] = cy + len; p3[2] = cz + s0 * r;
            n0[0] = c0; n0[1] = 0; n0[2] = s0;
            n1[0] = c1; n1[1] = 0; n1[2] = s1;
        }
        memcpy(q.v[0], p0, 12); memcpy(q.v[1], p1, 12); memcpy(q.v[2], p2, 12); memcpy(q.v[3], p3, 12);
        memcpy(q.n[0], n0, 12); memcpy(q.n[1], n1, 12); memcpy(q.n[2], n1, 12); memcpy(q.n[3], n0, 12);
        quads.push_back(q);
    }
    // 末端盖（平法线朝轴向）
    float ex = axis == 0 ? cx + len : cx, ey = axis == 1 ? cy + len : cy;
    for (int i = 0; i < seg; i++) {
        float a0 = (float)i / seg * 6.2831853f, a1 = (float)(i + 1) / seg * 6.2831853f;
        M3Quad q;
        q.color = col; q.part = part; q.flags = emit ? M3F_EMIT : 0;
        float s = len >= 0 ? 1.0f : -1.0f;
        float nx = axis == 0 ? s : 0, ny = axis == 1 ? s : 0;
        q.v[0][0] = ex; q.v[0][1] = ey; q.v[0][2] = cz;
        if (axis == 0) {
            q.v[1][0] = ex; q.v[1][1] = ey + cosf(a0) * r; q.v[1][2] = cz + sinf(a0) * r;
            q.v[2][0] = ex; q.v[2][1] = ey + cosf((a0 + a1) / 2) * r; q.v[2][2] = cz + sinf((a0 + a1) / 2) * r;
            q.v[3][0] = ex; q.v[3][1] = ey + cosf(a1) * r; q.v[3][2] = cz + sinf(a1) * r;
        } else {
            q.v[1][0] = ex + cosf(a0) * r; q.v[1][1] = ey; q.v[1][2] = cz + sinf(a0) * r;
            q.v[2][0] = ex + cosf((a0 + a1) / 2) * r; q.v[2][1] = ey; q.v[2][2] = cz + sinf((a0 + a1) / 2) * r;
            q.v[3][0] = ex + cosf(a1) * r; q.v[3][1] = ey; q.v[3][2] = cz + sinf(a1) * r;
        }
        for (int k = 0; k < 4; k++) { q.n[k][0] = nx; q.n[k][1] = ny; q.n[k][2] = 0; }
        quads.push_back(q);
    }
}

void M3Builder::ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, Color col,
                          int lonSeg, int latSeg) {
    // 纬带四边形 + 顶点法线（球面法线经缩放近似）
    for (int la = 0; la < latSeg; la++) {
        float p0 = -1.5707963f + (float)la / latSeg * 3.14159265f;
        float p1 = -1.5707963f + (float)(la + 1) / latSeg * 3.14159265f;
        for (int lo = 0; lo < lonSeg; lo++) {
            float t0 = (float)lo / lonSeg * 6.2831853f;
            float t1 = (float)(lo + 1) / lonSeg * 6.2831853f;
            M3Quad q;
            q.color = col; q.part = part; q.flags = 0;
            auto vtx = [&](float th, float ph, float out[3], float nrm[3]) {
                float cp = cosf(ph), sp = sinf(ph);
                out[0] = cx + rx * cp * cosf(th);
                out[1] = cy + ry * cp * sinf(th);
                out[2] = cz + rz * sp;
                float nx = cp * cosf(th) / rx, ny = cp * sinf(th) / ry, nz = sp / rz;
                float l = sqrtf(nx * nx + ny * ny + nz * nz);
                nrm[0] = nx / l; nrm[1] = ny / l; nrm[2] = nz / l;
            };
            vtx(t0, p0, q.v[0], q.n[0]);
            vtx(t1, p0, q.v[1], q.n[1]);
            vtx(t1, p1, q.v[2], q.n[2]);
            vtx(t0, p1, q.v[3], q.n[3]);
            quads.push_back(q);
        }
    }
}

void M3Builder::fin(const float v0[3], const float v1[3], const float v2[3], float th, Color col,
                    bool remap) {
    // 三角薄板：上下两面 + 三侧壁；v0,v1 前缘两点，v2 后缘点
    float t = th / 2;
    float bot[3][3], top[3][3];
    for (int k = 0; k < 3; k++) {
        bot[0][k] = v0[k]; bot[1][k] = v1[k]; bot[2][k] = v2[k];
        top[0][k] = v0[k]; top[1][k] = v1[k]; top[2][k] = v2[k];
    }
    bot[0][2] -= t; bot[1][2] -= t; bot[2][2] -= t;
    top[0][2] += t; top[1][2] += t; top[2][2] += t;
    // 顶/底三角（以退化四边形表示：顶点2重复）
    for (int f = 0; f < 2; f++) {
        M3Quad q;
        q.color = col; q.part = part; q.flags = remap ? M3F_REMAP : 0;
        auto src = f == 0 ? top : bot;
        memcpy(q.v[0], src[0], 12); memcpy(q.v[1], src[1], 12);
        memcpy(q.v[2], src[2], 12); memcpy(q.v[3], src[2], 12);
        float nz = f == 0 ? 1.0f : -1.0f;
        for (int k = 0; k < 4; k++) { q.n[k][0] = 0; q.n[k][1] = 0; q.n[k][2] = nz; }
        quads.push_back(q);
    }
    // 侧壁（三条边）
    for (int e = 0; e < 3; e++) {
        int a = e, b = (e + 1) % 3;
        M3Quad q;
        q.color = col; q.part = part; q.flags = remap ? M3F_REMAP : 0;
        memcpy(q.v[0], bot[a], 12); memcpy(q.v[1], bot[b], 12);
        memcpy(q.v[2], top[b], 12); memcpy(q.v[3], top[a], 12);
        float e1[3] = {q.v[1][0] - q.v[0][0], q.v[1][1] - q.v[0][1], q.v[1][2] - q.v[0][2]};
        float e2[3] = {q.v[3][0] - q.v[0][0], q.v[3][1] - q.v[0][1], q.v[3][2] - q.v[0][2]};
        float nx = e1[1] * e2[2] - e1[2] * e2[1];
        float ny = e1[2] * e2[0] - e1[0] * e2[2];
        float nzz = e1[0] * e2[1] - e1[1] * e2[0];
        float l = sqrtf(nx * nx + ny * ny + nzz * nzz);
        if (l < 1e-6f) continue;
        for (int k = 0; k < 4; k++) { q.n[k][0] = nx / l; q.n[k][1] = ny / l; q.n[k][2] = nzz / l; }
        quads.push_back(q);
    }
}

// ---------------- 渲染 ----------------
PixBuf m3Render(const std::vector<M3Quad>& quads, int dir, int outW, int outH, float gy,
                uint8_t partFilter, float pivX, float pivY, float scale) {
    // 4x 超采样：单位/建筑在屏幕上占比小，4x 能显著消除锯齿，输出更细腻的预渲染质感
    constexpr int SS = 4;
    int W = outW * SS, H = outH * SS;
    PixBuf hi(W, H);
    std::vector<float> zb((size_t)W * H, 1e30f);
    // dir 为屏幕空间朝向（0=东，顺时针）；模型 +X 为车头，投影 x 右 y 上前，
    // 屏幕顺时针 = 模型 XY 平面负角度旋转
    float ang = -dir * 3.14159265f / 4.0f;
    float cs = cosf(ang), sn = sinf(ang);
    float ox = W / 2.0f, oy = gy * SS;
    float pxs = SS * scale;

    for (const M3Quad& q0 : quads) {
        if (partFilter != 0xFF && q0.part != partFilter) continue;
        // 旋转 + 投影
        float sx[4], sy[4], sd[4], snx[4], sny[4], snz[4];
        for (int k = 0; k < 4; k++) {
            float x = q0.v[k][0] - pivX, y = q0.v[k][1] - pivY, z = q0.v[k][2];
            rotZ(x, y, cs, sn);
            x += pivX; y += pivY;
            sx[k] = ox + x * pxs;
            sy[k] = oy + (-y * SIN_E - z * COS_E) * pxs;
            sd[k] = y * COS_E - z * SIN_E; // 视深（大=远）
            float nx = q0.n[k][0], ny = q0.n[k][1];
            rotZ(nx, ny, cs, sn);
            snx[k] = nx; sny[k] = ny; snz[k] = q0.n[k][2];
        }
        // 拆三角 (0,1,2) / (0,2,3)
        for (int tri = 0; tri < 2; tri++) {
            int i0 = 0, i1 = tri == 0 ? 1 : 2, i2 = tri == 0 ? 2 : 3;
            float ax = sx[i0], ay = sy[i0], bx = sx[i1], by = sy[i1], cx2 = sx[i2], cy2 = sy[i2];
            float area = (bx - ax) * (cy2 - ay) - (cx2 - ax) * (by - ay);
            if (fabsf(area) < 1e-6f) continue;
            int minX = std::max(0, (int)floorf(std::min(ax, std::min(bx, cx2))));
            int maxX = std::min(W - 1, (int)ceilf(std::max(ax, std::max(bx, cx2))));
            int minY = std::max(0, (int)floorf(std::min(ay, std::min(by, cy2))));
            int maxY = std::min(H - 1, (int)ceilf(std::max(ay, std::max(by, cy2))));
            for (int py = minY; py <= maxY; py++)
                for (int px = minX; px <= maxX; px++) {
                    float fx = px + 0.5f, fy = py + 0.5f;
                    // 重心坐标：wa/wb/wc 分别对应顶点 i0/i1/i2
                    // （面积比 λ_A=area(P,B,C)/area、λ_B=area(P,C,A)/area、λ_C=1-λ_A-λ_B）
                    float wa = ((bx - fx) * (cy2 - fy) - (cx2 - fx) * (by - fy)) / area;
                    float wb = ((cx2 - fx) * (ay - fy) - (ax - fx) * (cy2 - fy)) / area;
                    float wc = 1.0f - wa - wb;
                    if (wa < -0.001f || wb < -0.001f || wc < -0.001f) continue;
                    float depth = wa * sd[i0] + wb * sd[i1] + wc * sd[i2];
                    size_t zi = (size_t)py * W + px;
                    if (depth >= zb[zi]) continue;
                    zb[zi] = depth;
                    float nx = wa * snx[i0] + wb * snx[i1] + wc * snx[i2];
                    float ny = wa * sny[i0] + wb * sny[i1] + wc * sny[i2];
                    float nz = wa * snz[i0] + wb * snz[i1] + wc * snz[i2];
                    float nl = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nl < 1e-6f) continue;
                    nx /= nl; ny /= nl; nz /= nl;
                    Color c = q0.color;
                    Color out;
                    if (q0.flags & M3F_EMIT) {
                        out = c;
                    } else if (q0.flags & M3F_REMAP) {
                        // 阵营色面：保持红色系（r>150, g/b<90），亮度比例留给运行时 remap
                        float k = AMB + DIFF * std::max(0.0f, nx * LX + ny * LY + nz * LZ);
                        k = std::clamp(k, 0.62f, 1.0f);
                        out = Color{(uint8_t)(255 * k), (uint8_t)(26 * k), (uint8_t)(22 * k), 255};
                    } else {
                        float k = AMB + DIFF * std::max(0.0f, nx * LX + ny * LY + nz * LZ);
                        if (k > 1.28f) k = 1.28f;
                        // 表面微噪（确定性哈希，±4%）：打破纯平涂色的矢量感，
                        // 模拟 RA2 手绘预渲染素材的颗粒/笔触质感
                        uint32_t h = (uint32_t)px * 73856093u ^ (uint32_t)py * 19349663u;
                        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                        k *= 0.96f + 0.08f * ((h % 1024) / 1024.0f);
                        out = Color{(uint8_t)std::min(255, (int)(c.r * k)),
                                    (uint8_t)std::min(255, (int)(c.g * k)),
                                    (uint8_t)std::min(255, (int)(c.b * k)), 255};
                    }
                    hi.set(px, py, out);
                }
        }
    }
    // SS×SS 盒滤波降采样（覆盖比例作 alpha → 抗锯齿边缘）
    PixBuf lo(outW, outH);
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < SS; dy++)
                for (int dx = 0; dx < SS; dx++) {
                    Color c = hi.get(x * SS + dx, y * SS + dy);
                    if (c.a > 0) { r += c.r; g += c.g; b += c.b; n++; }
                }
            if (n) lo.set(x, y, Color{(uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n),
                                      (uint8_t)(255 * n / (SS * SS))});
        }
    return lo;
}
