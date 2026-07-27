#include "gfx/bldmodels.h"
#include "core/util.h"
#include <cmath>

// ===================== 瓦片坐标 → 模型空间 =====================
// 游戏瓦片 (gx,gy) → 渲染模型 (x,y,z)：(gx-gy)*S, -(gx+gy)*S, gz
// 屏幕 x = 模型x，屏幕 y = -模型y*0.5 - z*0.866（m3Render 投影），
// 因此游戏 +x → 屏幕右下、+y → 屏幕左下，与 tileToScreen 一致。
namespace {

constexpr float S = 32.0f;            // 每格模型单位（= TILE_W/2）
constexpr float R = 45.254833995939f; // S*sqrt(2)：圆半径换算（相似变换缩放）
constexpr float ZS = 2.4f;            // 高度拉伸：投影 z*0.866 且 RA2 建筑高耸，故拔高 z 比例

// RA2 建筑调色板
const Color CONC{150, 150, 154};      // 混凝土基座
const Color A_WALL{186, 190, 200};    // 盟系墙：浅灰白
const Color A_WALL_D{148, 152, 162};
const Color A_ROOF{86, 98, 114};      // 盟系顶：蓝灰
const Color S_WALL{172, 144, 104};    // 苏系墙：土黄棕
const Color S_WALL_D{134, 110, 76};
const Color S_ROOF{104, 62, 48};      // 苏系顶：锈红棕
const Color N_WALL{176, 166, 144};    // 中立墙：米褐
const Color N_WALL_D{140, 130, 110};
const Color N_ROOF{108, 100, 82};
const Color GLASSC{120, 190, 220};    // 玻璃
const Color DOOR{52, 54, 58};         // 门/洞口
const Color PIPE{96, 100, 106};       // 管道
const Color E_ORANGE{255, 160, 60};   // 发光：橙
const Color E_CYAN{140, 230, 255};    // 发光：青
const Color E_RED{255, 70, 55};       // 发光：红
const Color E_GREEN{120, 255, 140};   // 发光：绿
const Color E_PURPLE{200, 120, 255};  // 发光：紫
const Color E_WHITE{235, 240, 245};   // 发光：白

struct B3 {
    M3Builder& b;
    explicit B3(M3Builder& bb) : b(bb) {}

    void map(float gx, float gy, float gz, float out[3]) const {
        out[0] = (gx - gy) * S;
        out[1] = -(gx + gy) * S;
        out[2] = gz * ZS;
    }
    // 轴对齐盒：中心 (gx,gy) 格，占地 sx×sy 格，z0 底、高 sz 像素
    void box(float gx, float gy, float z0, float sx, float sy, float sz, Color col,
             uint32_t remapMask = 0, uint32_t emitMask = 0) {
        float hx = sx / 2, hy = sy / 2, z1 = z0 + sz;
        float c[8][3];
        map(gx - hx, gy - hy, z0, c[0]); map(gx + hx, gy - hy, z0, c[1]);
        map(gx + hx, gy + hy, z0, c[2]); map(gx - hx, gy + hy, z0, c[3]);
        map(gx - hx, gy - hy, z1, c[4]); map(gx + hx, gy - hy, z1, c[5]);
        map(gx + hx, gy + hy, z1, c[6]); map(gx - hx, gy + hy, z1, c[7]);
        b.hexa(c, col, remapMask, emitMask);
    }
    // 圆柱：中心 (gx,gy) 格，半径 r 格，z0 起高 h 像素
    void cyl(float gx, float gy, float z0, float r, float h, Color col, int seg = 10,
             bool remapTop = false, bool emitTop = false) {
        float mx, my, dummy[3];
        map(gx, gy, z0, dummy);
        mx = dummy[0]; my = dummy[1];
        b.cylZ(mx, my, z0 * ZS, r * R, h * ZS, col, seg, remapTop, emitTop);
    }
    // 椭球：中心 (gx,gy) 格，半径 (rx,ry) 格、rz 像素
    void ellip(float gx, float gy, float gz, float rx, float ry, float rz, Color col,
               int lonSeg = 10, int latSeg = 7) {
        float c[3];
        map(gx, gy, gz, c);
        b.ellipsoid(c[0], c[1], c[2], rx * R, ry * R, rz * ZS, col, lonSeg, latSeg);
    }
    // 人字屋顶：底 (w×d) 于 z0，脊线高 rise；ridgeX=true 脊沿游戏 x
    void gable(float gx, float gy, float w, float d, float z0, float rise, bool ridgeX, Color col) {
        float hx = w / 2, hy = d / 2, z1 = z0 + rise;
        float c[8][3];
        map(gx - hx, gy - hy, z0, c[0]); map(gx + hx, gy - hy, z0, c[1]);
        map(gx + hx, gy + hy, z0, c[2]); map(gx - hx, gy + hy, z0, c[3]);
        if (ridgeX) {
            map(gx - hx, gy, z1, c[4]); map(gx + hx, gy, z1, c[5]);
            map(gx + hx, gy, z1, c[6]); map(gx - hx, gy, z1, c[7]);
        } else {
            map(gx, gy - hy, z1, c[4]); map(gx, gy - hy, z1, c[5]);
            map(gx, gy + hy, z1, c[6]); map(gx, gy + hy, z1, c[7]);
        }
        b.hexa(c, col);
    }
    // 混凝土基座（建筑通用底盘，四周外扩 m 格）
    void slab(float w, float h, float m = 0.12f) {
        box(w / 2, h / 2, 0.0f, w + m * 2, h + m * 2, 2.0f, CONC);
    }
    // 旗杆 + 阵营色小旗（屋顶识别）
    void flag(float gx, float gy, float z0) {
        box(gx, gy, z0, 0.06f, 0.06f, 10.0f, Pal::GUN);
        box(gx + 0.10f, gy, z0 + 6.5f, 0.16f, 0.05f, 3.5f, Pal::REMAP, M3FACE_ALL);
    }
    // 天线杆
    void antenna(float gx, float gy, float z0, float h, Color tip = E_RED) {
        box(gx, gy, z0, 0.05f, 0.05f, h, Pal::GUN);
        box(gx, gy, z0 + h, 0.08f, 0.08f, 1.6f, tip, 0, M3FACE_ALL);
    }
    // 烟囱/排气塔：柱体 + 深色顶口
    void stack(float gx, float gy, float z0, float r, float h, Color col) {
        cyl(gx, gy, z0, r, h, col, 10);
        cyl(gx, gy, z0 + h - 1.0f, r * 1.08f, 2.2f, Pal::GUN, 10);
    }
    // 墙面发光窗带（南侧可视墙，沿游戏 x 一排）
    void winRowX(float gx0, float gy, float gz, int n, float step, Color c = GLASSC) {
        for (int i = 0; i < n; i++)
            box(gx0 + i * step, gy, gz, 0.12f, 0.03f, 2.4f, c, 0, M3FACE_ALL);
    }
    void winRowY(float gx, float gy0, float gz, int n, float step, Color c = GLASSC) {
        for (int i = 0; i < n; i++)
            box(gx, gy0 + i * step, gz, 0.03f, 0.12f, 2.4f, c, 0, M3FACE_ALL);
    }
    // 厂房大门（南面深色门洞板）
    void doorX(float gx, float gy, float z0, float w, float h) {
        box(gx, gy, z0, w, 0.04f, h, DOOR);
    }
    void doorY(float gx, float gy, float z0, float w, float h) {
        box(gx, gy, z0, 0.04f, w, h, DOOR);
    }
};

} // namespace

