# 武器、弹头与装甲

摘自 YR `rulesmd.ini`。Verses 顺序固定为 11 项：

```text
None, Flak, Plate | Light, Medium, Heavy | Wood, Steel, Concrete | Special_1, Special_2
  ——步兵——          ——载具——              ——建筑——              ——特殊——
```

- `Special_1`：恐怖机器人等（偏“像步兵一样脆弱的机械”）
- `Special_2`：衍生火箭等（防互伤）
- Verses `0%`：不强攻/不还击/不被动索敌；`1%`：不强还击；`2%`：不被动索敌

---

## 1. 常用弹头 Verses（%）

| Warhead | N | F | P | L | M | H | W | S | C | S1 | S2 | 用途 |
|---------|---|---|---|---|---|---|---|---|---|----|----|------|
| SA | 100 | 80 | 80 | 50 | 25 | 25 | 75 | 50 | 25 | 100 | 100 | 机枪（GI/征兵等） |
| SSA | 100 | 100 | 100 | 60 | 40 | 40 | 75 | 50 | 25 | 100 | 100 | 强化轻武 |
| HE | 100 | 100 | 100 | 70 | 70 | 35 | 75 | 40 | 20 | 80 | 100 | 火箭/溅射 |
| AP | 25 | 25 | 15 | 75 | 100 | 100 | 65 | 45 | 60 | 60 | 100 | 坦克炮 |
| ApocAP | 25 | 25 | 25 | 75 | 100 | 100 | 100 | 100 | 70 | 60 | 100 | 天启主炮（更克防御） |
| UltraAP | 2 | 2 | 2 | 100 | 40 | 100 | 2 | 2 | 2 | 2 | 100 | 坦克杀手 |
| HollowPoint | 200 | 100 | 100 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 100 | 狙击/手枪秒步兵 |
| Electric | 100 | 100 | 100 | 85 | 100 | 100 | 50 | 50 | 50 | 200 | 100 | 特斯拉 |
| FlakWH | 150 | 80 | 50 | 100 | 100 | 20 | 0 | 0 | 0 | 100 | 100 | 防空高射（不对建筑） |
| MirageWH | 100 | 100 | 80 | 100 | 100 | 100 | 30 | 20 | 20 | 100 | 100 | 幻影热射线 |
| GrandCannonWH | 100 | 100 | 100 | 100 | 100 | 100 | 50 | 100 | 50 | 100 | 100 | 巨炮 |
| Controller | 100 | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 0 | 100 | 100 | 心控（建筑 0） |
| PsiPulse | 100 | 100 | 100 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 精神冲击（仅步兵） |
| RadBeamWarhead | 100 | 100 | 100 | 20 | 15 | 10 | 0 | 0 | 0 | 100 | 100 | 生化辐射炮 |
| Parasite | 100 | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 0 | 0 | 0 | 恐怖机器人 |
| ChronoBeam | 100… | … | … | … | … | … | … | … | … | … | 0 | 抹除（S2=0） |
| Organic | 100 | 100 | 100 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 仅有机 |
| V3WH | 100 | 90 | 80 | 90 | 70 | 70 | 100 | 100 | 50 | 80 | 0 | V3 爆炸 |
| DemobombWH | 100 | 100 | 100 | 100 | 50 | 50 | 80 | 150 | 10 | 100 | 100 | 爆破卡车 |
| NUKE | 100 | 100 | 100 | 200 | 100 | 100 | 60 | 100 | 8 | 100 | 100 | 核弹 |

完整列表见 rules `[Warheads]`（上百种变体：精英弹、IFV 变体等）。

---

## 2. 常用武器 Damage / Range / ROF

