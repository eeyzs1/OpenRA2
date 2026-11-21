#pragma once
#include "gfx/pixel.h"
#include "game/data.h"

// 运行时 VXL 体素渲染：从 assets/voxels/*.vxl + unittem.pal + voxels.vpl 绘制载具。
// 成功时写入 PixBuf（传 houseColor 时按索引换色，否则输出纯红占位带）；失败返回 false。
namespace VxlRt {

// 初始化：加载调色板 / VPL（可重复调用，幂等）
void init();

// 该单位是否有可用的车体 VXL（步兵 SHP 单位返回 false）
bool hasBody(UnitType t);
// Native HVA phase counts; 1 for a non-voxel part, 0 for invalid required HVA.
int bodyFrames(UnitType t);
int turretFrames(UnitType t);

// 渲染车体 / 炮塔 / 卸货姿态。dir = 0..7（0=东，顺时针）
// Multi-frame assemblies share a canvas and origin across all HVA phases.
// Ground contact is ~0.72h; body and turret use the same coordinate system.
bool renderBody(UnitType t, int dir, int frame, PixBuf& out, const Color* houseColor=nullptr,Vec2i* originAnchor=nullptr);
bool renderTurret(UnitType t, int dir, PixBuf& out, const Color* houseColor=nullptr, int frame=0);
// Fine vehicle poses: facing=4*dir at the existing eight headings, modulo32.
// Both parts return the same physical voxel origin across all headings/phases.
bool renderBodyFacing(UnitType t,int facing,int frame,PixBuf& out,Vec2i& anchor,const Color* houseColor=nullptr);
bool renderTurretFacing(UnitType t,int facing,int frame,PixBuf& out,Vec2i& anchor,const Color* houseColor=nullptr,
                        std::optional<Vector2>* barrelTip=nullptr);
bool renderUnload(UnitType t,int dir,PixBuf& out,Vec2i& anchor,const Color* houseColor=nullptr);
// Source gunner assemblies share a physical voxel origin across all numbered
// turrets, HVA phases and facings. root is a content stem without extension.
int ifvFrames(const std::string& root,int turret);
bool renderIFV(const std::string& root,int turrets,int turret,int dir,int frame,
               PixBuf& out,Vec2i& anchor,const Color* houseColor=nullptr);
// Ground pose combines independently aimed turret and hull with one depth buffer.
// The returned physical hull anchor is independent of turret direction.
bool renderDeployed(UnitType t,int bodyDir,int turretDir,PixBuf& out,Vec2i& anchor,const Color* houseColor=nullptr);

} // namespace VxlRt
