// 离线诊断：导出建筑模型四边形为 CSV，用 Python matplotlib 绘制三视图验证几何
#include "gfx/bldmodels.h"
#include "gfx/model3d.h"
#include <cstdio>

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "conyard";
    BldType t = BldType::ConYard;
    if (which[0] == 'p') t = BldType::PowerPlant;
    M3Builder mb;
    if (!buildBldModel3D(t, mb)) { printf("no model\n"); return 1; }
    FILE* f = fopen("quads.csv", "w");
    fprintf(f, "x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3,r,g,b\n");
    for (const M3Quad& q : mb.quads) {
        fprintf(f, "%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%d,%d,%d\n",
                q.v[0][0], q.v[0][1], q.v[0][2], q.v[1][0], q.v[1][1], q.v[1][2],
                q.v[2][0], q.v[2][1], q.v[2][2], q.v[3][0], q.v[3][1], q.v[3][2],
                q.color.r, q.color.g, q.color.b);
    }
    fclose(f);
    printf("dumped %zu quads\n", mb.quads.size());
    return 0;
}
