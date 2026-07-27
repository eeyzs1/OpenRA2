#pragma once
#include "gfx/model3d.h"
#include "game/data.h"

// 建筑 3D 模型库：以游戏瓦片坐标系建模（x→屏幕右下，y→屏幕左下，z→上）
// 经仿射变换映射到渲染器模型空间后，由 m3Render(dir=0) 输出 RA2 式等距建筑图。
// 返回 false = 该建筑无 3D 模型（回退 2D 绘制）
bool buildBldModel3D(BldType t, M3Builder& b);
