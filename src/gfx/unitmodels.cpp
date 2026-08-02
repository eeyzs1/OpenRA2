#include "gfx/unitmodels.h"
#include "core/util.h"

// ===================== 单位 3D 模型库（RA2 预渲染风格） =====================
// 模型空间：+X 车头/机头/舰艏，+Y 左舷，+Z 上，1 单位 = 1 屏幕像素。
// 全部单位以盒式图元拼装，由 m3Render 做 8 方向正交投影渲染。

namespace {

// ---- 阵营装甲基调 ----
const Color ALLIED{138, 146, 160};   // 盟军装甲：冷灰蓝
const Color ALLIED_DT{102, 108, 120};
const Color SOVIET{118, 108, 80};    // 苏联装甲：橄榄棕
const Color SOVIET_DT{86, 78, 58};
const Color CHINA{110, 120, 88};     // 中国装甲：松绿
const Color CHINA_DT{80, 88, 64};
const Color YURI{120, 60, 130};      // 尤里装甲：暗紫
const Color YURI_DT{90, 40, 100};
const Color DECK{70, 74, 82};        // 甲板/车顶
const Color WOOD{110, 96, 70};

// 履带底盘：左右履带 + 前后端收圆；remapFender=true 时加阵营色翼子板
void tracks(M3Builder& b, float halfLen, float yOff, float h, bool remapFender = true) {
    for (int s = -1; s <= 1; s += 2) {
        b.box(0, s * yOff, h / 2 + 0.5f, halfLen * 2 - 2, 4.4f, h, Pal::TRACK);
        b.box(halfLen - 1.4f, s * yOff, h / 2 + 0.3f, 2.6f, 4.4f, h * 0.7f, Pal::TRACK);
        b.box(-halfLen + 1.4f, s * yOff, h / 2 + 0.3f, 2.6f, 4.4f, h * 0.7f, Pal::TRACK);
        // 负重轮排（侧面深色圆墩列）
        for (int i = 0; i < 5; i++)
            b.cylXY(-halfLen + 4 + i * (halfLen * 2 - 8) / 4, s * (yOff + 2.3f), h / 2 + 0.4f,
                    h * 0.32f, 0.9f, 1, Pal::TRACK_HI, 6);
        if (remapFender) // 翼子板色条（阵营色细条纹，RA2 坦克标志性识别带）
            b.box(0, s * (yOff + 2.5f), h + 1.1f, halfLen * 2 - 5, 1.5f, 0.8f, Pal::REMAP, M3FACE_ALL);
    }
}

// 坦克车体：履带 + 主装甲盒 + 首上楔形 + 发动机舱格栅
void tankHull(M3Builder& b, float hl, float hw, float z0, float hh, Color col, Color dt) {
    b.box(0, 0, z0 + hh / 2, hl * 2, hw * 2, hh, col);
    // 首上装甲（前斜面）
    b.wedge(hl - 4.5f, hl + 2.5f, hw - 0.5f, z0, z0 + hh + 1.5f, z0 + 1.0f, col);
    // 车尾动力舱（略高）+ 格栅
    b.box(-hl + 3.5f, 0, z0 + hh + 0.8f, 7, hw * 1.6f, 1.6f, dt);
    b.box(-hl + 3.5f, 0, z0 + hh + 1.7f, 5.5f, hw * 1.3f, 0.5f, Pal::GUN);
    // 驾驶员舱盖 + 前灯
    b.box(hl - 6.5f, -hw * 0.35f, z0 + hh + 0.6f, 3, 3, 1.2f, dt);
    b.box(hl + 2.2f, -hw * 0.55f, z0 + 1.2f, 0.8f, 1.4f, 1.0f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
    b.box(hl + 2.2f, hw * 0.55f, z0 + 1.2f, 0.8f, 1.4f, 1.0f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
}

// 通用炮塔：座圈 + 塔体盒 + 尾舱 + 炮管 + 阵营色条
void tankTurret(M3Builder& b, float tx, float tz, float tw, float tl, float th,
                float gunLen, Color col, Color dt) {
    b.setPart(M3P_TURRET);
    b.cylZ(tx, 0, tz - 1.2f, tw * 0.72f, 1.6f, dt, 12);
    b.box(tx, 0, tz + th / 2, tl, tw, th, col);
    b.box(tx - tl / 2 + 1.5f, 0, tz + th / 2, 3, tw * 0.8f, th * 0.8f, dt); // 尾舱
    b.cylXY(tx + tl / 2 - 1, 0, tz + th * 0.55f, 1.0f, gunLen, 0, Pal::GUN, 8);
    b.box(tx + tl / 2 + gunLen - 2.5f, 0, tz + th * 0.55f, 2.2f, 1.9f, 1.9f, Pal::GUN); // 制退器
    b.box(tx, 0, tz + th + 0.5f, tl * 0.30f, tw * 0.30f, 0.9f, Pal::REMAP, M3FACE_ALL); // 顶阵营条
    b.box(tx + 1, -tw * 0.3f, tz + th + 1.0f, 2.4f, 2.4f, 0.9f, dt); // 车长舱盖
    b.setPart(M3P_BODY);
}

// 舰船体：主船体 + 尖艏 + 舰尾收束 + 阵营色水线
void shipHull(M3Builder& b, float hl, float hw, float freeboard, Color col, Color dt) {
    b.box(0, 0, freeboard / 2, hl * 2, hw * 2, freeboard, col);
    b.wedge(hl, hl + hw * 0.9f, hw, 0, freeboard, freeboard * 0.55f, col);   // 尖艏
    b.wedge(-hl - hw * 0.4f, -hl, hw, 0, freeboard * 0.7f, freeboard, col);  // 舰尾收束
    b.box(0, 0, freeboard + 0.4f, hl * 2 - 2, hw * 2 - 1.5f, 0.8f, dt);      // 甲板
    // 阵营色水线（两舷）
    for (int s = -1; s <= 1; s += 2)
        b.box(0, s * (hw + 0.15f), 1.4f, hl * 1.7f, 0.6f, 1.1f, Pal::REMAP, M3FACE_ALL);
    // 艏锚链孔
    b.box(hl + hw * 0.5f, 0, freeboard * 0.7f, 1.2f, 1.2f, 1.2f, Pal::GUN);
}

// 舰载雷达桅杆
void mast(M3Builder& b, float x, float z0, float h, bool radar) {
    b.box(x, 0, z0 + h / 2, 1.2f, 1.2f, h, Pal::GUN);
    if (radar) {
        b.box(x, 0, z0 + h + 0.8f, 3.6f, 0.9f, 1.4f, Color{170, 200, 235, 255}, 0, M3FACE_ALL);
        b.box(x, 0, z0 + h - 2.0f, 2.6f, 0.8f, 0.9f, Pal::GUN);
    } else {
        b.box(x, 0, z0 + h + 0.5f, 1.6f, 1.6f, 1.0f, Color{255, 120, 110, 255}, 0, M3FACE_ALL);
    }
}

// 喷气式战机：机身 + 座舱 + 后掠翼 + 尾翼 + 尾喷；canard=鸭翼（黑鹰）
void jet(M3Builder& b, Color col, Color dt, float scale, bool canard) {
    float L = 13 * scale;                 // 机身半长
    b.cylXY(-L, 0, 0, 2.1f * scale, L * 2, 0, col, 10);          // 机身
    b.ellipsoid(L + 1.5f * scale, 0, 0, 3.2f * scale, 1.7f * scale, 1.7f * scale, col, 8, 4); // 机头
    b.ellipsoid(3.5f * scale, 0, 1.8f * scale, 3.4f * scale, 1.5f * scale, 1.3f * scale,
                Pal::GLASS, 8, 4);                                        // 座舱盖
    for (int s = -1; s <= 1; s += 2) {
        float v0[3] = {4 * scale, s * 1.6f * scale, 0.4f};
        float v1[3] = {-3 * scale, s * 12.5f * scale, 0.4f};   // 后掠主翼
        float v2[3] = {-8 * scale, s * 1.6f * scale, 0.4f};
        b.fin(v0, v1, v2, 0.8f, dt);
        float w0[3] = {-8.5f * scale, s * 1.2f * scale, 0.6f}; // 平尾
        float w1[3] = {-11.5f * scale, s * 5.5f * scale, 0.6f};
        float w2[3] = {-12.5f * scale, s * 1.2f * scale, 0.6f};
        b.fin(w0, w1, w2, 0.7f, dt);
        b.box(-3.2f * scale, s * 12.0f * scale, 0.6f, 1.6f, 1.4f, 0.9f,
              Pal::REMAP, M3FACE_ALL);                          // 翼尖阵营标
        if (canard) {
            float c0[3] = {6.5f * scale, s * 1.4f * scale, 0.8f};
            float c1[3] = {3.5f * scale, s * 5.0f * scale, 0.8f};
            float c2[3] = {1.5f * scale, s * 1.4f * scale, 0.8f};
            b.fin(c0, c1, c2, 0.6f, dt);
        }
    }
    // 垂尾
    b.box(-10 * scale, 0, 3.2f * scale, 4.5f * scale, 0.8f, 4.6f * scale, dt);
    b.wedge(-12.5f * scale, -8 * scale, 0.4f, 5.5f * scale, 3.0f * scale, 8.2f * scale, dt);
    // 尾喷口（自发光橙点）
    b.cylXY(-L - 1.2f, 0, 0, 1.5f * scale, 1.6f, 0, Pal::GUN, 8);
    b.box(-L - 1.6f, 0, 0, 1.0f, 1.6f * scale, 1.6f * scale, Color{255, 170, 70, 255}, 0, M3FACE_ALL);
    // 机背阵营条
    b.box(-1, 0, 2.0f * scale, 8 * scale, 1.2f, 0.7f, Pal::REMAP, M3FACE_ALL);
}

// 货舱金矿（满载帧）：确定性散布的金块
void oreLoad(M3Builder& b, float x0, float x1, float hw, float z, int n, uint64_t seed) {
    Rng r(seed);
    for (int i = 0; i < n; i++) {
        float ox = x0 + (x1 - x0) * r.unit();
        float oy = (r.unit() * 2 - 1) * hw;
        float s = 1.2f + r.unit() * 1.4f;
        b.box(ox, oy, z + s * 0.4f, s, s, s * 0.8f, Pal::ORE_GOLD);
    }
}

} // namespace

int unitCanvasSize3D(UnitType t) {
    switch (t) {
        case UnitType::Apocalypse: case UnitType::MCV: case UnitType::BattleFortress: return 72;
        case UnitType::Harvester: case UnitType::ChronoMiner: case UnitType::WarMiner: return 64;
        case UnitType::Destroyer: case UnitType::Typhoon: case UnitType::Aegis:
        case UnitType::AmphTransport: case UnitType::Boomer: return 72;
        case UnitType::SeaScorpion: case UnitType::Squid: case UnitType::MasterMind: return 60;
        case UnitType::Dreadnought: case UnitType::AircraftCarrier: case UnitType::Kirov: return 84;
        case UnitType::Nighthawk: case UnitType::FloatingDisc: case UnitType::SiegeChopper: return 64;
        default: return 56;
    }
}

float unitGroundY3D(UnitType t) {
    const UnitDef& d = unitDef(t);
    int cs = unitCanvasSize3D(t);
    if (d.isAir()) return cs * 0.55f;
    if (d.isNaval()) return cs * 0.60f;
    return cs * 0.64f;
}

float unitScale3D(UnitType t) {
    const UnitDef& d = unitDef(t);
    if (d.isAir()) return 0.90f;
    if (d.isNaval()) return 0.82f;
    return 1.02f; // 地面载具：RA2 式饱满占比（高分辨率下模型尺寸不变，画布增大）
}

bool buildUnitModel3D(UnitType t, M3Builder& b, bool full, float* turretPivotX, float* turretPivotY) {
    float pvx = 0, pvy = 0;
    switch (t) {
        // ===================== 主战坦克 =====================
        case UnitType::Grizzly: { // 灰熊（盟）：轻巧斜面车体 + 单炮塔
            tracks(b, 11, 6.4f, 3.6f);
            tankHull(b, 10, 5.6f, 3.2f, 4.2f, ALLIED, ALLIED_DT);
            tankTurret(b, -0.5f, 8.0f, 7.5f, 9, 3.4f, 11, ALLIED, ALLIED_DT);
            break;
        }
        case UnitType::MirageTank: { // 幻影（盟）：低剪影迷彩
            tracks(b, 11, 6.4f, 3.6f);
            tankHull(b, 10, 5.6f, 3.2f, 4.0f, Color{92, 104, 72, 255}, Color{70, 84, 54, 255});
            // 迷彩块
            b.box(-4, 3.2f, 7.6f, 4, 3, 0.7f, Color{66, 80, 50, 255});
            b.box(2, -3.4f, 7.6f, 5, 2.6f, 0.7f, Color{66, 80, 50, 255});
            b.box(-1, -1, 7.7f, 3, 3, 0.7f, Color{112, 124, 88, 255});
            tankTurret(b, -0.5f, 7.8f, 7.0f, 8.5f, 3.0f, 11, Color{92, 104, 72, 255}, Color{70, 84, 54, 255});
            break;
        }
        case UnitType::Rhino: { // 犀牛（苏）：重型方车体 + 大炮塔
            tracks(b, 12.5f, 7.4f, 4.0f);
            tankHull(b, 11.5f, 6.4f, 3.6f, 5.0f, SOVIET, SOVIET_DT);
            tankTurret(b, -1, 9.2f, 9.0f, 10.5f, 4.0f, 13, SOVIET, SOVIET_DT);
            pvx = -1;
            break;
        }
        case UnitType::Type99: { // 99式（中）：低扁长车身 + 长身管制退器
            tracks(b, 13.5f, 7.6f, 4.0f);
            tankHull(b, 12.5f, 6.6f, 3.6f, 4.4f, CHINA, CHINA_DT);
            tankTurret(b, -1.5f, 8.6f, 8.6f, 10, 3.6f, 15, CHINA, CHINA_DT);
            pvx = -1.5f;
            break;
        }
        case UnitType::Apocalypse: { // 天启（苏）：四履带超重型 + 双管巨塔
            for (int s = -1; s <= 1; s += 2) {
                b.box(0, s * 9.0f, 2.6f, 33, 5.0f, 5.2f, Pal::TRACK);
                b.box(0, s * 11.6f, 5.4f, 28, 1.2f, 0.9f, Pal::REMAP, M3FACE_ALL); // 外侧色条
                for (int i = 0; i < 6; i++)
                    b.cylXY(-13 + i * 5.2f, s * 11.8f, 2.4f, 1.5f, 1.0f, 1, Pal::TRACK_HI, 6);
            }
            b.box(0, 0, 7.6f, 30, 14, 6.0f, Color{88, 74, 60, 255});
            b.wedge(11, 16.5f, 7, 4.5f, 11.0f, 6.0f, Color{88, 74, 60, 255}); // 首上
            b.box(-11, 0, 11.2f, 8, 12, 1.6f, Color{66, 54, 44, 255});       // 尾舱
            b.box(15.8f, -4.5f, 7.0f, 1.0f, 1.8f, 1.4f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
            b.box(15.8f, 4.5f, 7.0f, 1.0f, 1.8f, 1.4f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
            b.setPart(M3P_TURRET);
            b.cylZ(-2, 0, 10.4f, 7.5f, 2.0f, Color{66, 54, 44, 255}, 14);
            b.box(-2, 0, 13.4f, 14, 12.5f, 4.4f, Color{96, 80, 64, 255});
            b.box(-8, 0, 13.2f, 4, 10, 3.6f, Color{66, 54, 44, 255});
            for (int s = -1; s <= 1; s += 2) { // 双联炮管
                b.cylXY(4.5f, s * 3.0f, 13.6f, 1.2f, 16, 0, Pal::GUN, 8);
                b.box(18.5f, s * 3.0f, 13.6f, 2.6f, 2.4f, 2.4f, Pal::GUN);
            }
            b.box(-2, 0, 16.0f, 5, 2.4f, 1.0f, Pal::REMAP, M3FACE_ALL);
            b.setPart(M3P_BODY);
            pvx = -2;
            break;
        }
        // ===================== 特色坦克 =====================
        case UnitType::PrismTank: { // 光棱（盟）：车体 + 棱镜水晶塔
            tracks(b, 10.5f, 6.2f, 3.4f);
            tankHull(b, 9.5f, 5.4f, 3.0f, 4.0f, ALLIED, ALLIED_DT);
            b.setPart(M3P_TURRET);
            b.cylZ(0, 0, 7.4f, 4.6f, 1.6f, ALLIED_DT, 12);
            b.box(0, 0, 9.0f, 7, 7, 2.0f, ALLIED);
            // 光棱水晶（自发光菱柱）
            b.rbox(0.5f, 0, 12.4f, 3.2f, 3.2f, 6.4f, 0.7853982f, Color{190, 235, 255, 255}, 0, M3FACE_ALL);
            b.box(0.5f, 0, 16.0f, 1.6f, 1.6f, 1.6f, Color{235, 250, 255, 255}, 0, M3FACE_ALL);
            b.box(-2.5f, 0, 11.4f, 1.0f, 5.5f, 3.2f, ALLIED_DT); // 支架
            b.setPart(M3P_BODY);
            break;
        }
        case UnitType::TeslaTank: { // 磁暴（苏）：绝缘车体 + 磁暴球塔
            tracks(b, 10.5f, 6.4f, 3.6f);
            tankHull(b, 9.5f, 5.6f, 3.2f, 4.4f, SOVIET, SOVIET_DT);
            b.setPart(M3P_TURRET);
            b.cylZ(0, 0, 7.8f, 4.8f, 1.8f, SOVIET_DT, 12);
            b.box(0, 0, 9.4f, 7.5f, 7.5f, 2.2f, SOVIET);
            b.cylZ(0.5f, 0, 10.5f, 1.6f, 2.6f, Pal::GUN, 8);              // 绝缘柱
            b.ellipsoid(0.5f, 0, 14.6f, 3.0f, 3.0f, 3.0f, Color{120, 180, 255, 255}, 10, 5); // 磁暴球
            b.box(0.5f, 0, 14.6f, 1.2f, 1.2f, 1.2f, Color{225, 242, 255, 255}, 0, M3FACE_ALL);
            b.setPart(M3P_BODY);
            break;
        }
        case UnitType::TankDestroyer: { // 坦克杀手（德）：后高楔形战斗室 + 超长炮（固定）
            tracks(b, 12, 7.0f, 3.8f);
            b.box(-1, 0, 5.6f, 20, 11.5f, 4.0f, Color{96, 102, 110, 255});
            b.wedge(-11, 8, 5.75f, 7.6f, 12.6f, 8.4f, Color{88, 94, 102, 255}); // 后高前低战斗室
            b.wedge(8, 13.5f, 5.5f, 3.6f, 9.2f, 4.6f, Color{96, 102, 110, 255}); // 首上
            b.box(-9, 0, 13.0f, 3.5f, 3.2f, 1.2f, Pal::REMAP, M3FACE_ALL);        // 顶阵营条
            b.cylXY(6, 0, 9.8f, 1.1f, 20, 0, Pal::GUN, 8);                       // 超长身管
            b.box(24.5f, 0, 9.8f, 3.0f, 2.4f, 2.4f, Pal::GUN);                   // 制退器
            b.box(-6, -3.5f, 12.8f, 2.6f, 2.6f, 0.9f, Color{70, 76, 84, 255});   // 舱盖
            break;
        }
        case UnitType::RobotTank: { // 遥控坦克（盟）：悬浮气垫 + 天线
            b.ellipsoid(0, 0, 2.2f, 10, 5.4f, 2.2f, Color{40, 44, 52, 255});     // 气垫裙
            b.box(0, 0, 5.4f, 17, 9, 3.4f, Color{120, 128, 140, 255});           // 悬浮车体
            b.wedge(7.5f, 11.5f, 4.5f, 3.8f, 7.4f, 5.0f, Color{120, 128, 140, 255});
            b.box(-1, 0, 7.5f, 10, 2.0f, 0.8f, Pal::REMAP, M3FACE_ALL);
            b.cylZ(-6, 0, 7.2f, 0.5f, 6.5f, Pal::GUN, 6);                        // 遥控天线
            b.box(-6, 0, 14.0f, 1.2f, 1.2f, 1.2f, Color{255, 110, 90, 255}, 0, M3FACE_ALL);
            b.setPart(M3P_TURRET);
            b.cylZ(2, 0, 7.4f, 3.0f, 1.2f, ALLIED_DT, 10);
            b.box(2, 0, 8.8f, 5, 5, 2.0f, Color{110, 118, 132, 255});
            b.cylXY(4.2f, 0, 9.0f, 0.8f, 8, 0, Pal::GUN, 8);
            b.setPart(M3P_BODY);
            pvx = 2;
            break;
        }
        case UnitType::BattleFortress: { // 战斗要塞（盟）：四履带移动堡垒
            for (int s = -1; s <= 1; s += 2) {
                b.box(0, s * 8.6f, 2.6f, 32, 4.8f, 5.2f, Pal::TRACK);
                for (int i = 0; i < 6; i++)
                    b.cylXY(-12.5f + i * 5, s * 11.2f, 2.4f, 1.5f, 1.0f, 1, Pal::TRACK_HI, 6);
            }
            b.box(0, 0, 8.2f, 30, 13.5f, 7.0f, Color{104, 108, 116, 255});
            b.wedge(12, 17.5f, 6.75f, 4.5f, 12.0f, 6.5f, Color{96, 100, 108, 255}); // 首上
            b.box(-3, 0, 13.8f, 18, 11, 4.6f, Color{88, 92, 100, 255});            // 上层堡垒
            b.box(-3, 0, 16.4f, 12, 2.2f, 0.8f, Pal::REMAP, M3FACE_ALL);
            for (int s = -1; s <= 1; s += 2)                                        // 射击孔
                for (int i = 0; i < 3; i++)
                    b.box(-8 + i * 5.5f, s * 5.6f, 13.6f, 1.6f, 0.7f, 1.2f, Color{20, 22, 26, 255});
            b.box(6.5f, 0, 13.6f, 2.8f, 6, 2.2f, Pal::GLASS, 0, M3FACE_ALL);       // 观察窗
            b.box(16.8f, -4, 7.5f, 1.0f, 1.6f, 1.2f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
            b.box(16.8f, 4, 7.5f, 1.0f, 1.6f, 1.2f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
            break;
        }
        // ===================== 轻型车辆 =====================
        case UnitType::IFV: { // 多功能步兵车（盟）：轻甲 + 小导弹塔
            tracks(b, 9.5f, 5.6f, 3.2f);
            tankHull(b, 8.5f, 4.9f, 2.8f, 3.8f, Color{138, 144, 154, 255}, ALLIED_DT);
            b.box(5.5f, 0, 6.0f, 3.5f, 6.5f, 2.0f, Pal::GLASS, 0, M3FACE_ALL); // 风挡
            b.setPart(M3P_TURRET);
            b.cylZ(-1, 0, 7.0f, 3.2f, 1.2f, ALLIED_DT, 10);
            b.box(-1, 0, 8.4f, 5.5f, 5.5f, 1.8f, Color{118, 124, 134, 255});
            b.rbox(-0.5f, 0, 10.2f, 6.5f, 2.2f, 2.2f, 0, Color{150, 154, 162, 255}); // 导弹箱
            b.box(2.8f, 0, 10.2f, 0.9f, 1.6f, 1.6f, Color{200, 110, 90, 255});
            b.setPart(M3P_BODY);
            pvx = -1;
            break;
        }
        case UnitType::FlakTrack: { // 高射炮车（苏）：半履带 + 四联高炮
            tracks(b, 9.5f, 5.8f, 3.4f);
            tankHull(b, 8.5f, 5.0f, 3.0f, 3.6f, SOVIET, SOVIET_DT);
            b.setPart(M3P_TURRET);
            b.cylZ(-1.5f, 0, 6.8f, 3.8f, 1.4f, SOVIET_DT, 10);
            b.box(-1.5f, 0, 8.2f, 6, 6.5f, 2.0f, Color{88, 80, 60, 255});
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sz = 0; sz < 2; sz++) // 四联炮管
                    b.cylXY(1.0f, sy * 1.5f, 9.2f + sz * 1.6f, 0.55f, 9, 0, Pal::GUN, 6);
            b.box(1.0f, 0, 10.0f, 2.5f, 4.5f, 3.2f, Color{70, 64, 48, 255}); // 炮架
            b.setPart(M3P_BODY);
            pvx = -1.5f;
            break;
        }
        case UnitType::V3Launcher: { // V3 火箭车（苏）：卡车底盘 + 背部巨弹
            tracks(b, 10.5f, 6.0f, 3.4f);
            b.box(0, 0, 5.4f, 19, 9.5f, 4.0f, Color{100, 92, 70, 255});
            b.box(8, 0, 6.6f, 4, 8, 2.4f, Color{88, 80, 60, 255});          // 驾驶舱
            b.box(9.8f, 0, 6.6f, 0.9f, 6, 1.8f, Pal::GLASS, 0, M3FACE_ALL);
            b.box(0, 0, 7.8f, 15, 2.2f, 0.8f, Pal::REMAP, M3FACE_ALL);        // 发射轨色带
            b.rbox(-1, -2.2f, 9.0f, 16, 1.0f, 1.0f, 0, Pal::GUN);            // 双导轨
            b.rbox(-1, 2.2f, 9.0f, 16, 1.0f, 1.0f, 0, Pal::GUN);
            b.cylXY(-9, 0, 11.2f, 2.6f, 17, 0, Color{176, 92, 64, 255}, 10); // V3 弹体
            b.cylXY(8, 0, 11.2f, 2.0f, 3.5f, 0, Color{224, 214, 196, 255}, 8); // 弹头
            for (int s = -1; s <= 1; s += 2) {
                float v0[3] = {-9, 0, 11.2f}, v1[3] = {-12.5f, s * 3.2f, 11.2f}, v2[3] = {-12.0f, 0, 11.2f};
                b.fin(v0, v1, v2, 0.7f, Color{150, 76, 52, 255});            // 尾翼
            }
            break;
        }
        case UnitType::DemoTruck: { // 自爆卡车（利比亚）：民用卡车 + 核弹桶
            tracks(b, 10, 5.8f, 3.2f, false);
            b.box(1, 0, 5.2f, 17, 9, 3.6f, WOOD);
            b.box(7.5f, 0, 7.6f, 4.5f, 8.5f, 3.0f, Color{96, 84, 62, 255}); // 驾驶室
            b.box(9.6f, 0, 7.8f, 0.9f, 7, 2.0f, Pal::GLASS, 0, M3FACE_ALL);
            b.box(-4.5f, 0, 7.2f, 11, 8.5f, 1.0f, Color{88, 76, 56, 255});  // 货台
            for (int s = -1; s <= 1; s += 2)                                // 货台栏板
                b.box(-4.5f, s * 4.2f, 8.4f, 11, 0.7f, 2.4f, Color{88, 76, 56, 255});
            b.cylZ(-5, 0, 7.6f, 3.4f, 5.0f, Color{90, 130, 70, 255}, 12);   // 核弹桶
            b.cylZ(-5, 0, 12.6f, 2.6f, 0.9f, Color{60, 96, 46, 255}, 12);
            b.box(-5, 0, 10.4f, 2.6f, 0.7f, 2.6f, Color{240, 220, 60, 255}, 0, M3FACE_ALL); // 辐射标
            b.box(-4.5f, 0, 7.9f, 12, 1.6f, 0.6f, Pal::REMAP, M3FACE_ALL);  // 台沿阵营色
            break;
        }
        case UnitType::TerrorDrone: { // 恐怖机器人（苏）：机械蜘蛛
            b.ellipsoid(0, 0, 4.2f, 5.5f, 4.5f, 3.4f, Color{92, 96, 104, 255});
            b.ellipsoid(4.5f, 0, 4.6f, 2.6f, 2.4f, 2.4f, Color{70, 74, 82, 255}); // 头
            b.box(6.2f, -1.0f, 5.2f, 0.8f, 0.9f, 0.9f, Color{255, 110, 70, 255}, 0, M3FACE_ALL); // 眼
            b.box(6.2f, 1.0f, 5.2f, 0.8f, 0.9f, 0.9f, Color{255, 110, 70, 255}, 0, M3FACE_ALL);
            b.box(0, 0, 7.4f, 4, 3, 0.7f, Pal::REMAP, M3FACE_ALL);               // 顶阵营标
            for (int s = -1; s <= 1; s += 2)
                for (int i = 0; i < 2; i++) { // 四足（折节）
                    float bx = -2.5f + i * 5.5f;
                    b.rbox(bx, s * 5.2f, 4.6f, 1.1f, 4.5f, 1.1f, s * 0.35f, Color{70, 74, 82, 255});
                    b.box(bx + (i ? 1.5f : -1.5f), s * 7.6f, 1.8f, 1.0f, 1.0f, 3.6f, Color{56, 58, 66, 255});
                }
            break;
        }
        // ===================== 采矿车/基地车 =====================
        case UnitType::Harvester: { // 采矿车（中）：重载底盘 + 开放式货舱
            tracks(b, 15, 8.2f, 4.2f);
            b.box(0, 0, 6.6f, 26, 12.5f, 5.0f, Color{110, 104, 88, 255});
            b.wedge(11, 16, 6.2f, 4, 9.4f, 5.4f, Color{110, 104, 88, 255});
            b.box(-4, 0, 10.0f, 15, 11, 1.6f, Color{70, 66, 54, 255});       // 货舱底
            for (int s = -1; s <= 1; s += 2) {                               // 舱帮
                b.box(-4, s * 5.5f, 11.6f, 15, 1.0f, 3.2f, Color{88, 82, 68, 255});
                b.box(-4, s * 6.1f, 13.0f, 14, 0.6f, 0.8f, Pal::REMAP, M3FACE_ALL);
            }
            b.box(-11.5f, 0, 11.6f, 1.2f, 11, 3.2f, Color{88, 82, 68, 255}); // 后挡板
            if (full) oreLoad(b, -10, 2, 4.2f, 11.2f, 9, 31);
            b.box(10.5f, 0, 11.0f, 5, 8, 4.0f, Color{96, 90, 74, 255});      // 驾驶室
            b.box(12.8f, 0, 11.6f, 0.9f, 6.5f, 2.4f, Pal::GLASS, 0, M3FACE_ALL);
            b.box(13.2f, 0, 8.0f, 1.0f, 8, 1.2f, Color{230, 220, 160, 255}, 0, M3FACE_ALL);
            break;
        }
        case UnitType::ChronoMiner: { // 超时空采矿车（盟）：校平车体 + 超时空环
            tracks(b, 13, 7.4f, 3.8f);
            b.box(0, 0, 6.2f, 23, 11, 4.4f, Color{128, 134, 142, 255});
            b.wedge(9.5f, 14, 5.5f, 3.8f, 8.6f, 5.0f, Color{128, 134, 142, 255});
            b.box(-3.5f, 0, 9.2f, 13, 9.5f, 1.4f, Color{86, 92, 102, 255});  // 货舱
            for (int s = -1; s <= 1; s += 2)
                b.box(-3.5f, s * 4.75f, 10.6f, 13, 0.9f, 2.8f, Color{100, 106, 116, 255});
            if (full) oreLoad(b, -8.5f, 1.5f, 3.6f, 10.0f, 7, 77);
            b.box(9.5f, 0, 10.0f, 4, 7, 3.4f, Color{112, 118, 128, 255});    // 驾驶室
            b.box(11.3f, 0, 10.4f, 0.9f, 5.5f, 2.0f, Pal::GLASS, 0, M3FACE_ALL);
            // 超时空装置：车顶环 + 蓝光芯（自发光）
            b.cylZ(-1, 0, 12.4f, 3.4f, 1.0f, Color{70, 90, 120, 255}, 14);
            b.cylZ(-1, 0, 13.4f, 2.2f, 0.8f, Color{140, 200, 255, 255}, 12, false, true);
            b.box(-1, 0, 14.4f, 1.6f, 1.6f, 1.2f, Color{215, 242, 255, 255}, 0, M3FACE_ALL);
            b.box(0, 0, 8.7f, 14, 1.8f, 0.6f, Pal::REMAP, M3FACE_ALL);
            break;
        }
        case UnitType::WarMiner: { // 武装采矿车（苏）：大倾角货斗 + 机枪塔
            tracks(b, 16, 8.8f, 4.4f);
            b.box(0, 0, 7.0f, 28, 13.5f, 5.4f, Color{96, 84, 66, 255});
            b.wedge(12, 17.5f, 6.75f, 4.2f, 10.0f, 5.6f, Color{96, 84, 66, 255});
            b.wedge(-13, 3, 6.0f, 9.6f, 13.6f, 11.0f, Color{70, 60, 46, 255}); // 大倾角货斗
            for (int s = -1; s <= 1; s += 2) {
                b.box(-5, s * 6.0f, 12.2f, 16, 1.0f, 4.0f, Color{70, 60, 46, 255});
                b.box(-5, s * 6.6f, 14.0f, 15, 0.6f, 0.8f, Pal::REMAP, M3FACE_ALL);
            }
            if (full) oreLoad(b, -11, 1, 4.6f, 12.4f, 10, 53);
            b.box(11.5f, 0, 11.6f, 5, 8.5f, 4.4f, Color{84, 74, 58, 255});     // 驾驶室
            b.box(13.8f, 0, 12.0f, 0.9f, 7, 2.6f, Pal::GLASS, 0, M3FACE_ALL);
            b.cylZ(3, 0, 13.2f, 2.6f, 1.4f, SOVIET_DT, 10);                    // 机枪塔
            b.cylXY(5.2f, 0, 14.0f, 0.6f, 7, 0, Pal::GUN, 6);
            break;
        }
        case UnitType::MCV: { // 基地车：重型平板 + 集装箱货舱
            tracks(b, 17, 9.2f, 4.6f);
            b.box(0, 0, 7.4f, 30, 14, 5.6f, Pal::STEEL);
            b.wedge(13, 18.5f, 7, 4.4f, 10.4f, 6.0f, Pal::STEEL);
            b.box(-6, 0, 11.6f, 16, 12, 3.0f, Pal::STEEL_DARK);                // 货舱
            b.box(-6, 0, 13.4f, 14, 10, 0.9f, Color{70, 74, 82, 255});
            b.box(-6, 0, 10.4f, 16.5f, 2.0f, 0.7f, Pal::REMAP, M3FACE_ALL);   // 舱沿阵营色
            b.box(11, 0, 11.4f, 5.5f, 9, 4.4f, Color{86, 90, 100, 255});       // 驾驶室
            b.box(13.5f, 0, 11.8f, 0.9f, 7.5f, 2.6f, Pal::GLASS, 0, M3FACE_ALL);
            b.cylZ(-12, -4, 13.2f, 0.5f, 5.0f, Pal::GUN, 6);                   // 天线
            b.box(-12, -4, 18.4f, 1.1f, 1.1f, 1.1f, Color{255, 110, 90, 255}, 0, M3FACE_ALL);
            break;
        }
        // ===================== 海军 =====================
        case UnitType::Destroyer: { // 驱逐舰（盟）：前主炮 + 舰桥 + 雷达桅
            shipHull(b, 16, 6, 4.5f, Color{96, 110, 134, 255}, Color{70, 80, 100, 255});
            b.setPart(M3P_TURRET); // 前主炮（可旋转）
            b.cylZ(9, 0, 4.9f, 2.8f, 1.4f, Color{60, 68, 84, 255}, 12);
            b.box(9, 0, 6.2f, 4.5f, 4.5f, 2.0f, Color{82, 94, 116, 255});
            b.cylXY(11, 0, 6.6f, 0.7f, 9, 0, Pal::GUN, 8);
            b.setPart(M3P_BODY);
            b.box(-4, 0, 7.4f, 9, 8, 5.0f, Color{104, 118, 142, 255});        // 舰桥
            b.box(-2.5f, 0, 9.0f, 4, 8.6f, 1.6f, Pal::GLASS, 0, M3FACE_ALL);   // 舷窗带
            mast(b, -7, 9.9f, 7.0f, true);
            b.box(-12, 0, 6.2f, 4, 5, 3.0f, Color{88, 100, 122, 255});         // 尾舱（机库）
            b.box(0, 0, 5.6f, 20, 0.8f, 0.5f, Color{200, 205, 215, 255});      // 甲板中线
            pvx = 9;
            break;
        }
        case UnitType::Typhoon: { // 台风潜艇（苏）：低舷艇体 + 指挥塔围壳
            b.ellipsoid(0, 0, 2.4f, 15, 5, 3.2f, Color{56, 58, 72, 255});
            b.ellipsoid(15, 0, 2.2f, 4, 2.6f, 2.2f, Color{56, 58, 72, 255}, 8, 4); // 艏
            b.box(0, 0, 4.6f, 22, 6, 1.4f, Color{44, 46, 58, 255});                // 甲板
            b.box(-1, 0, 6.6f, 7, 3.6f, 3.4f, Color{74, 76, 92, 255});             // 围壳
            b.box(0.5f, 0, 8.6f, 3, 2, 1.0f, Pal::GLASS, 0, M3FACE_ALL);
            b.cylZ(-1.5f, 0, 8.3f, 0.5f, 4.0f, Pal::GUN, 6);                        // 潜望镜桅
            b.box(-1.5f, 0, 12.5f, 1.4f, 0.9f, 0.9f, Color{200, 220, 240, 255}, 0, M3FACE_ALL);
            for (int s = -1; s <= 1; s += 2) { // 尾舵
                float v0[3] = {-13, 0, 2.4f}, v1[3] = {-17, s * 3.6f, 2.4f}, v2[3] = {-17, 0, 2.4f};
                b.fin(v0, v1, v2, 0.8f, Color{48, 50, 62, 255});
            }
            b.box(3, 0, 5.6f, 10, 0.9f, 0.7f, Pal::REMAP, M3FACE_ALL);              // 舷侧阵营标
            break;
        }
        case UnitType::Aegis: { // 中华神盾舰（中）：相控阵 + 垂发井
            shipHull(b, 18, 7, 5.0f, Color{104, 116, 128, 255}, Color{76, 86, 98, 255});
            b.box(-3, 0, 8.6f, 11, 10, 6.0f, Color{112, 124, 136, 255});            // 舰桥
            for (int s = -1; s <= 1; s += 2) {                                      // 相控阵盾面（自发光）
                b.box(-2, s * 5.1f, 9.4f, 3.2f, 0.6f, 2.6f, Color{120, 200, 255, 255}, 0, M3FACE_ALL);
                b.box(-6.5f, s * 5.1f, 9.4f, 2.4f, 0.6f, 2.2f, Color{120, 200, 255, 255}, 0, M3FACE_ALL);
            }
            b.box(1.5f, 0, 10.6f, 3, 8, 1.4f, Pal::GLASS, 0, M3FACE_ALL);           // 舷窗
            mast(b, -8.5f, 11.6f, 6.5f, true);
            for (int i = 0; i < 2; i++)                                             // 前垂发井
                for (int j = 0; j < 3; j++)
                    b.box(8 + i * 4, -3 + j * 3, 5.6f, 2.6f, 2.2f, 0.8f, Color{52, 58, 68, 255});
            b.box(12, 0, 5.9f, 8, 0.8f, 0.5f, Color{200, 205, 215, 255});
            b.box(-13, 0, 6.4f, 5, 6, 2.0f, Color{88, 98, 110, 255});               // 直升机平台
            break;
        }
        case UnitType::SeaScorpion: { // 海蝎（苏中）：快艇 + 四联高炮
            shipHull(b, 11.5f, 4.6f, 3.6f, Color{100, 104, 96, 255}, Color{74, 78, 70, 255});
            b.box(-4.5f, 0, 5.6f, 5, 5.5f, 3.6f, Color{88, 92, 84, 255});           // 驾驶舱
            b.box(-3, 0, 6.6f, 1.6f, 5.8f, 1.4f, Pal::GLASS, 0, M3FACE_ALL);
            b.cylZ(5, 0, 4.0f, 2.6f, 1.2f, Color{66, 70, 62, 255}, 10);             // 炮座
            b.box(5, 0, 5.6f, 3.5f, 4, 2.4f, Color{80, 84, 76, 255});
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sz = 0; sz < 2; sz++)
                    b.cylXY(6.5f, sy * 1.1f, 6.2f + sz * 1.2f, 0.45f, 6.5f, 0, Pal::GUN, 6);
            break;
        }
        case UnitType::Dreadnought: { // 无畏级战舰（苏）：双联 V3 发射架
            shipHull(b, 21, 8, 5.5f, Color{88, 92, 104, 255}, Color{64, 68, 78, 255});
            b.box(-8, 0, 9.4f, 10, 11, 7.0f, Color{96, 100, 112, 255});             // 舰桥
            b.box(-5.5f, 0, 11.0f, 3.5f, 11.6f, 1.8f, Pal::GLASS, 0, M3FACE_ALL);
            mast(b, -12, 12.9f, 7.5f, true);
            for (int s = -1; s <= 1; s += 2) {                                      // 双导弹架
                b.box(8, s * 3.4f, 7.0f, 5, 4.5f, 3.0f, Color{70, 74, 84, 255});    // 基座
                b.cylXY(6, s * 3.4f, 10.0f, 2.2f, 13, 0, Color{176, 92, 64, 255}, 10);
                b.cylXY(19, s * 3.4f, 10.0f, 1.7f, 2.5f, 0, Color{224, 214, 196, 255}, 8);
            }
            b.box(-18, 0, 7.0f, 4, 6, 3.0f, Color{78, 82, 92, 255});                // 尾舱
            break;
        }
        case UnitType::AircraftCarrier: { // 航空母舰（盟）：全通甲板 + 右舷舰岛
            shipHull(b, 22, 9, 6.0f, Color{96, 104, 116, 255}, Color{70, 76, 88, 255});
            b.box(0, 0, 6.8f, 44, 16.5f, 1.6f, Color{72, 78, 88, 255});             // 飞行甲板
            b.wedge(22, 29.5f, 8.25f, 6.0f, 7.6f, 7.6f, Color{72, 78, 88, 255});
            b.box(0, 0, 7.7f, 38, 1.0f, 0.3f, Color{200, 200, 120, 255});           // 甲板中线
            b.box(0, -7.9f, 7.7f, 42, 0.7f, 0.4f, Color{120, 126, 136, 255});       // 舷侧边线
            b.box(0, 7.9f, 7.7f, 42, 0.7f, 0.4f, Color{120, 126, 136, 255});
            b.box(-4, -6.2f, 10.4f, 7, 4.5f, 5.6f, Color{104, 112, 124, 255});      // 舰岛
            b.box(-3, -7.8f, 11.0f, 3, 1.2f, 1.6f, Pal::GLASS, 0, M3FACE_ALL);
            mast(b, -6.5f, 13.2f, 5.5f, true);
            for (int i = 0; i < 3; i++) {                                           // 甲板舰载机
                float ax = 6 + i * 7;
                b.box(ax, 3.5f, 8.2f, 4.5f, 1.2f, 1.0f, Color{140, 146, 156, 255});
                b.box(ax - 0.5f, 3.5f, 8.3f, 1.6f, 4.5f, 0.5f, Color{120, 126, 136, 255});
            }
            b.box(20, 0, 7.9f, 3, 3, 0.4f, Pal::REMAP, M3FACE_ALL);                 // 艏部阵营徽
            break;
        }
        case UnitType::AmphTransport: { // 两栖运输船：方正登陆艇 + 艏门跳板
            shipHull(b, 13, 6.5f, 4.5f, Color{110, 112, 100, 255}, Color{82, 84, 74, 255});
            b.box(13.4f, 0, 3.0f, 1.2f, 9, 3.6f, Pal::STEEL_DARK);                  // 艏门
            b.box(13.9f, 0, 3.2f, 0.5f, 7, 2.4f, Color{50, 52, 46, 255});
            b.box(-3, 0, 6.6f, 14, 10.5f, 3.0f, Color{88, 90, 80, 255});            // 载员舱
            b.box(-3, 0, 8.3f, 11, 1.8f, 0.6f, Pal::REMAP, M3FACE_ALL);
            b.box(-10, 0, 8.0f, 4, 7, 5.0f, Color{96, 98, 88, 255});                // 驾驶舱
            b.box(-8.4f, 0, 9.4f, 0.9f, 5.5f, 2.0f, Pal::GLASS, 0, M3FACE_ALL);
            break;
        }
        case UnitType::Dolphin: { // 海豚（盟）：流线身 + 背鳍 + 阵营项圈
            b.ellipsoid(0, 0, 1.0f, 12, 4.2f, 3.6f, Color{150, 160, 172, 255});
            b.ellipsoid(1, 0, 0.0f, 10, 3.6f, 3.0f, Color{210, 216, 224, 255}, 10, 5); // 白腹
            b.cylXY(11, 0, 0.8f, 1.1f, 5, 0, Color{150, 160, 172, 255}, 8);            // 吻部
            {   // 背鳍
                float v0[3] = {2, 0, 4.0f}, v1[3] = {-3, 0, 4.0f}, v2[3] = {-1, 0, 8.6f};
                M3Quad fin;
                (void)fin;
                b.fin(v0, v2, v1, 1.0f, Color{130, 140, 152, 255});
            }
            for (int s = -1; s <= 1; s += 2) { // 尾鳍
                float v0[3] = {-11, 0, 1.0f}, v1[3] = {-16, s * 4.0f, 1.6f}, v2[3] = {-15.5f, 0, 1.2f};
                b.fin(v0, v1, v2, 0.9f, Color{130, 140, 152, 255});
            }
            b.box(6.5f, -1.6f, 2.6f, 0.8f, 0.8f, 0.8f, Color{20, 22, 26, 255});        // 眼
            b.box(4, 0, 4.0f, 2.0f, 8.6f, 0.9f, Pal::REMAP, M3FACE_ALL);               // 声呐项圈
            break;
        }
        case UnitType::Squid: { // 巨型乌贼（苏）：纺锤胴体 + 放射触手
            b.ellipsoid(1, 0, 1.6f, 8.5f, 4.2f, 3.8f, Color{88, 66, 92, 255});
            b.ellipsoid(4, 0, 2.2f, 5, 3.4f, 3.0f, Color{104, 80, 110, 255}, 10, 5);   // 头部受光
            b.box(7.5f, -2.2f, 2.8f, 1.2f, 1.2f, 1.2f, Color{230, 230, 240, 255}, 0, M3FACE_ALL); // 眼
            b.box(7.5f, 2.2f, 2.8f, 1.2f, 1.2f, 1.2f, Color{230, 230, 240, 255}, 0, M3FACE_ALL);
            for (int i = -2; i <= 2; i++) { // 尾部触手（扇形放射）
                float yaw = i * 0.38f;
                float len = i == 0 ? 10.0f : 8.5f;
                float cxr = -6 - cosf(yaw) * len / 2, cyr = sinf(yaw) * len / 2 + i * 1.2f;
                b.rbox(cxr, cyr, 1.2f, len, 1.3f, 1.3f, -yaw, Color{58, 42, 62, 255});
                b.box(cxr - cosf(yaw) * len / 2, cyr + sinf(yaw) * len / 2, 1.2f, 1.6f, 1.6f, 1.6f,
                      Color{88, 66, 92, 255});
            }
            for (int s = -1; s <= 1; s += 2) { // 两条长捕腕（前伸）
                b.rbox(10.5f, s * 2.2f, 1.4f, 8, 1.1f, 1.1f, s * 0.30f, Color{58, 42, 62, 255});
                b.box(14.6f, s * 3.4f, 1.4f, 1.5f, 1.5f, 1.5f, Color{88, 66, 92, 255});
            }
            b.box(0, 0, 5.6f, 6, 3, 0.7f, Pal::REMAP, M3FACE_ALL);                      // 顶阵营标
            break;
        }
        // ===================== 空军 =====================
        case UnitType::Intruder:   jet(b, Color{148, 158, 172, 255}, Color{108, 116, 130, 255}, 1.0f, false); break;
        case UnitType::MiG:        jet(b, Color{172, 170, 162, 255}, Color{128, 124, 116, 255}, 1.05f, false); break;
        case UnitType::Hornet:     jet(b, Color{126, 138, 152, 255}, Color{88, 98, 112, 255}, 0.85f, false); break;
        case UnitType::BlackEagle: jet(b, Color{74, 80, 92, 255}, Color{46, 50, 60, 255}, 1.0f, true); break;
        case UnitType::Kirov: { // 基洛夫空艇（苏）：巨型气囊 + 吊舱 + 尾翼
            b.ellipsoid(0, 0, 0, 24, 9.5f, 8.5f, Color{136, 110, 92, 255}, 16, 8);
            b.ellipsoid(20, 0, 0, 7, 5.5f, 5.0f, Color{136, 110, 92, 255}, 12, 6);     // 首部收拢
            b.ellipsoid(-22, 0, 0, 5, 4.5f, 4.0f, Color{120, 96, 80, 255}, 10, 5);     // 尾部
            for (int s = -1; s <= 1; s += 2) {                                          // 侧阵营条带
                b.rbox(2, s * 9.9f, 1.5f, 16, 0.8f, 3.0f, 0, Pal::REMAP, M3FACE_ALL);
                float v0[3] = {-20, 0, 0}, v1[3] = {-28, s * 7.5f, 0}, v2[3] = {-28, 0, 0}; // 水平尾翼
                b.fin(v0, v1, v2, 1.2f, Color{96, 76, 62, 255});
            }
            b.box(-24, 0, 5.5f, 6, 1.2f, 8, Color{96, 76, 62, 255});                    // 垂尾
            b.wedge(-27, -21, 0.6f, 9.0f, 5.5f, 12.5f, Color{96, 76, 62, 255});
            b.box(6, 0, -10.5f, 13, 5.5f, 4.0f, Pal::STEEL_DARK);                       // 吊舱
            b.box(11.5f, 0, -10.2f, 2.0f, 4.5f, 2.6f, Pal::GLASS, 0, M3FACE_ALL);       // 舱窗
            b.box(6, 0, -12.8f, 10, 4, 0.8f, Pal::REMAP, M3FACE_ALL);
            for (int s = -1; s <= 1; s += 2) {                                          // 侧挂引擎
                b.box(-4, s * 8.0f, -7.5f, 5, 3, 3, Color{70, 58, 48, 255});
                b.box(-7, s * 8.0f, -7.5f, 0.8f, 4.5f, 0.9f, Pal::GUN);                 // 螺旋桨
            }
            break;
        }
        case UnitType::Nighthawk: { // 夜鹰直升机（盟）：运直 + 旋翼 + 尾梁
            b.ellipsoid(2, 0, 0.5f, 10.5f, 5.0f, 4.6f, Color{88, 96, 84, 255}, 14, 7);
            b.ellipsoid(9.5f, 0, 1.2f, 4, 3.6f, 3.2f, Pal::GLASS, 10, 5);               // 座舱
            b.cylXY(-10, 0, 1.5f, 1.6f, -13, 0, Color{78, 86, 74, 255}, 8);             // 尾梁
            b.box(-23.5f, 0, 2.5f, 1.0f, 0.9f, 5.0f, Color{68, 76, 64, 255});           // 垂尾
            b.rbox(-23, 0, 3.0f, 0.7f, 6.5f, 0.9f, 0, Pal::GUN);                        // 尾桨
            b.cylZ(1, 0, 5.0f, 1.1f, 2.0f, Pal::GUN, 8);                                // 旋翼桅杆
            b.rbox(1, 0, 7.4f, 36, 1.6f, 0.7f, 0.35f, Color{44, 46, 42, 255});          // 主旋翼（X 交叉）
            b.rbox(1, 0, 7.4f, 36, 1.6f, 0.7f, 0.35f + 1.5707963f, Color{44, 46, 42, 255});
            b.box(1, 0, 7.4f, 2.0f, 2.0f, 1.2f, Pal::GUN);
            for (int s = -1; s <= 1; s += 2) {                                          // 起落橇
                b.box(2, s * 4.6f, -4.6f, 14, 1.0f, 0.9f, Pal::GUN);
                b.box(-1, s * 4.6f, -3.0f, 0.9f, 0.9f, 3.0f, Pal::GUN);
                b.box(6, s * 4.6f, -3.0f, 0.9f, 0.9f, 3.0f, Pal::GUN);
            }
            b.box(2, 0, 4.2f, 12, 1.0f, 0.7f, Pal::REMAP, M3FACE_ALL);                  // 顶阵营条
            b.cylXY(6, 3.5f, -1.0f, 0.5f, 6, 0, Pal::GUN, 6);                           // 舱门机枪
            break;
        }
        // ===================== 尤复阵营：尤里载具 =====================
        case UnitType::LasherTank: { // 狂风坦克（尤里）：轻巧有机曲线 + 单炮塔
            tracks(b, 11, 6.4f, 3.6f);
            tankHull(b, 10, 5.6f, 3.2f, 4.2f, YURI, YURI_DT);
            tankTurret(b, -0.5f, 8.0f, 7.5f, 9, 3.4f, 11, YURI, YURI_DT);
            pvx = -0.5f;
            break;
        }
        case UnitType::GatlingTank: { // 盖特坦克（尤里）：四联加特林炮塔
            tracks(b, 10.5f, 6.2f, 3.4f);
            tankHull(b, 9.5f, 5.6f, 3.0f, 3.8f, YURI, YURI_DT);
            b.setPart(M3P_TURRET);
            b.cylZ(-1, 0, 6.8f, 3.6f, 1.4f, YURI_DT, 10);
            b.box(-1, 0, 8.2f, 6, 6, 2.0f, YURI);                          // 回转体
            for (int sy = -1; sy <= 1; sy += 2)                            // 四联加特林炮管
                for (int sz = 0; sz < 2; sz++)
                    b.cylXY(1.0f, sy * 1.3f, 9.0f + sz * 1.4f, 0.5f, 8, 0, Pal::GUN, 6);
            b.box(1.0f, 0, 9.6f, 2.2f, 3.6f, 2.6f, Pal::GUN);             // 炮架
            b.box(-1, 0, 10.6f, 4, 0.8f, 0.6f, Pal::REMAP, M3FACE_ALL);   // 顶阵营条
            b.setPart(M3P_BODY);
            pvx = -1;
            break;
        }
        case UnitType::Magnetron: { // 磁电坦克（尤里）：悬浮反重力底盘 + 磁力吊臂
            // 悬浮底盘（无履带）
            b.ellipsoid(0, 0, 2.4f, 11, 5.6f, 2.2f, Color{40, 36, 46, 255});     // 反重力裙
            b.box(0, 0, 5.0f, 18, 9.5f, 3.6f, YURI);                            // 悬浮车体
            b.wedge(7.5f, 11.5f, 4.75f, 3.4f, 7.0f, 4.6f, YURI);                // 首上
            b.box(-1, 0, 7.2f, 12, 1.6f, 0.7f, Pal::REMAP, M3FACE_ALL);         // 顶阵营条
            // 磁力吊臂（炮塔）
            b.setPart(M3P_TURRET);
            b.cylZ(0, 0, 7.0f, 3.0f, 1.6f, YURI_DT, 10);                        // 底座
            b.box(0, 0, 8.4f, 5, 5, 2.0f, YURI);                                // 回转体
            b.cylZ(2, 0, 9.0f, 1.2f, 6.0f, Pal::GUN, 8);                        // 立柱吊臂
            b.ellipsoid(2, 0, 15.6f, 2.8f, 2.8f, 2.0f, Color{140, 110, 180, 255}, 10, 5); // 磁极头
            b.box(2, 0, 15.6f, 1.0f, 1.0f, 1.0f, Color{200, 160, 255, 255}, 0, M3FACE_ALL); // 磁极发光点
            b.setPart(M3P_BODY);
            break;
        }
        case UnitType::MasterMind: { // 主脑坦克（尤里）：脑穹顶 + 履带底盘
            tracks(b, 12, 7.0f, 3.8f);
            tankHull(b, 11, 6.2f, 3.4f, 4.2f, YURI, YURI_DT);
            // 巨型脑穹顶（炮塔）
            b.setPart(M3P_TURRET);
            b.cylZ(0, 0, 7.6f, 5.2f, 1.6f, YURI_DT, 12);                        // 底座
            b.ellipsoid(0, 0, 11.0f, 6.5f, 6.5f, 5.5f, Color{150, 90, 170, 255}, 14, 8); // 透明脑穹顶
            b.ellipsoid(-1.5f, -1.5f, 10.5f, 3.0f, 3.0f, 2.5f, Color{190, 140, 210, 255}, 10, 5); // 受光面
            // 脑沟纹（环形条纹）
            for (int i = 0; i < 3; i++)
                b.cylZ(0, 0, 9.0f + i * 1.6f, 6.0f - i * 0.6f, 0.4f, Color{110, 60, 130, 255}, 12);
            // 顶心灵宝石（自发光）
            b.cylZ(0, 0, 16.0f, 1.2f, 1.4f, Color{220, 160, 255, 255}, 8, false, true);
            b.setPart(M3P_BODY);
            break;
        }
        case UnitType::FloatingDisc: { // 飞碟（尤里）：碟形 UFO + 中央节点（空军）
            // 碟身（扁椭球）
            b.ellipsoid(0, 0, 0, 14, 14, 3.5f, YURI, 16, 6);
            b.ellipsoid(0, 0, 1.5f, 12, 12, 2.5f, Color{150, 90, 160, 255}, 14, 5); // 上盘受光
            // 中央穹顶（玻璃罩）
            b.ellipsoid(0, 0, 4.5f, 5.0f, 5.0f, 3.5f, Pal::GLASS, 12, 6);
            b.ellipsoid(-1, -1, 4.0f, 2.5f, 2.5f, 1.8f, Color{180, 230, 240, 255}, 10, 5);
            // 碟缘阵营色环
            b.cylZ(0, 0, 0.5f, 14.2f, 0.5f, Pal::REMAP, 16);
            // 底部绿光（自发光）
            b.cylZ(0, 0, -2.5f, 4.0f, 0.8f, Color{120, 255, 140, 255}, 10, false, true);
            // 底部电极（三根下伸）
            for (int i = 0; i < 3; i++) {
                float a = i * 2.0943951f;
                b.box(cosf(a) * 8, sinf(a) * 8, -3.5f, 1.0f, 1.0f, 3.0f, Pal::GUN);
            }
            break;
        }
        case UnitType::Boomer: { // 雷鸣潜艇（尤里）：低舷艇体 + 指挥塔 + 导弹舱（海军）
            b.ellipsoid(0, 0, 2.6f, 17, 5.4f, 3.4f, Color{50, 48, 64, 255});         // 艇体（比台风大）
            b.ellipsoid(17, 0, 2.4f, 4.5f, 2.8f, 2.4f, Color{50, 48, 64, 255}, 8, 4); // 艏
            b.box(0, 0, 4.8f, 26, 6.4f, 1.4f, Color{40, 38, 52, 255});               // 甲板
            // 指挥塔围壳（炮塔）
            b.setPart(M3P_TURRET);
            b.box(-2, 0, 6.6f, 8, 4.0f, 3.6f, Color{74, 70, 92, 255});               // 围壳
            b.box(-0.5f, 0, 8.6f, 3.5f, 2.2f, 1.0f, Pal::GLASS, 0, M3FACE_ALL);      // 舷窗
            b.cylZ(-2.5f, 0, 8.3f, 0.5f, 4.0f, Pal::GUN, 6);                         // 潜望镜桅
            b.box(-2.5f, 0, 12.5f, 1.4f, 0.9f, 0.9f, Color{200, 220, 240, 255}, 0, M3FACE_ALL);
            b.setPart(M3P_BODY);
            // 导弹发射舱（前部双联）
            for (int i = 0; i < 2; i++) {
                b.box(6 + i * 4, 0, 5.4f, 3.0f, 4.0f, 1.2f, Color{44, 42, 56, 255}); // 导弹舱口
                b.box(6 + i * 4, 0, 6.2f, 2.0f, 3.0f, 0.4f, Pal::GUN);               // 舱盖缝
            }
            for (int s = -1; s <= 1; s += 2) {                                       // 尾舵
                float v0[3] = {-15, 0, 2.6f}, v1[3] = {-20, s * 3.8f, 2.6f}, v2[3] = {-20, 0, 2.6f};
                b.fin(v0, v1, v2, 0.8f, Color{42, 40, 54, 255});
            }
            b.box(4, 0, 5.8f, 12, 0.9f, 0.7f, Pal::REMAP, M3FACE_ALL);               // 舷侧阵营标
            pvx = -2;
            break;
        }
        case UnitType::SiegeChopper: { // 攻城直升机（苏）：机身 + 旋翼 + 尾梁 + 攻城炮（部署态）
            Color scBody{70, 80, 56, 255}, scDark{48, 56, 38, 255}; // 苏联暗绿
            b.ellipsoid(2, 0, 0.5f, 10.5f, 5.0f, 4.6f, scBody, 14, 7);
            b.ellipsoid(9.5f, 0, 1.2f, 4, 3.6f, 3.2f, Pal::GLASS, 10, 5);            // 座舱
            b.cylXY(-10, 0, 1.5f, 1.6f, -13, 0, scDark, 8);                          // 尾梁
            b.box(-23.5f, 0, 2.5f, 1.0f, 0.9f, 5.0f, scDark);                        // 垂尾
            b.rbox(-23, 0, 3.0f, 0.7f, 6.5f, 0.9f, 0, Pal::GUN);                     // 尾桨
            b.cylZ(1, 0, 5.0f, 1.1f, 2.0f, Pal::GUN, 8);                             // 旋翼桅杆
            b.rbox(1, 0, 7.4f, 36, 1.6f, 0.7f, 0.35f, Color{44, 46, 42, 255});      // 主旋翼（X 交叉）
            b.rbox(1, 0, 7.4f, 36, 1.6f, 0.7f, 0.35f + 1.5707963f, Color{44, 46, 42, 255});
            b.box(1, 0, 7.4f, 2.0f, 2.0f, 1.2f, Pal::GUN);
            for (int s = -1; s <= 1; s += 2) {                                       // 起落橇
                b.box(2, s * 4.6f, -4.6f, 14, 1.0f, 0.9f, Pal::GUN);
                b.box(-1, s * 4.6f, -3.0f, 0.9f, 0.9f, 3.0f, Pal::GUN);
                b.box(6, s * 4.6f, -3.0f, 0.9f, 0.9f, 3.0f, Pal::GUN);
            }
            b.box(2, 0, 4.2f, 12, 1.0f, 0.7f, Pal::REMAP, M3FACE_ALL);               // 顶阵营条
            if (full) { // 部署态：攻城长炮展开
                b.cylXY(8, 0, -1.0f, 1.2f, 14, 0, Pal::GUN, 8);                      // 攻城长炮管
                b.box(15, 0, -1.0f, 2.6f, 2.4f, 2.4f, Pal::GUN);                     // 制退器
                b.box(6, 0, 0.5f, 4, 4, 2.0f, scDark);                               // 炮架
            }
            break;
        }
        case UnitType::ChaosDrone: { // 混乱无人机（尤里）：紧凑飞行器 + 毒气罐（空军）
            b.ellipsoid(0, 0, 0, 7.0f, 5.5f, 3.5f, YURI, 12, 6);                    // 主体
            b.ellipsoid(5.5f, 0, 0.5f, 3.0f, 2.6f, 2.4f, Color{100, 50, 110, 255}, 10, 5); // 机头
            b.box(5.5f, 0, 1.0f, 0.8f, 0.8f, 0.8f, Color{255, 110, 70, 255}, 0, M3FACE_ALL); // 红眼
            // 旋翼
            b.cylZ(0, 0, 3.5f, 0.6f, 1.4f, Pal::GUN, 6);                            // 旋翼桅杆
            b.rbox(0, 0, 5.2f, 16, 1.2f, 0.5f, 0.35f, Color{44, 46, 42, 255});     // 主旋翼（X 交叉）
            b.rbox(0, 0, 5.2f, 16, 1.2f, 0.5f, 0.35f + 1.5707963f, Color{44, 46, 42, 255});
            // 毒气罐（两侧）
            for (int s = -1; s <= 1; s += 2) {
                b.cylZ(-3, s * 4.5f, 1.0f, 1.4f, 3.5f, Color{90, 110, 70, 255}, 8); // 绿色毒气罐
                b.box(-3, s * 4.5f, 3.2f, 1.0f, 1.0f, 0.8f, Pal::GUN);              // 罐盖
                b.box(-3, s * 5.8f, 1.0f, 0.6f, 0.6f, 0.6f, Color{160, 255, 120, 255}, 0, M3FACE_ALL); // 喷气孔
            }
            b.box(-1, 0, 2.5f, 6, 0.8f, 0.5f, Pal::REMAP, M3FACE_ALL);             // 阵营条
            break;
        }
        default:
            return false; // 步兵/军犬/火箭飞行兵等：回退 2D 绘制
    }
    if (turretPivotX) *turretPivotX = pvx;
    if (turretPivotY) *turretPivotY = pvy;
    return true;
}
