#pragma once
#include "gfx/model3d.h"
#include "game/data.h"

// 构建单位 3D 模型（车辆/舰船/飞行器；返回 false = 该单位无 3D 模型，回退 2D 绘制）
// full：采矿车满载（货舱装矿）
// turretPivotX/Y：独立炮塔的旋转支点（模型空间坐标，供 m3Render 对齐转塔中心）
bool buildUnitModel3D(UnitType t, M3Builder& b, bool full,
                      float* turretPivotX = nullptr, float* turretPivotY = nullptr);

// 3D 渲染输出画布边长（与原 2D 版一致，保证锚点/图标/预览布局不变）
int unitCanvasSize3D(UnitType t);
// 地面原点 y（画布内，随单位类别微调使底盘落于逻辑点）
float unitGroundY3D(UnitType t);
// 模型→屏幕缩放（车体/炮塔渲染必须使用同一值，保证对齐）
float unitScale3D(UnitType t);
