#pragma once
// ===================== 外部素材覆盖系统 =====================
// 游戏启动时优先从 assets/ 目录加载外部素材，缺失时回退到内置程序化生成。
// 用户可将自行提取/制作的 RA2 风格素材按以下命名约定放入对应目录即可生效。
//
// 目录与命名约定（所有 PNG 均为 RGBA，阵营色区域用纯红 (255,0,0) 表示，运行时替换为玩家颜色）：
//   assets/sprites/
//     tile_<terrain>_<variant>.png   地形瓦片 64x32，terrain: clear|rough|water|ore|gems|bridge，variant: 0..3
//     overlay_<name>.png             装饰物，name: tree1|tree2|tree3|rock1|rock2
//     unit_<name>_d<0..7>.png        载具/舰船/飞行器，8 方向（0=东 顺时针）
//     unit_<name>_d<0..7>_f<0..1>.png 步兵（含行走帧）；载具采矿车满载为 f1
//     turret_<name>_d<0..7>.png      炮塔（与车身同锚点）
//     bld_<name>.png                 建筑（锚点：底部中心）
//     bld_<name>_scaffold.png        建筑脚手架（建造中）
//     icon_unit_<name>.png           单位图标 56x44（侧边栏）
//     icon_bld_<name>.png            建筑图标 56x44
//     fx_explosion_<0..11>.png       爆炸帧
//     fx_smoke_<0..5>.png            烟雾帧
//     fx_muzzle.png                  枪口焰
//     fx_proj_<0|1>_d<0..7>.png      炮弹(0)/导弹(1)
//   assets/sfx/
//     <sfx>.wav 或 <sfx>.ogg         音效，sfx 名见 sfxName()（如 cannon / explosion / tesla）
//   assets/music/
//     *.ogg / *.mp3 / *.wav          背景音乐，多首随机轮换；空目录时使用内置合成进行曲
//
// 单位/建筑内部名由 unitAssetName()/bldAssetName() 给出（英文小写，如 grizzly / tesla_coil）。

#include "game/data.h"
#include "game/map.h"
#include "sfx/sound.h"

// ---- 地形/装饰 ----
inline const char* terrainAssetName(Terrain t) {
    switch (t) {
        case Terrain::Clear: return "clear";
        case Terrain::Rough: return "rough";
        case Terrain::Water: return "water";
        case Terrain::Ore: return "ore";
        case Terrain::Gems: return "gems";
        case Terrain::Bridge: return "bridge";
    }
    return "clear";
}
inline const char* overlayAssetName(Overlay o) {
    switch (o) {
        case Overlay::Tree1: return "tree1";
        case Overlay::Tree2: return "tree2";
        case Overlay::Tree3: return "tree3";
        case Overlay::Rock1: return "rock1";
        case Overlay::Rock2: return "rock2";
        default: return "none";
    }
}

// ---- 单位内部名（与 UnitType 一一对应）----
inline const char* unitAssetName(UnitType t) {
    switch (t) {
        case UnitType::MCV: return "mcv";
        case UnitType::Harvester: return "harvester";
        case UnitType::GI: return "gi";
        case UnitType::Conscript: return "conscript";
        case UnitType::PLA: return "pla";
        case UnitType::Engineer: return "engineer";
        case UnitType::AttackDog: return "attackdog";
        case UnitType::Spy: return "spy";
        case UnitType::FlakTrooper: return "flaktrooper";
        case UnitType::TeslaTrooper: return "teslatrooper";
        case UnitType::Sniper: return "sniper";
        case UnitType::Tanya: return "tanya";
        case UnitType::Desolator: return "desolator";
        case UnitType::Chrono: return "chrono";
        case UnitType::GuardianGI: return "guardiangi";
        case UnitType::CrazyIvan: return "crazyivan";
        case UnitType::Grizzly: return "grizzly";
        case UnitType::Rhino: return "rhino";
        case UnitType::Type99: return "type99";
        case UnitType::FlakTrack: return "flaktrack";
        case UnitType::IFV: return "ifv";
        case UnitType::PrismTank: return "prismtank";
        case UnitType::TeslaTank: return "teslatank";
        case UnitType::MirageTank: return "miragetank";
        case UnitType::V3Launcher: return "v3launcher";
        case UnitType::Apocalypse: return "apocalypse";
        case UnitType::TerrorDrone: return "terrordrone";
        case UnitType::Intruder: return "intruder";
        case UnitType::MiG: return "mig";
        case UnitType::BlackEagle: return "blackeagle";
        case UnitType::Kirov: return "kirov";
        case UnitType::Rocketeer: return "rocketeer";
        case UnitType::Destroyer: return "destroyer";
        case UnitType::Typhoon: return "typhoon";
        case UnitType::Aegis: return "aegis";
        case UnitType::SeaScorpion: return "seascorpion";
        case UnitType::Dreadnought: return "dreadnought";
        case UnitType::AircraftCarrier: return "aircraftcarrier";
        case UnitType::AmphTransport: return "amphtransport";
        case UnitType::ChronoMiner: return "chronominer";
        case UnitType::WarMiner: return "warminer";
        case UnitType::TankDestroyer: return "tankdestroyer";
        case UnitType::Terrorist: return "terrorist";
        case UnitType::DemoTruck: return "demotruck";
        case UnitType::Nighthawk: return "nighthawk";
        case UnitType::Dolphin: return "dolphin";
        case UnitType::Squid: return "squid";
        case UnitType::RobotTank: return "robottank";
        case UnitType::BattleFortress: return "battlefortress";
        case UnitType::Hornet: return "hornet";
        case UnitType::NavySEAL: return "navyseal";
        case UnitType::Yuri: return "yuri";
        case UnitType::ChronoCommando: return "chronocommando";
        case UnitType::PsiCommando: return "psicommando";
        default: return "unknown";
    }
}