// ===================== 建筑模型 =====================
bool buildBldModel3D(BldType t, M3Builder& mb) {
    B3 b(mb);
    switch (t) {
        // ---------- 基地核心 ----------
        case BldType::ConYard: { // 建造厂：主厂房 + 副楼 + 吊塔 + 阵营旗
            b.slab(3, 3);
            b.box(1.2f, 1.5f, 2.0f, 1.9f, 2.2f, 20.0f, A_WALL);
            b.box(1.2f, 1.5f, 22.0f, 2.0f, 2.3f, 2.2f, A_ROOF);       // 檐口
            b.box(2.3f, 1.0f, 2.0f, 1.0f, 1.3f, 13.0f, A_WALL_D);      // 副楼
            b.box(2.3f, 1.0f, 15.0f, 1.1f, 1.4f, 1.8f, A_ROOF);
            b.winRowX(0.6f, 0.42f, 12.0f, 4, 0.38f);                   // 南墙窗带
            b.doorX(1.5f, 2.61f, 2.0f, 0.6f, 8.0f);                    // 车库门
            // 吊塔：立柱 + 横臂 + 吊钩
            b.box(0.35f, 2.5f, 2.0f, 0.22f, 0.22f, 30.0f, PIPE);
            b.box(0.95f, 2.5f, 30.0f, 1.5f, 0.14f, 1.6f, PIPE);
            b.box(1.55f, 2.5f, 26.0f, 0.05f, 0.05f, 4.5f, Pal::GUN);
            b.box(1.55f, 2.5f, 24.5f, 0.14f, 0.14f, 1.4f, CONC);
            b.flag(0.45f, 0.55f, 24.2f);
            b.antenna(2.3f, 1.0f, 16.8f, 9.0f);
            break;
        }
        // ---------- 电力 ----------
        case BldType::PowerPlant: { // 发电厂（盟）：主厂房 + 双冷却塔 + 玻璃带
            b.slab(2, 2);
            b.box(0.8f, 1.0f, 2.0f, 1.1f, 1.5f, 14.0f, A_WALL);
            b.box(0.8f, 1.0f, 16.0f, 1.2f, 1.6f, 1.8f, A_ROOF);
            b.winRowX(0.45f, 0.27f, 9.0f, 3, 0.35f);
            b.doorY(0.27f, 1.3f, 2.0f, 0.4f, 6.0f);
            b.stack(1.55f, 0.6f, 2.0f, 0.22f, 22.0f, A_WALL_D);        // 双冷却塔
            b.stack(1.55f, 1.5f, 2.0f, 0.22f, 18.0f, A_WALL_D);
            b.flag(0.35f, 0.4f, 17.8f);
            break;
        }
        case BldType::TeslaReactor: { // 磁能反应堆（苏）：厂房 + 绝缘柱 + 发光磁球
            b.slab(2, 2);
            b.box(1.0f, 1.1f, 2.0f, 1.5f, 1.4f, 11.0f, S_WALL);
            b.box(1.0f, 1.1f, 13.0f, 1.6f, 1.5f, 1.8f, S_ROOF);
            b.winRowX(0.6f, 0.42f, 7.0f, 3, 0.4f);
            b.doorX(1.0f, 1.81f, 2.0f, 0.5f, 6.0f);
            b.cyl(1.0f, 1.1f, 14.8f, 0.28f, 2.0f, PIPE, 10);           // 基座环
            b.box(1.0f, 1.1f, 16.5f, 0.16f, 0.16f, 5.0f, Pal::GUN);    // 绝缘柱
            b.ellip(1.0f, 1.1f, 24.0f, 0.30f, 0.30f, 5.5f, E_ORANGE);  // 磁球（发光）
            b.ellip(1.0f, 1.1f, 24.0f, 0.16f, 0.16f, 7.5f, E_WHITE);   // 内芯亮核
            b.flag(0.4f, 0.5f, 14.8f);
            break;
        }
        case BldType::NuclearReactor: { // 核子反应堆：主厂房 + 安全壳高塔 + 警示灯
            b.slab(3, 3);
            b.box(1.2f, 1.8f, 2.0f, 1.8f, 1.6f, 15.0f, S_WALL);
            b.box(1.2f, 1.8f, 17.0f, 1.9f, 1.7f, 2.0f, S_ROOF);
            b.winRowX(0.6f, 1.02f, 10.0f, 4, 0.4f);
            b.doorX(1.5f, 2.61f, 2.0f, 0.6f, 7.0f);
            b.cyl(2.2f, 0.9f, 2.0f, 0.55f, 30.0f, S_WALL_D, 14);       // 安全壳
            b.ellip(2.2f, 0.9f, 33.0f, 0.55f, 0.55f, 4.5f, CONC);      // 壳顶
            b.box(2.2f, 0.9f, 36.0f, 0.10f, 0.10f, 2.5f, E_RED, 0, M3FACE_ALL); // 顶警示灯
            b.box(0.5f, 1.05f, 17.5f, 0.10f, 0.10f, 1.8f, E_RED, 0, M3FACE_ALL);
            b.flag(0.45f, 1.3f, 19.0f);
            break;
        }
        // ---------- 生产 ----------
        case BldType::Barracks: { // 兵营：人字顶营房 + 旗
            b.slab(2, 2);
            b.box(1.0f, 1.0f, 2.0f, 1.6f, 1.5f, 10.0f, N_WALL);
            b.gable(1.0f, 1.0f, 1.7f, 1.6f, 12.0f, 9.0f, true, N_ROOF);
            b.doorX(1.0f, 1.76f, 2.0f, 0.45f, 6.0f);
            b.winRowY(0.22f, 0.7f, 6.0f, 3, 0.32f);
            b.winRowX(0.55f, 0.26f, 6.0f, 3, 0.45f);
            b.flag(1.5f, 0.45f, 21.0f);
            break;
        }
        case BldType::WarFactory: { // 战车工厂：大跨度厂房（三段弧顶）+ 塔吊
            b.slab(3, 3);
            b.box(1.5f, 1.6f, 2.0f, 2.4f, 2.2f, 16.0f, A_WALL_D);
            b.box(1.5f, 1.6f, 18.0f, 2.2f, 2.0f, 3.0f, A_WALL);        // 弧顶叠层
            b.box(1.5f, 1.6f, 21.0f, 1.8f, 1.6f, 2.6f, A_WALL);
            b.box(1.5f, 1.6f, 23.6f, 1.3f, 1.1f, 2.2f, A_ROOF);
            b.doorX(1.5f, 2.71f, 2.0f, 1.5f, 11.0f);                   // 大车库门
            b.box(1.5f, 2.68f, 13.5f, 1.7f, 0.06f, 1.2f, Pal::REMAP, M3FACE_ALL); // 门上色带
            b.winRowY(0.32f, 0.8f, 10.0f, 4, 0.45f);
            b.stack(2.6f, 0.6f, 2.0f, 0.18f, 20.0f, PIPE);
            b.flag(0.5f, 0.6f, 25.8f);
            break;
        }
        case BldType::OreRefinery: { // 矿石精炼厂：处理间 + 受矿斗 + 储矿筒
            b.slab(3, 2);
            b.box(2.1f, 1.0f, 2.0f, 1.3f, 1.5f, 13.0f, S_WALL);
            b.box(2.1f, 1.0f, 15.0f, 1.4f, 1.6f, 1.8f, S_ROOF);
            // 受矿斗（斜斗：四级台阶）
            for (int i = 0; i < 4; i++)
                b.box(0.75f, 1.0f, 2.0f + i * 3.0f, 1.3f - i * 0.22f, 1.5f - i * 0.18f, 3.0f, Color{92, 84, 70, 255});
            b.box(0.75f, 1.0f, 14.0f, 0.8f, 1.0f, 1.2f, DOOR);        // 斗口
            b.cyl(1.65f, 0.55f, 2.0f, 0.30f, 17.0f, PIPE, 10);         // 排气管
            b.winRowX(1.85f, 0.27f, 9.0f, 2, 0.5f);
            b.flag(2.4f, 0.5f, 16.8f);
            break;
        }
        // ---------- 科技 ----------
        case BldType::Radar: { // 雷达站：机房 + 塔 + 球罩雷达
            b.slab(2, 2);
            b.box(0.75f, 1.1f, 2.0f, 1.0f, 1.3f, 10.0f, A_WALL);
            b.box(0.75f, 1.1f, 12.0f, 1.1f, 1.4f, 1.6f, A_ROOF);
            b.doorY(0.27f, 1.4f, 2.0f, 0.4f, 5.0f);
            b.box(1.45f, 0.75f, 2.0f, 0.30f, 0.30f, 16.0f, A_WALL_D);  // 塔身
            b.ellip(1.45f, 0.75f, 21.5f, 0.42f, 0.42f, 6.0f, E_WHITE); // 球罩
            b.box(1.45f, 0.75f, 27.0f, 0.06f, 0.06f, 3.0f, Pal::GUN);
            b.flag(0.45f, 0.6f, 13.6f);
            break;
        }
        case BldType::BattleLab: { // 作战实验室：实验楼 + 半球穹顶 + 天线阵
            b.slab(2, 2);
            b.box(0.85f, 1.05f, 2.0f, 1.2f, 1.4f, 13.0f, A_WALL);
            b.box(0.85f, 1.05f, 15.0f, 1.3f, 1.5f, 1.8f, A_ROOF);
            b.winRowX(0.5f, 0.37f, 9.0f, 3, 0.35f);
            b.ellip(1.55f, 1.15f, 8.0f, 0.45f, 0.45f, 7.0f, GLASSC);   // 玻璃穹顶
            b.antenna(1.5f, 0.45f, 2.0f, 24.0f, E_CYAN);
            b.antenna(1.75f, 0.6f, 2.0f, 19.0f, E_RED);
            b.doorX(0.85f, 1.76f, 2.0f, 0.45f, 6.0f);
            b.flag(0.4f, 0.5f, 16.8f);
            break;
        }
        case BldType::AirForceCmd: { // 空指部：塔台（玻璃瞭望室）+ 雷达
            b.slab(2, 2);
            b.box(0.7f, 1.2f, 2.0f, 0.9f, 1.1f, 9.0f, A_WALL);
            b.box(0.7f, 1.2f, 11.0f, 1.0f, 1.2f, 1.6f, A_ROOF);
            b.box(1.35f, 0.85f, 2.0f, 0.55f, 0.55f, 18.0f, A_WALL_D);  // 塔身
            b.box(1.35f, 0.85f, 20.0f, 0.75f, 0.75f, 5.0f, GLASSC, 0, M3FACE_ALL); // 瞭望室
            b.box(1.35f, 0.85f, 25.0f, 0.85f, 0.85f, 1.2f, A_ROOF);
            b.antenna(1.35f, 0.85f, 26.2f, 5.0f);
            b.flag(0.4f, 0.7f, 12.6f);
            break;
        }
        case BldType::NavalYard: { // 海军船厂：船台厂房 + 门式吊
            b.slab(3, 3);
            b.box(1.1f, 1.5f, 2.0f, 1.7f, 2.0f, 13.0f, A_WALL_D);
            b.box(1.1f, 1.5f, 15.0f, 1.8f, 2.1f, 1.8f, A_ROOF);
            b.doorX(1.1f, 2.51f, 2.0f, 1.1f, 9.0f);
            b.winRowY(0.27f, 0.9f, 8.0f, 3, 0.5f);
            // 门式吊：双柱 + 横梁
            b.box(2.45f, 1.0f, 2.0f, 0.18f, 0.18f, 22.0f, PIPE);
            b.box(2.45f, 2.3f, 2.0f, 0.18f, 0.18f, 22.0f, PIPE);
            b.box(2.45f, 1.65f, 24.0f, 0.16f, 1.5f, 1.6f, PIPE);
            b.box(2.45f, 1.65f, 20.0f, 0.05f, 0.05f, 4.0f, Pal::GUN);
            b.flag(0.45f, 0.55f, 16.8f);
            break;
        }
        // ---------- 防御 ----------
        case BldType::Pillbox: { // 机枪碉堡：低八棱堡 + 枪眼
            b.slab(1, 1, 0.08f);
            b.cyl(0.5f, 0.5f, 2.0f, 0.42f, 6.0f, CONC, 8);
            b.cyl(0.5f, 0.5f, 8.0f, 0.46f, 1.6f, N_WALL_D, 8, true);   // 堡檐（阵营色）
            b.box(0.5f, 0.06f, 5.0f, 0.5f, 0.06f, 1.8f, DOOR);        // 枪眼
            b.box(0.5f, 0.94f, 5.0f, 0.5f, 0.06f, 1.8f, DOOR);
            break;
        }
        case BldType::SentryGun: { // 哨戒炮：基座 + 炮箱 + 炮管
            b.slab(1, 1, 0.08f);
            b.cyl(0.5f, 0.5f, 2.0f, 0.32f, 3.5f, S_WALL_D, 8);
            b.box(0.5f, 0.5f, 5.5f, 0.5f, 0.45f, 4.0f, S_WALL);
            b.box(0.5f, 0.5f, 9.5f, 0.54f, 0.30f, 1.0f, Pal::REMAP, M3FACE_ALL); // 色带
            { // 炮管（模型空间朝东南，屏幕右侧）
                float c[3]; b.map(0.75f, 0.5f, 7.0f, c);
                mb.cylXY(c[0], c[1], c[2], 2.2f, 20.0f, 0, Pal::GUN, 8);
            }
            break;
        }
        case BldType::PrismTower: { // 光棱塔：塔身 + 棱镜晶体（自发光）
            b.slab(1, 1, 0.08f);
            b.box(0.5f, 0.5f, 2.0f, 0.5f, 0.5f, 6.0f, A_WALL);
            b.box(0.5f, 0.5f, 8.0f, 0.40f, 0.40f, 10.0f, A_WALL_D);   // 塔柱
            b.box(0.5f, 0.5f, 18.0f, 0.46f, 0.46f, 1.4f, Pal::REMAP, M3FACE_ALL);
            { // 棱镜：双锥晶体（上下两椭球压扁拼合）
                float c[3]; b.map(0.5f, 0.5f, 23.0f, c);
                mb.ellipsoid(c[0], c[1], c[2], 7.0f, 7.0f, 8.0f, E_CYAN, 6, 5);
                mb.ellipsoid(c[0], c[1], c[2], 3.5f, 3.5f, 10.5f, E_WHITE, 6, 5);
            }
            break;
        }
        case BldType::TeslaCoil: { // 磁暴线圈：基座 + 绝缘柱 + 发光电球
            b.slab(1, 1, 0.08f);
            b.cyl(0.5f, 0.5f, 2.0f, 0.38f, 4.0f, S_WALL_D, 8);
            b.cyl(0.5f, 0.5f, 6.0f, 0.30f, 1.4f, Pal::REMAP, 8, true);
            b.box(0.5f, 0.5f, 7.4f, 0.30f, 0.30f, 9.0f, PIPE);        // 绝缘柱
            b.box(0.5f, 0.5f, 12.0f, 0.38f, 0.38f, 1.2f, CONC);       // 环箍
            b.ellip(0.5f, 0.5f, 19.5f, 0.22f, 0.22f, 4.5f, E_ORANGE); // 电球
            b.ellip(0.5f, 0.5f, 19.5f, 0.11f, 0.11f, 6.0f, E_WHITE);
            break;
        }
        case BldType::FlakCannon: { // 高射炮：炮位 + 双联炮管
            b.slab(1, 1, 0.08f);
            b.cyl(0.5f, 0.5f, 2.0f, 0.36f, 3.0f, S_WALL_D, 8);
            b.box(0.5f, 0.5f, 5.0f, 0.55f, 0.5f, 3.5f, S_WALL);
            {
                float c[3]; b.map(0.72f, 0.5f, 8.0f, c);
                mb.cylXY(c[0], c[1], c[2] - 2.0f, 2.0f, 16.0f, 0, Pal::GUN, 8);
                mb.cylXY(c[0], c[1], c[2] + 2.0f, 2.0f, 16.0f, 0, Pal::GUN, 8);
            }
            b.box(0.5f, 0.5f, 8.5f, 0.30f, 0.52f, 1.0f, Pal::REMAP, M3FACE_ALL);
            break;
        }
        case BldType::GrandCannon: { // 巨炮：重型基座 + 炮塔 + 长炮管
            b.slab(2, 2, 0.08f);
            b.box(1.0f, 1.0f, 2.0f, 1.4f, 1.4f, 7.0f, A_WALL_D);
            b.box(1.0f, 1.0f, 9.0f, 1.5f, 1.5f, 1.4f, CONC);
            b.box(0.95f, 0.85f, 10.4f, 1.0f, 1.0f, 5.5f, A_WALL);      // 炮塔
            b.box(0.95f, 0.85f, 15.6f, 0.9f, 0.9f, 1.0f, Pal::REMAP, M3FACE_ALL);
            { // 炮管：模型空间直指正东（屏幕右），自炮塔心穿过塔面伸出
                mb.cylXY(3.2f, -57.6f, 30.0f, 4.5f, 58.0f, 0, Pal::GUN, 10);
                mb.box(3.2f + 50.0f, -57.6f, 30.0f, 5.0f, 6.5f, 6.5f, Pal::GUN);
            }
            break;
        }
        case BldType::PatriotMissile: { // 爱国者飞弹：发射座 + 竖立导弹
            b.slab(1, 1, 0.08f);
            b.cyl(0.5f, 0.5f, 2.0f, 0.34f, 2.5f, A_WALL_D, 8);
            b.box(0.42f, 0.5f, 4.5f, 0.22f, 0.22f, 12.0f, E_WHITE);   // 弹体
            b.cyl(0.42f, 0.5f, 16.5f, 0.10f, 2.5f, E_RED, 8, false, true); // 弹头
            b.box(0.60f, 0.5f, 4.5f, 0.10f, 0.30f, 9.0f, A_WALL);     // 导轨
            b.box(0.5f, 0.5f, 4.5f, 0.44f, 0.44f, 1.0f, Pal::REMAP, M3FACE_ALL);
            break;
        }
        case BldType::Wall: { // 围墙：混凝土墙段 + 顶色标
            b.box(0.5f, 0.5f, 0.5f, 0.9f, 0.9f, 7.0f, Color{150, 148, 140, 255});
            b.box(0.5f, 0.5f, 7.5f, 0.92f, 0.92f, 1.0f, Color{110, 108, 100, 255});
            b.box(0.5f, 0.5f, 8.5f, 0.3f, 0.3f, 0.8f, Pal::REMAP, M3FACE_ALL);
            break;
        }
        // ---------- 高级经济 ----------
        case BldType::OrePurifier: { // 矿石精炼器：厂房 + 双提纯塔 + 输矿管
            b.slab(2, 2);
            b.box(0.85f, 1.05f, 2.0f, 1.2f, 1.4f, 11.0f, A_WALL);
            b.box(0.85f, 1.05f, 13.0f, 1.3f, 1.5f, 1.6f, A_ROOF);
            b.cyl(1.6f, 0.6f, 2.0f, 0.24f, 18.0f, PIPE, 10);
            b.cyl(1.6f, 1.3f, 2.0f, 0.24f, 15.0f, PIPE, 10);
            b.box(1.6f, 0.95f, 12.0f, 0.10f, 0.8f, 1.0f, Pal::REMAP, M3FACE_ALL); // 管联色带
            b.winRowX(0.5f, 0.37f, 8.0f, 3, 0.35f);
            b.flag(0.4f, 0.5f, 14.6f);
            break;
        }
        case BldType::IndustrialPlant: { // 工业工厂：锯齿顶厂房 + 双烟囱
            b.slab(3, 2);
            b.box(1.5f, 1.0f, 2.0f, 2.4f, 1.6f, 12.0f, S_WALL);
            b.gable(0.85f, 1.0f, 1.2f, 1.7f, 14.0f, 4.0f, false, S_ROOF);
            b.gable(2.15f, 1.0f, 1.2f, 1.7f, 14.0f, 4.0f, false, S_ROOF);
            b.doorX(1.5f, 1.81f, 2.0f, 0.9f, 8.0f);
            b.stack(2.65f, 0.5f, 2.0f, 0.20f, 24.0f, S_WALL_D);
            b.stack(2.65f, 1.4f, 2.0f, 0.20f, 20.0f, S_WALL_D);
            b.winRowX(0.5f, 0.22f, 8.0f, 4, 0.5f);
            b.flag(0.45f, 0.5f, 18.0f);
            break;
        }
        // ---------- 超武 ----------
        case BldType::NukeSilo: { // 核弹井：圆形井盖 + 导弹尖 + 警示灯环
            b.slab(2, 2);
            b.cyl(1.0f, 1.0f, 2.0f, 0.75f, 3.0f, CONC, 14);
            b.cyl(1.0f, 1.0f, 5.0f, 0.60f, 1.2f, Pal::GUN, 14);
            b.cyl(1.0f, 1.0f, 6.2f, 0.16f, 6.0f, E_WHITE, 10);         // 弹体尖
            b.cyl(1.0f, 1.0f, 12.2f, 0.10f, 2.0f, E_RED, 8, false, true);
            for (int i = 0; i < 4; i++) { // 警示灯
                float a = i * 1.5708f;
                b.box(1.0f + cosf(a) * 0.85f, 1.0f + sinf(a) * 0.85f, 4.0f, 0.08f, 0.08f, 1.6f, E_RED, 0, M3FACE_ALL);
            }
            b.flag(0.35f, 0.5f, 5.0f);
            break;
        }
        case BldType::WeatherDevice: { // 天气控制器：主控楼 + 三极水晶阵
            b.slab(3, 2);
            b.box(1.5f, 1.0f, 2.0f, 1.4f, 1.3f, 10.0f, A_WALL);
            b.box(1.5f, 1.0f, 12.0f, 1.5f, 1.4f, 1.6f, A_ROOF);
            b.winRowX(1.0f, 0.37f, 7.0f, 4, 0.35f);
            for (int i = 0; i < 3; i++) {
                float px = 0.6f + i * 0.9f;
                b.box(px, 0.45f, 2.0f, 0.14f, 0.14f, 16.0f + i * 3.0f, A_WALL_D);
                b.ellip(px, 0.45f, 20.0f + i * 3.0f, 0.12f, 0.12f, 3.5f, E_CYAN);
            }
            b.ellip(1.5f, 1.0f, 19.0f, 0.26f, 0.26f, 4.0f, E_CYAN);    // 主水晶
            b.flag(0.5f, 1.5f, 13.6f);
            break;
        }
        case BldType::IronCurtain: { // 铁幕装置：门式拱架 + 赤红力场核
            b.slab(2, 2);
            b.box(0.5f, 1.0f, 2.0f, 0.5f, 1.2f, 18.0f, S_WALL_D);     // 双柱
            b.box(1.5f, 1.0f, 2.0f, 0.5f, 1.2f, 18.0f, S_WALL_D);
            b.box(1.0f, 1.0f, 20.0f, 1.6f, 1.3f, 3.0f, S_ROOF);       // 顶梁
            b.box(1.0f, 1.0f, 23.0f, 0.5f, 0.5f, 1.2f, Pal::REMAP, M3FACE_ALL);
            b.ellip(1.0f, 1.0f, 12.0f, 0.34f, 0.34f, 5.5f, E_RED);    // 力场核
            b.ellip(1.0f, 1.0f, 12.0f, 0.17f, 0.17f, 7.0f, E_WHITE);  // 核内亮芯
            b.doorX(1.0f, 1.61f, 2.0f, 0.5f, 5.0f);
            break;
        }
        case BldType::ChronoSphere: { // 超时空传送仪：三支柱 + 时空球
            b.slab(3, 2);
            b.box(0.7f, 1.0f, 2.0f, 0.45f, 0.9f, 15.0f, A_WALL);
            b.box(2.3f, 1.0f, 2.0f, 0.45f, 0.9f, 15.0f, A_WALL);
            b.box(1.5f, 0.6f, 2.0f, 0.9f, 0.45f, 15.0f, A_WALL_D);
            b.box(1.5f, 1.0f, 17.0f, 2.2f, 1.1f, 2.0f, A_ROOF);       // 顶环梁
            b.ellip(1.5f, 1.0f, 11.0f, 0.34f, 0.34f, 6.5f, E_CYAN);   // 时空球
            b.ellip(1.5f, 1.0f, 11.0f, 0.16f, 0.16f, 8.0f, E_WHITE);
            b.flag(0.5f, 1.5f, 19.0f);
            break;
        }
        // ---------- 中立科技 ----------
        case BldType::OilDerrick: { // 油井：井架塔 + 横担 + 储油罐
            b.slab(2, 2);
            b.box(0.65f, 0.65f, 2.0f, 0.13f, 0.13f, 24.0f, PIPE);     // 井架四柱
            b.box(1.05f, 0.65f, 2.0f, 0.13f, 0.13f, 24.0f, PIPE);
            b.box(0.65f, 1.05f, 2.0f, 0.13f, 0.13f, 24.0f, PIPE);
            b.box(1.05f, 1.05f, 2.0f, 0.13f, 0.13f, 24.0f, PIPE);
            b.box(0.85f, 0.85f, 11.0f, 0.55f, 0.55f, 1.0f, PIPE);      // 横担
            b.box(0.85f, 0.85f, 19.0f, 0.48f, 0.48f, 1.0f, PIPE);
            b.box(0.85f, 0.85f, 25.5f, 0.45f, 0.38f, 2.0f, Pal::GUN);  // 塔顶
            b.box(0.85f, 0.85f, 27.5f, 0.08f, 0.08f, 2.0f, E_RED, 0, M3FACE_ALL);
            b.cyl(1.6f, 1.5f, 2.0f, 0.36f, 7.0f, N_WALL_D, 14);        // 储油罐
            break;
        }
        case BldType::Hospital: { // 医院：白楼 + 红十字
            b.slab(2, 2);
            b.box(1.0f, 1.0f, 2.0f, 1.5f, 1.5f, 15.0f, Color{208, 210, 214, 255});
            b.box(1.0f, 1.0f, 17.0f, 1.6f, 1.6f, 1.8f, A_ROOF);
            b.winRowX(0.6f, 0.27f, 8.0f, 3, 0.4f);
            b.winRowX(0.6f, 0.27f, 12.0f, 3, 0.4f);
            b.doorX(1.0f, 1.76f, 2.0f, 0.5f, 7.0f);
            b.box(1.0f, 1.0f, 18.8f, 0.5f, 0.14f, 0.9f, E_RED, 0, M3FACE_ALL); // 十字
            b.box(1.0f, 1.0f, 18.8f, 0.14f, 0.5f, 0.9f, E_RED, 0, M3FACE_ALL);
            break;
        }
        case BldType::MachineShop: { // 机械商店：车库 + 排气塔
            b.slab(2, 2);
            b.box(1.0f, 1.0f, 2.0f, 1.5f, 1.5f, 11.0f, N_WALL);
            b.box(1.0f, 1.0f, 13.0f, 1.6f, 1.6f, 1.8f, N_ROOF);
            b.doorX(1.0f, 1.76f, 2.0f, 0.9f, 8.0f);
            b.box(1.0f, 1.73f, 10.5f, 1.0f, 0.06f, 1.0f, Pal::REMAP, M3FACE_ALL);
            b.stack(1.6f, 0.5f, 2.0f, 0.16f, 16.0f, PIPE);
            b.winRowY(0.27f, 0.7f, 7.0f, 3, 0.35f);
            break;
        }
        // ---------- RA2 补全：高级建筑 ----------
        case BldType::CloningVat: { // 复制中心：厂房 + 双培养槽（绿光）
            b.slab(2, 2);
            b.box(0.8f, 1.0f, 2.0f, 1.1f, 1.5f, 10.0f, S_WALL);
            b.box(0.8f, 1.0f, 12.0f, 1.2f, 1.6f, 1.6f, S_ROOF);
            b.cyl(1.55f, 0.7f, 2.0f, 0.26f, 12.0f, S_WALL_D, 10);
            b.cyl(1.55f, 0.7f, 14.0f, 0.28f, 1.0f, E_GREEN, 10, false, true); // 槽顶绿光
            b.cyl(1.55f, 1.35f, 2.0f, 0.26f, 12.0f, S_WALL_D, 10);
            b.cyl(1.55f, 1.35f, 14.0f, 0.28f, 1.0f, E_GREEN, 10, false, true);
            b.doorY(0.27f, 1.3f, 2.0f, 0.4f, 5.0f);
            b.flag(0.4f, 0.5f, 13.6f);
            break;
        }
        case BldType::ServiceDepot: { // 维修厂：开放式维修棚（柱 + 平顶）+ 举升台
            b.slab(3, 2);
            b.box(0.4f, 0.4f, 2.0f, 0.18f, 0.18f, 12.0f, PIPE);
            b.box(2.6f, 0.4f, 2.0f, 0.18f, 0.18f, 12.0f, PIPE);
            b.box(0.4f, 1.6f, 2.0f, 0.18f, 0.18f, 12.0f, PIPE);
            b.box(2.6f, 1.6f, 2.0f, 0.18f, 0.18f, 12.0f, PIPE);
            b.box(1.5f, 1.0f, 14.0f, 2.6f, 1.6f, 2.0f, N_ROOF);       // 平顶
            b.box(1.5f, 1.0f, 16.0f, 1.2f, 0.5f, 0.8f, Pal::REMAP, M3FACE_ALL);
            b.box(1.5f, 1.0f, 2.0f, 1.0f, 0.8f, 1.5f, CONC);          // 举升台
            b.box(0.7f, 1.7f, 2.0f, 0.5f, 0.3f, 6.0f, N_WALL);        // 工具间
            break;
        }
        case BldType::GapGenerator: { // 裂缝产生器：塔 + 干扰球
            b.slab(1, 1, 0.08f);
            b.box(0.5f, 0.5f, 2.0f, 0.45f, 0.45f, 8.0f, A_WALL);
            b.box(0.5f, 0.5f, 10.0f, 0.34f, 0.34f, 8.0f, A_WALL_D);
            b.box(0.5f, 0.5f, 18.0f, 0.38f, 0.38f, 1.2f, Pal::REMAP, M3FACE_ALL);
            b.ellip(0.5f, 0.5f, 23.0f, 0.24f, 0.24f, 5.0f, E_PURPLE);
            break;
        }
        case BldType::SpySat: { // 间谍卫星：机房 + 雷达罩球（高塔白球）
            b.slab(2, 2);
            b.box(0.8f, 1.1f, 2.0f, 1.1f, 1.3f, 9.0f, A_WALL);
            b.box(0.8f, 1.1f, 11.0f, 1.2f, 1.4f, 1.6f, A_ROOF);
            b.doorY(0.27f, 1.4f, 2.0f, 0.4f, 5.0f);
            b.ellip(0.8f, 1.1f, 19.5f, 0.40f, 0.40f, 7.0f, Color{214, 218, 224, 255}); // 屋顶雷达罩
            b.box(0.8f, 1.1f, 24.0f, 0.06f, 0.06f, 3.0f, Pal::GUN);   // 顶杆
            b.flag(1.45f, 1.55f, 12.6f);
            break;
        }
        case BldType::PsychicSensor: { // 心灵探测器：塔 + 紫晶球
            b.slab(1, 1, 0.08f);
            b.box(0.5f, 0.5f, 2.0f, 0.5f, 0.5f, 7.0f, S_WALL);
            b.box(0.5f, 0.5f, 9.0f, 0.36f, 0.36f, 9.0f, S_WALL_D);
            b.box(0.5f, 0.5f, 18.0f, 0.42f, 0.42f, 1.2f, Pal::REMAP, M3FACE_ALL);
            b.ellip(0.5f, 0.5f, 22.5f, 0.26f, 0.26f, 5.5f, E_PURPLE);
            b.ellip(0.5f, 0.5f, 22.5f, 0.12f, 0.12f, 7.0f, E_WHITE);
            break;
        }
        case BldType::BattleBunker: { // 战斗碉堡：混凝土半圆堡 + 射孔
            b.slab(1, 1, 0.08f);
            b.cyl(0.5f, 0.5f, 2.0f, 0.42f, 5.0f, CONC, 10);
            b.ellip(0.5f, 0.5f, 8.0f, 0.42f, 0.42f, 3.5f, N_WALL_D);
            b.box(0.5f, 0.08f, 5.5f, 0.5f, 0.06f, 1.6f, DOOR);
            b.box(0.5f, 0.5f, 11.0f, 0.14f, 0.14f, 0.8f, Pal::REMAP, M3FACE_ALL);
            break;
        }
        case BldType::TankBunker: { // 坦克碉堡：U 型矮墙（坦克进驻）
            b.slab(1, 1, 0.06f);
            b.box(0.15f, 0.5f, 2.0f, 0.2f, 0.9f, 5.0f, CONC);         // 西墙
            b.box(0.85f, 0.5f, 2.0f, 0.2f, 0.9f, 5.0f, CONC);         // 东墙
            b.box(0.5f, 0.12f, 2.0f, 0.9f, 0.2f, 5.0f, CONC);         // 北墙
            b.box(0.15f, 0.5f, 7.0f, 0.22f, 0.92f, 0.8f, Pal::REMAP, M3FACE_ALL); // 檐色
            break;
        }
        // ---------- RA2 补全：中立 ----------
        case BldType::TechAirport: { // 科技机场：航站楼 + 塔台 + 跑道
            b.slab(3, 2);
            b.box(1.2f, 1.0f, 2.0f, 1.7f, 1.3f, 9.0f, N_WALL);
            b.box(1.2f, 1.0f, 11.0f, 1.8f, 1.4f, 1.6f, N_ROOF);
            b.winRowX(0.6f, 0.37f, 6.0f, 5, 0.32f);
            b.doorX(1.2f, 1.66f, 2.0f, 0.5f, 5.0f);
            b.box(2.5f, 0.6f, 2.0f, 0.4f, 0.4f, 16.0f, N_WALL_D);     // 塔台
            b.box(2.5f, 0.6f, 18.0f, 0.6f, 0.6f, 4.0f, GLASSC, 0, M3FACE_ALL);
            b.box(2.5f, 0.6f, 22.0f, 0.66f, 0.66f, 1.0f, N_ROOF);
            b.antenna(2.5f, 0.6f, 23.0f, 4.0f);
            break;
        }
        case BldType::SecretLab: { // 秘密实验室：地堡楼 + 穹顶 + 天线
            b.slab(2, 2);
            b.box(1.0f, 1.05f, 2.0f, 1.4f, 1.4f, 10.0f, N_WALL);
            b.box(1.0f, 1.05f, 12.0f, 1.5f, 1.5f, 1.6f, N_ROOF);
            b.ellip(1.0f, 1.05f, 16.0f, 0.35f, 0.35f, 4.5f, CONC);
            b.antenna(0.5f, 0.5f, 13.6f, 10.0f, E_GREEN);
            b.doorX(1.0f, 1.76f, 2.0f, 0.45f, 5.0f);
            b.winRowY(0.32f, 0.7f, 7.0f, 3, 0.35f);
            break;
        }
        case BldType::CivHouse: { // 民房：人字顶小屋 + 烟囱 + 窗
            b.slab(2, 2, 0.06f);
            b.box(1.0f, 1.0f, 2.0f, 1.3f, 1.2f, 9.0f, Color{186, 166, 136, 255});
            b.gable(1.0f, 1.0f, 1.45f, 1.35f, 11.0f, 9.0f, true, Color{138, 78, 54, 255});
            b.box(0.6f, 0.9f, 11.0f, 0.18f, 0.18f, 9.0f, Color{140, 100, 80, 255}); // 烟囱
            b.doorX(1.15f, 1.61f, 2.0f, 0.35f, 5.5f);
            b.winRowX(0.6f, 1.61f, 5.5f, 2, 0.3f, E_WHITE);
            b.winRowY(0.37f, 0.7f, 5.5f, 2, 0.4f, E_WHITE);
            break;
        }
        default:
            return false;
    }
    return true;
}
