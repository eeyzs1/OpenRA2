#include "game/lang.h"
#include "game/campaign.h"
#include "raylib.h"
#include <cstdio>

int g_lang = 0; // 0 中文 1 English

// ===================== 字符串表 =====================
// 顺序必须与 enum class S 一一对应
static const char* TBL[(int)S::COUNT][2] = {
    // 通用
    {"返回", "Back"},
    {"开", "On"},
    {"关", "Off"},
    {"设置", "Settings"},
    {"随机", "Random"},
    // 主菜单
    {"共和国之辉", "OPENRA2"},
    {"COMMAND & CONQUER · OPENRA2 像素复刻", "COMMAND & CONQUER · OPENRA2 PIXEL REMAKE"},
    {"遭遇战", "Skirmish"},
    {"战役模式", "Campaign"},
    {"退出游戏", "Exit Game"},
    {"程序生成像素素材 · 盟军 / 苏联 / 中国", "Procedural pixel assets · Allies / Soviet / China"},
    // 战役选择
    {"任务 %d", "Mission %d"},
    {"目标：坚守十分钟", "Objective: Hold for 10 minutes"},
    {"目标：歼灭所有敌军", "Objective: Eliminate all enemies"},
    {"点击进入", "Click to start"},
    // 遭遇战设置
    {"更换一张", "New Map"},
    {"地图尺寸", "Map Size"},
    {"小 64x64", "Small 64x64"},
    {"中 96x96", "Medium 96x96"},
    {"大 128x128", "Large 128x128"},
    {"地图类型", "Map Type"},
    {"大陆", "Continent"},
    {"岛屿", "Islands"},
    {"湖泊", "Lake"},
    {"玩家", "Player"},
    {"颜色", "Color"},
    {"阵营", "Faction"},
    {"指挥官（你）", "Commander (You)"},
    {"电脑 %d", "Computer %d"},
    {"移除", "Remove"},
    {"+ 添加电脑", "+ Add Computer"},
    {"初始资金", "Starting Funds"},
    {"游戏速度", "Game Speed"},
    {"慢", "Slow"},
    {"普通", "Normal"},
    {"快", "Fast"},
    {"音量", "Volume"},
    {"补给箱", "Crates"},
    {"AI结盟", "AI Alliance"},
    {"开始游戏", "Start Game"},
    // HUD / 侧边栏
    {"资金", "Funds"},
    {"电", "PWR"},
    {"低电力!", "LOW POWER!"},
    {"需建筑", "Need Bldg"},
    {"就绪", "Ready"},
    {"点击选目标", "Pick target"},
    {"建筑", "Base"},
    {"防御", "Def"},
    {"步兵", "Inf"},
    {"车辆", "Veh"},
    {"海军", "Navy"},
    {"选择放置位置（右键取消）", "Select placement (right-click to cancel)"},
    {"无法建造：缺前置建筑或资金不足", "Cannot build: missing prerequisite or low funds"},
    {"生产队列忙或条件不足", "Production busy or requirements unmet"},
    {"已取消一个", "Canceled one"},
    {"已取消生产", "Production canceled"},
    {"维修", "Repair"},
    {"出售", "Sell"},
    {"菜单", "Menu"},
    {"维修模式：点击己方受损建筑（右键取消）", "Repair mode: click your damaged building (right-click to cancel)"},
    {"出售模式：点击己方建筑（右键取消）", "Sell mode: click your building (right-click to cancel)"},
    {"坚守 %d:%02d", "Hold %d:%02d"},
    {"敌军增援将至（第%d/%d波）", "Enemy wave incoming (%d/%d)"},
    {"歼灭所有敌军", "Eliminate all enemies"},
    {"已选 %d 单位", "%d units selected"},
    {"载员 %d/%d  %s 卸载", "Cargo %d/%d  %s to unload"},
    {"左键选择/框选 右键移动/攻击(点己方运输船=登船) A+右键攻击移动 ESC菜单 · 按键可在设置中修改",
     "LMB select/box  RMB move/attack (click own transport=board)  A+RMB attack-move  ESC menu · keys in Settings"},
    {"已暂停", "PAUSED"},
    {"胜 利", "VICTORY"},
    {"失 败", "DEFEAT"},
    {"游戏菜单", "Game Menu"},
    {"再来一局", "Play Again"},
    {"继续游戏", "Continue"},
    {"保存进度", "Save Game"},
    {"读取进度", "Load Game"},
    {"重新开始", "Restart"},
    {"返回主菜单", "Main Menu"},
    // 局内消息
    {"找到基地车，按 %s 展开！", "Find your MCV, press %s to deploy!"},
    {"电力不足，生产减缓", "Low power: production slowed"},
    {"工程师：前往占领", "Engineer: moving to capture"},
    {"步兵登船中", "Infantry boarding"},
    {"工程师：前往修复", "Engineer: moving to repair"},
    {"无法在此放置", "Cannot place here"},
    {"已出售建筑", "Building sold"},
    {"建造厂不可出售", "Construction Yard cannot be sold"},
    {"建筑已修复", "Building repaired"},
    {"无需维修或资金不足", "Nothing to repair or low funds"},
    {"建造厂已部署", "Construction Yard deployed"},
    {"已切换部署状态", "Deploy state toggled"},
    {"单位散布", "Units scattering"},
    {"警戒模式：按视野索敌", "Guard mode: engage on sight"},
    {"选择同类单位", "Selected same-type units"},
    {"编队 %d 已设定（%d 单位）", "Squad %d set (%d units)"},
    {"音乐：开", "Music: On"},
    {"音乐：关", "Music: Off"},
    {"进度已保存", "Game saved"},
    {"保存失败", "Save failed"},
    {"进度已读取", "Game loaded"},
    {"读取失败（无存档）", "Load failed (no save)"},
    {"集结点已设置", "Rally point set"},
    {"%s已发射", "%s launched"},
    {"选择目标位置（右键取消）", "Select target (right-click to cancel)"},
    // EVA 播报
    {"警告：侦测到敌方%s", "Warning: enemy %s detected"},
    {"卸载完成", "Unload complete"},
    {"警告：无可靠岸地点，无法卸载", "Warning: no landing zone, cannot unload"},
    {"警告：核弹已发射", "Warning: nuclear missile launched"},
    {"警告：闪电风暴接近中", "Warning: lightning storm approaching"},
    {"超时空传送启动", "Chronosphere activated"},
    {"%s已就绪", "%s ready"},
    {"单位损失", "Unit lost"},
    {"建筑被占领：%s", "Building captured: %s"},
    {"已占领：%s", "Captured: %s"},
    {"工程师已修复：%s", "Engineer repaired: %s"},
    {"警告：基地遭受攻击", "Warning: base under attack"},
    {"警告：采矿车遭受攻击", "Warning: harvester under attack"},
    {"建筑被摧毁：%s", "Building destroyed: %s"},
    {"%s晋升为老兵", "%s promoted to Veteran"},
    {"%s晋升为精英", "%s promoted to Elite"},
    {"间谍渗透：窃取资金 $%d", "Spy: stole $%d"},
    {"警告：精炼厂被间谍渗透，资金失窃", "Warning: refinery infiltrated, funds stolen"},
    {"警告：电厂被间谍破坏，电力瘫痪", "Warning: power plant sabotaged"},
    {"间谍渗透：敌方电力瘫痪 30 秒", "Spy: enemy power down for 30s"},
    {"间谍渗透：已获取敌方雷达数据", "Spy: enemy radar data acquired"},
    {"警告：雷达站被间谍渗透", "Warning: radar infiltrated"},
    {"间谍渗透：新兵营单位直接晋升老兵", "Spy: new infantry start as veterans"},
    {"间谍渗透：新车辆/空军单位直接晋升老兵", "Spy: new vehicles/aircraft start as veterans"},
    {"间谍渗透：新海军单位直接晋升老兵", "Spy: new naval units start as veterans"},
    {"警告：作战实验室被间谍渗透", "Warning: battle lab infiltrated"},
    {"间谍渗透：窃取技术资料，敌方超武充能已重置", "Spy: tech stolen, enemy superweapon charge reset"},
    {"间谍渗透：%s", "Spy infiltrated: %s"},
    {"补给箱：获得资金 $1000", "Crate: +$1000"},
    {"补给箱：全体单位完全治疗", "Crate: all units fully healed"},
    {"补给箱：单位军衔晋升", "Crate: unit promoted"},
    {"警告：步兵无法承受超时空传送", "Warning: infantry cannot survive chrono shift"},
    {"警告：敌军增援抵达战场", "Warning: enemy reinforcements have arrived"},
    // 设置页
    {"语言", "Language"},
    {"显示与声音", "Display & Sound"},
    {"显示模式", "Display Mode"},
    {"无边框全屏", "Borderless Fullscreen"},
    {"窗口", "Windowed"},
    {"分辨率", "Resolution"},
    {"跟随桌面", "Desktop Native"},
    {"按键设置", "Key Bindings"},
    {"恢复默认按键", "Reset Defaults"},
    {"请按新按键…（ESC 取消）", "Press a key... (ESC to cancel)"},
    {"点击右侧键位框，然后按下新按键", "Click a key box, then press a new key"},
    {"停止", "Stop"},
    {"卸载", "Unload"},
    {"部署/展开", "Deploy"},
    {"散布", "Scatter"},
    {"警戒", "Guard"},
    {"选择同类", "Select Same Type"},
    {"音乐开关", "Music Toggle"},
    {"返回基地", "View Base"},
    {"暂停", "Pause"},
    {"设集结点", "Set Rally Point"},
    {"出售建筑", "Sell Building"},
    {"快速存档", "Quick Save"},
    {"快速读档", "Quick Load"},
    {"游戏加速", "Speed Up"},
    {"游戏减速", "Speed Down"},
};