// ---- 建筑内部名（与 BldType 一一对应）----
inline const char* bldAssetName(BldType t) {
    switch (t) {
        case BldType::ConYard: return "conyard";
        case BldType::PowerPlant: return "powerplant";
        case BldType::TeslaReactor: return "teslareactor";
        case BldType::NuclearReactor: return "nuclearreactor";
        case BldType::Barracks: return "barracks";
        case BldType::WarFactory: return "warfactory";
        case BldType::OreRefinery: return "orerefinery";
        case BldType::Radar: return "radar";
        case BldType::BattleLab: return "battlelab";
        case BldType::AirForceCmd: return "airforcecmd";
        case BldType::NavalYard: return "navalyard";
        case BldType::Pillbox: return "pillbox";
        case BldType::SentryGun: return "sentrygun";
        case BldType::PrismTower: return "prismtower";
        case BldType::TeslaCoil: return "teslacoil";
        case BldType::FlakCannon: return "flakcannon";
        case BldType::GrandCannon: return "grandcannon";
        case BldType::PatriotMissile: return "patriotmissile";
        case BldType::Wall: return "wall";
        case BldType::OrePurifier: return "orepurifier";
        case BldType::IndustrialPlant: return "industrialplant";
        case BldType::NukeSilo: return "nukesilo";
        case BldType::WeatherDevice: return "weatherdevice";
        case BldType::IronCurtain: return "ironcurtain";
        case BldType::ChronoSphere: return "chronosphere";
        case BldType::OilDerrick: return "oilderrick";
        case BldType::Hospital: return "hospital";
        case BldType::MachineShop: return "machineshop";
        case BldType::CloningVat: return "cloningvat";
        case BldType::ServiceDepot: return "servicedepot";
        case BldType::GapGenerator: return "gapgenerator";
        case BldType::SpySat: return "spysat";
        case BldType::PsychicSensor: return "psychicsensor";
        case BldType::BattleBunker: return "battlebunker";
        case BldType::TankBunker: return "tankbunker";
        case BldType::TechAirport: return "techairport";
        case BldType::SecretLab: return "secretlab";
        case BldType::CivHouse: return "civhouse";
        default: return "unknown";
    }
}

// ---- 音效内部名（与 Sfx 一一对应）----
inline const char* sfxAssetName(Sfx s) {
    switch (s) {
        case Sfx::Shot: return "shot";
        case Sfx::Cannon: return "cannon";
        case Sfx::Flak: return "flak";
        case Sfx::Missile: return "missile";
        case Sfx::Explosion: return "explosion";
        case Sfx::BigExplosion: return "bigexplosion";
        case Sfx::Tesla: return "tesla";
        case Sfx::Prism: return "prism";
        case Sfx::Click: return "click";
        case Sfx::Place: return "place";
        case Sfx::Ready: return "ready";
        case Sfx::Cash: return "cash";
        case Sfx::Alarm: return "alarm";
        case Sfx::Deploy: return "deploy";
        case Sfx::Sell: return "sell";
        case Sfx::NukeLaunch: return "nukelaunch";
        case Sfx::NukeBlast: return "nukeblast";
        case Sfx::Lightning: return "lightning";
        case Sfx::Storm: return "storm";
        case Sfx::IronCurtain: return "ironcurtain";
        case Sfx::SWReady: return "swready";
        case Sfx::Crush: return "crush";
        case Sfx::Eva: return "eva";
        case Sfx::NavalCannon: return "navalcannon";
        case Sfx::Torpedo: return "torpedo";
        default: return "unknown";
    }
}