| Weapon | 单位示例 | Dmg | ROF | Range | Warhead |
|--------|----------|-----|-----|-------|---------|
| M60 | GI | 15 | 20 | 4 | SA |
| M1Carbine | Conscript | 15 | 25 | 4 | SA |
| Vulcan / Vulcan2 | Sentry / Pillbox | 50 | 26 | 5.5 | SA |
| PsychicJab | Initiate | 25 | 15 | 4.5 | SAFlame |
| GoodTeeth | Dog | 30 | 30 | 1.5 | ParasiteDog |
| DoublePistols | Tanya | 125 | 5 | 6 | HollowPoint2 |
| Sapper | Tanya C4 | 2500 | 100 | 1.5 | Mechanical |
| AKM | Boris | 65 | 20 | 7 | BORISWH |
| Virusgun | Virus | 125 | 100 | 10 | Virus |
| Punch | Brute | 100 | 60 | 1.4 | Battering |
| RadBeamWeapon | Desolator | 125 | 50 | 6 | RadBeamWarhead |
| MindControl | Yuri Clone | 1* | 200 | 7 | Controller |
| NeutronRifle | Chrono Legion | 8 | 120 | 5 | ChronoBeam |
| 105mm | Grizzly | 65 | 60 | 5 | AP |
| 120mm | Rhino | 90 | 65 | 5.75 | AP |
| 120mmx | Apocalypse | 100 | 80 | 5.75 | ApocAP |
| Robogun | Robot Tank | 65 | 60 | 5 | AP |
| MirageGun | Mirage | 100 | 70 | 7 | MirageWH |
| CoilBolt | Tesla Coil | 200 | 80 | 7 | Electric |
| PrismShot | Prism Tower | 120 | 45 | 8 | PrismWarhead |
| HoverMissile | IFV 空载 | 25 | 50 | 6 | HE |
| FlakWeapon | Flak Cannon | 40 | 20 | 12 | FlakWH |
| RedEye2 | Patriot | 75 | 55 | 12 | SAMWH |
| AGGattling | Gattling Tank 对地 | 25 | 16 | 5 | GattWH |
| AAGattling | Gattling 对空 | 25 | 16 | 8 | GattWH |
| Maverick | Harrier | 150 | 10 | 6 | ORCAAP |
| BlimpBomb | Kirov | 250 | 50 | 1.5 | BlimpHE |
| BlackHawkCannon | Nighthawk / 直升机空中 | 35 | 40 | 6 | SA |
| 160mm | Siege Chopper 部署 | 90 | 100 | 12 | SCHOPWH |
| DiskLaser | Floating Disc | 90 | 80 | 7 | DiskWH |
| MagneticBeam | Magnetron 吸车 | 5000 | 20 | 12 | LocomotorBeam |
| ChaosAttack | Chaos Drone | 600 | 30 | 3 | PsychGasCreate |
| 20mmRapid | War Miner | 30 | 20 | 5.5 | HARVWH |
| V3Launcher | V3（生成导弹） | 1 | 150 | 18 | Special |

\* MindControl 的 Damage 表示心控链数量语义，非普通伤害。

ROF：越大射击间隔越长。约中速下 ROF 15 ≈ 1 秒（社区近似）。

---

## 3. 军衔全局倍率（General）

| 参数 | 值 | 含义 |
|------|-----|------|
| VeteranRatio | 3.0 | 每级需击杀造价约 3× 自身（精英再一级） |
| VeteranCombat | 1.1 | 伤害 × |
| VeteranSpeed | 1.2 | 速度 × |
| VeteranArmor | 1.5 | 承伤（伤害÷） |
| VeteranROF | 0.6 | ROF 间隔 ×（射更快） |
| VeteranCap | 2 | 最高精英 |

多数单位：`VeteranAbilities=STRONGER,FIREPOWER,ROF,SIGHT,FASTER`；`EliteAbilities=SELF_HEAL,...`。精英常换 EliteWeapon。

---

## 4. 血条颜色

| 阈值 | 含义 |
|------|------|
| ConditionYellow | ≤50% HP → 黄 |
| ConditionRed | ≤25% HP → 红 |

---

## 5. OpenRA2 提示

项目已有装甲矩阵路径（见 `docs/ra2-fidelity.md`）。校准武器时应：`Weapon → Warhead → Verses` 三联映射，避免只用 VsInf/VsVeh/VsBld 三系数覆盖高科武器。