const char* TR(S id) {
    int i = (int)id;
    if (i < 0 || i >= (int)S::COUNT) return "?";
    return TBL[i][g_lang ? 1 : 0];
}

// ===================== 本地化名称 =====================
static const char* UNIT_EN[(int)UnitType::COUNT] = {
    "MCV", "War Miner",
    "GI", "Conscript", "PLA",
    "Engineer", "Attack Dog", "Spy",
    "Flak Trooper", "Tesla Trooper",
    "Sniper", "Tanya",
    "Desolator", "Chrono Legionnaire", "Guardian GI", "Crazy Ivan",
    "Grizzly Tank", "Rhino Tank", "Type 99 Tank",
    "Flak Track", "IFV",
    "Prism Tank", "Tesla Tank", "Mirage Tank",
    "V3 Launcher", "Apocalypse", "Terror Drone",
    "Intruder", "MiG", "Black Eagle",
    "Kirov Airship", "Rocketeer",
    "Destroyer", "Typhoon Sub", "Aegis Cruiser",
    "Sea Scorpion", "Dreadnought", "Aircraft Carrier", "Amphibious Transport",
};

static const char* BLD_EN[(int)BldType::COUNT] = {
    "Construction Yard",
    "Power Plant", "Tesla Reactor", "Nuclear Reactor",
    "Barracks", "War Factory", "Ore Refinery",
    "Radar Tower", "Battle Lab",
    "Airforce Command",
    "Naval Yard",
    "Pillbox", "Sentry Gun", "Prism Tower", "Tesla Coil",
    "Flak Cannon", "Grand Cannon",
    "Patriot Missile",
    "Wall",
    "Ore Purifier", "Industrial Plant",
    "Nuclear Silo", "Weather Device", "Iron Curtain",
    "Chronosphere",
    "Tech Oil Derrick", "Hospital", "Machine Shop",
};

