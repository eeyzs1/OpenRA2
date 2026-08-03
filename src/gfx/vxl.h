#pragma once
#include "gfx/pixel.h"
#include "game/data.h"

// 运行时 VXL 体素渲染：从 assets/voxels/*.vxl + unittem.pal + voxels.vpl 绘制载具。
// 成功时写入 PixBuf（阵营色为纯红带，供 finishUnitSprite remap）；失败返回 false。
namespace VxlRt {

// 初始化：加载调色板 / VPL（可重复调用，幂等）
void init();

// 该单位是否有可用的车体 VXL（步兵 SHP 单位返回 false）
bool hasBody(UnitType t);

// 渲染车体 / 炮塔 / 卸货姿态。dir = 0..7（0=东，顺时针）
// 画布与锚点规则与 gen_assets 一致：地面单位南触点 ≈ 0.72h，炮塔与车体同坐标系。
bool renderBody(UnitType t, int dir, int frame, PixBuf& out);
bool renderTurret(UnitType t, int dir, PixBuf& out);
bool renderUnload(UnitType t, int dir, PixBuf& out);

} // namespace VxlRt
