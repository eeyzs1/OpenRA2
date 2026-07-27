#pragma once
#include "gfx/pixel.h"
#include <vector>

// ===================== 迷你盒式 3D 渲染器（RA2 预渲染风格） =====================
// 单位模型由图元拼装（模型空间：+X 车头/机头/舰艏，+Y 左舷，+Z 上，单位=屏幕像素），
// 正交投影（仰角 30°）+ 左上光源朗伯着色，逐像素深度测试，2x 超采样抗锯齿。
// 平直着色（盒体）与平滑着色（圆柱/椭球，顶点法线插值）自动区分。

struct M3Quad {
    float v[4][3];  // 顶点（模型空间）
    float n[4][3];  // 顶点法线（平直面=面法线×4；曲面=径向法线）
    Color color;    // 基色
    uint8_t part;   // 0=车体/船体/机体  1=炮塔
    uint8_t flags;  // bit0: 阵营色面（红色系渲染，运行时 remap）  bit1: 自发光（不受光照）
};

enum : uint8_t { M3F_REMAP = 1, M3F_EMIT = 2 };
enum : uint8_t { M3P_BODY = 0, M3P_TURRET = 1 };

// 六面体面序（hexa 角点约定：0-3 底面 (x0y0,x1y0,x1y1,x0y1)，4-7 顶面同序）
enum : uint32_t {
    M3FACE_TOP = 1u << 0, M3FACE_BOT = 1u << 1,
    M3FACE_PX = 1u << 2, M3FACE_NX = 1u << 3,
    M3FACE_PY = 1u << 4, M3FACE_NY = 1u << 5,
    M3FACE_ALL = 0x3Fu
};

class M3Builder {
public:
    std::vector<M3Quad> quads;
    uint8_t part = M3P_BODY;
    void setPart(uint8_t p) { part = p; }

    // 通用六面体：c[8] 角点（0-3 底，4-7 顶）；remapMask/emitMask 按 M3FACE_* 位
    void hexa(const float c[8][3], Color col, uint32_t remapMask = 0, uint32_t emitMask = 0);
    // 轴对齐盒：中心 (cx,cy,cz)，尺寸 (sx,sy,sz)
    void box(float cx, float cy, float cz, float sx, float sy, float sz, Color col,
             uint32_t remapMask = 0, uint32_t emitMask = 0);
    // 偏航旋转盒：绕 (cx,cy) 旋转 yaw 弧度（机翼/旋翼/条纹板）
    void rbox(float cx, float cy, float cz, float sx, float sy, float sz, float yaw, Color col,
              uint32_t remapMask = 0, uint32_t emitMask = 0);
    // 楔形块：x0..x1，y±hw，底 z0，顶面从 (x0,zT0) 斜到 (x1,zT1)（首上装甲/舰艏/机头）
    void wedge(float x0, float x1, float hw, float z0, float zT0, float zT1, Color col,
               uint32_t remapMask = 0, uint32_t emitMask = 0);
    // 竖直圆柱（炮塔/罐体/旋翼毂）：底面中心 (cx,cy,cz)，半径 r，高 h
    void cylZ(float cx, float cy, float cz, float r, float h, Color col, int seg = 12,
              bool remapTop = false, bool emitTop = false);
    // 水平圆柱：axis=0 沿 X（炮管/机身），axis=1 沿 Y；起点的端面圆心 (cx,cy,cz)，长 len（带方向符号）
    void cylXY(float cx, float cy, float cz, float r, float len, int axis, Color col, int seg = 8,
               bool emit = false);
    // 椭球（气囊/穹顶/整流罩）
    void ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, Color col,
                   int lonSeg = 12, int latSeg = 6);
    // 三角薄板（机翼/尾翼/鳍）：v0-v1 前缘，v2 后缘点，厚 th（上下对称）
    void fin(const float v0[3], const float v1[3], const float v2[3], float th, Color col,
             bool remap = false);
};

// 渲染一帧：dir 0..7（0=东，顺时针），输出 outW×outH；地面原点置于 (outW/2, gy)。
// partFilter：0xFF 全部，M3P_BODY 仅车体，M3P_TURRET 仅炮塔。
// pivX/pivY：旋转支点（仅炮塔渲染时用——炮塔绕自身转塔中心旋转，而非模型原点）
// scale：模型空间→屏幕像素缩放（>1 放大模型占比）
PixBuf m3Render(const std::vector<M3Quad>& quads, int dir, int outW, int outH, float gy,
                uint8_t partFilter = 0xFF, float pivX = 0.0f, float pivY = 0.0f, float scale = 1.0f);