static const char* SW_EN[(int)SWType::COUNT] = {
    "Nuclear Missile", "Lightning Storm", "Iron Curtain", "Chrono Shift",
};

static const char* FACTION_EN[3] = {"Allies", "Soviet", "China"};

static const char* MISSION_EN[3][2] = {
    {"Border Skirmish", "Soviet forces cross the border. Build your base, repel reinforcements, and eliminate all enemies."},
    {"Coastal Defense", "An Allied fleet approaches from the sea. Build your navy and eliminate all enemies."},
    {"Decisive Moment", "Soviet and Allied forces surround the great lake. Hold for ten minutes until the counterattack."},
};

const char* unitName(UnitType t) { return g_lang ? UNIT_EN[(int)t] : unitDef(t).name; }
const char* bldName(BldType t) { return g_lang ? BLD_EN[(int)t] : bldDef(t).name; }
const char* swName(SWType t) { return g_lang ? SW_EN[(int)t] : swDef(t).name; }
const char* factName(Faction f) { return g_lang ? FACTION_EN[(int)f] : factionName(f); }
const char* missionName(int i) {
    if (g_lang && i >= 0 && i < 3) return MISSION_EN[i][0];
    return missionTable()[i].name;
}
const char* missionBrief(int i) {
    if (g_lang && i >= 0 && i < 3) return MISSION_EN[i][1];
    return missionTable()[i].brief;
}

// ===================== 按键显示名 =====================
const char* keyName(int key) {
    static char buf[4][24]; // 轮换缓冲（同一表达式多次调用安全）
    static int bi = 0;
    bi = (bi + 1) % 4;
    if (key >= KEY_A && key <= KEY_Z) { snprintf(buf[bi], 24, "%c", 'A' + (key - KEY_A)); return buf[bi]; }
    if (key >= KEY_ZERO && key <= KEY_NINE) { snprintf(buf[bi], 24, "%c", '0' + (key - KEY_ZERO)); return buf[bi]; }
    if (key >= KEY_F1 && key <= KEY_F12) { snprintf(buf[bi], 24, "F%d", key - KEY_F1 + 1); return buf[bi]; }
    if (key >= KEY_KP_0 && key <= KEY_KP_9) { snprintf(buf[bi], 24, "KP%d", key - KEY_KP_0); return buf[bi]; }
    switch (key) {
        case KEY_SPACE: return "Space";
        case KEY_ESCAPE: return "Esc";
        case KEY_ENTER: return "Enter";
        case KEY_TAB: return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_INSERT: return "Ins";
        case KEY_DELETE: return "Del";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_PAGE_UP: return "PgUp";
        case KEY_PAGE_DOWN: return "PgDn";
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case KEY_LEFT_SHIFT: return "LShift";
        case KEY_RIGHT_SHIFT: return "RShift";
        case KEY_LEFT_CONTROL: return "LCtrl";
        case KEY_RIGHT_CONTROL: return "RCtrl";
        case KEY_LEFT_ALT: return "LAlt";
        case KEY_RIGHT_ALT: return "RAlt";
        case KEY_MINUS: return "-";
        case KEY_EQUAL: return "=";
        case KEY_LEFT_BRACKET: return "[";
        case KEY_RIGHT_BRACKET: return "]";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_COMMA: return ",";
        case KEY_PERIOD: return ".";
        case KEY_SLASH: return "/";
        case KEY_BACKSLASH: return "\\";
        case KEY_GRAVE: return "`";
        case KEY_KP_ADD: return "KP+";
        case KEY_KP_SUBTRACT: return "KP-";
        case KEY_KP_MULTIPLY: return "KP*";
        case KEY_KP_DIVIDE: return "KP/";
        case KEY_KP_ENTER: return "KPEnter";
        case KEY_CAPS_LOCK: return "CapsLock";
        case KEY_NUM_LOCK: return "NumLock";
        case KEY_SCROLL_LOCK: return "ScrLock";
        case KEY_PRINT_SCREEN: return "PrtSc";
        case KEY_PAUSE: return "Pause";
    }
    return "?";
}

// ===================== 字体字模收集 =====================
void appendAllFontText(std::string& out) {
    for (int i = 0; i < (int)S::COUNT; i++) { out += TBL[i][0]; out += TBL[i][1]; }
    for (int i = 0; i < (int)UnitType::COUNT; i++) { out += unitDef((UnitType)i).name; out += UNIT_EN[i]; }
    for (int i = 0; i < (int)BldType::COUNT; i++) { out += bldDef((BldType)i).name; out += BLD_EN[i]; }
    for (int i = 0; i < (int)SWType::COUNT; i++) { out += swDef((SWType)i).name; out += SW_EN[i]; }
    for (int i = 0; i < 3; i++) { out += factionName((Faction)i); out += FACTION_EN[i]; }
    for (int i = 0; i < (int)missionTable().size(); i++) {
        out += missionTable()[i].name; out += missionTable()[i].brief;
        out += MISSION_EN[i][0]; out += MISSION_EN[i][1];
    }
    // 窗口标题与杂项中文
    out += "OpenRA2 - 共和国之辉 复刻";
}
