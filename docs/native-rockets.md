# 原始 DRED / DMISL 发射与防空

盟军第一关的原始 DRED 现在生成真正的 DMISL 空中实体，经过暂停、抬头和飞行后，用地图覆盖的弹头结算命中；原场景的 Patriot 发射可飞行的防空弹攻击这些实体。两条路径均保留供电、射程、冷却和实际生命值。没有删除防空塔、改变联盟或提高对雕像的伤害来缩短测试。

以下主体保留最初 all01t 导弹批的参数、实现和历史成绩，当时仅为该地图安装配置。后续已接入第一关任务入口及正常指令流程，影片按用户要求排除；当前进展见 [任务记录](native-mission.md) 与 [来源规则包](rules-profiles.md)。自动化结果仍不代表原版弹道、时间和整关体验已经等价。

后续普通地图 Scene18 来源包已接通 RA2 DRED/V3 和 YR Boomer：保留各版本基础值、独立导弹实体、实际防空、存档和原始机体美术；Boomer 依选中武器槽区分普通鱼雷与巡航导弹。菜单规则选择、航母/反潜机和精确原版时序仍待完成，详见上述来源规则包范围。以下 World37/LAN17 等版本与测试数属于对应历史批次；当前为 World49 / App10 / 地图 Scene16 / 规则 Scene18 / LAN32。

## 来源与参数

`gen_native_rockets.py` 从核心 `rules.ini`、`art.ini` 和原图局部覆盖首批导出 SourceScene v6，当前为 v9。初始对象仍为 738 个、实际使用类型 103 种；CMIN 增援、DMISL 与 V3ROCKET 各增加一个运行时定义，合计 106 个，不增加预置舰船。V3 专项见 [原始 V3](native-v3.md)。原图 SHA256 为 `ad64c9293a8bccb85c38f5871a974ed9c09b8b5da11bb346e990423bf625c79c`。

| 项目 | 本地原始值 |
|---|---|
| DRED 生成器 | 2 个槽位；SpawnRegenRate=80；SpawnReloadRate=0；NoSpawnAlt=yes |
| DredLauncher | 射程 25，最小射程 8，ROF=50，原武器 Burst=2；不直接结算其占位伤害 |
| DMISL 生命与装甲 | 核心 Strength=50；本图覆盖为 **75**；special_2 |
| 暂停与抬头 | DMislPauseFrames=20；DMislTiltFrames=60；PitchInitial=0；PitchFinal=0.5 |
| 飞行 | Speed=20；ROT=4；Acceleration=0.8；TurnRate=0.08；本图 Altitude=1550 |
| 发射位置 | DRED.PrimaryFireFLH=30,43,92；DMISL.SpawnDelay=2 |
| 普通命中 | Damage=300；DMISLWH：CellSpread=1.5，PercentAtMax=0.25；对 special_1 为 **2%** |
| 精英命中 | EliteDamage=600；DMISLEWH：CellSpread=3，PercentAtMax=0.5；保留独立 11 装甲表 |
| 原场景 NASAM | RedEye2：75 伤害，ROF=55，射程 12，AAHeatSeeker，SAMWH |

参数审计位于 `review_out/native-campaign/dred-source-rules.json`，导出来源及哈希位于本地 `assets/maps/native/all01t/scene-report.json`。`native_rockets_assets_test.py` 独立读取 MIX，再比对输出参数、两张弹头表、机体和武器资料。

导弹按 AircraftType 建模的依据见 [Ares 导弹说明](https://ares-developers.github.io/Ares-docs/new/custommissiles.html)；暂停、抬头和俯仰字段亦有对应说明。生成器的槽位、阶段和计时结构参考 [YRpp SpawnManagerClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/SpawnManagerClass.h)，速度与俯仰状态参考 [YRpp RocketLocomotionClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/RocketLocomotionClass.h)。这些资料提供实现依据，不能单凭类成员证明本工程的阶段转移与原引擎完全一致。

Speed 按原始百分比转换到 0..255 lepton 速度，20 得到 51；不是把 20 当作每 tick 的格数。相关单位见 [OpenTS Speed](https://opents-developers.github.io/OpenTS/keys/speed/)。当前统一按原始 15 帧到项目 30 tick 转换：暂停 40 tick、抬头 120 tick、再生 160 tick。

## 模拟与持久化

`world_native_rockets.cpp` 管理发射槽、母舰跟随、阶段与飞行。暂停/抬头期间导弹随母舰实际位移和方向更新；离舰后锁定发射时的落点，独立加速、转向、爬升和俯冲。离舰释放槽位并开始再生；当前每舰同时允许一枚处于起飞准备中。发射的精英状态随实体保存，普通与精英弹头不会因后续母舰晋升而替换。

导弹可被普通对空索敌和实际防空弹摧毁，不能由玩家命令接管。母舰销毁时清除未离舰导弹，飞行中导弹继续飞行并清除来源引用；死亡、目标销毁和实体编号复用不会让新对象接管旧母舰或旧制导目标的引用。

命中通过公共 CellSpread 路径结算一次，不附加 DredLauncher 的占位伤害。按当前二维地基算法，300 点弹头在雕像 3×3 地基正中造成 22 点伤害：中心 6、四个边格各 3、四角各 1。这是本工程已验证的结算结果，尚不是原版三维伤害查表等价证明。

导弹批由 World v37（`RA2WLD37`）增加槽位、阶段、速度、俯仰、方向、落点、精英标志及防空弹的原始弹头引用；可变字段全部参与 checksum。读档从保存的 SourceScene 重建不可变资料，检查实体类型、引用、槽位对应、阶段计时、未来期限和浮点边界，不保存裸指针。常规武器批写入 World v40，继续读取 v39/v38/v37/v36 及更早已支持格式；普通地图兼容夹具覆盖到 v10/World v39。应用封套仍为 v10，LAN 模拟协议为 v17，见 [队伍命令](native-team-orders.md) 和 [类别攻击](native-team-attack.md)。

## 图像

导弹使用本地原始 DMISL.VXL/HVA，生成 17 档俯仰 × 8 朝向共 136 帧，所有帧共用体素原点和画布；几何、表面剔除与光照法线随俯仰旋转。DMISL 按原始 Remappable=no 保留固有颜色，并抑制普通飞机阴影。

DRED 无可用弹药槽时使用原始 DREDWO.VXL/HVA，两个舰体版本共用投影锚点和画布；只对原始调色板的队色索引生成遮罩，不能把红色像素一概重染。NoSpawnAlt 的语义参考 [Vinifera 原规则说明](https://github.com/Vinifera-Developers/Vinifera/blob/develop/docs/New-Features-and-Enhancements.md)。运行时帧选择和图像缓存不修改模拟状态。

## 验证与边界

```powershell
cmake -S . -B build
cmake --build build --config Release
python tools/ra2pack/gen_native_scene.py all01t.map
ctest --test-dir build -C Release -R '^ra2\.(native_rockets|native_rockets_assets|native_teams|native_events|app_state|state|net_protocol)$' -V
```

`ra2.native_rockets` 检查原始参数、最小射程、真实导弹实体、母舰实际移动、阶段期限、加速/再生、暂停/抬头/飞行续档、空载舰体、指令隔离、母舰销毁/编号复用、原场景防空的实际击杀、非法状态拒绝，以及两个显示尺寸的渲染确定性。阶段隔离夹具明确用电力破坏暂停既有防空；独立的原始开局与队伍攻击检查保留原始供电和完整防御。

`ra2.native_teams` 另跑完整原始四舰攻击，直到雕像被摧毁；不通过修改血量或倍率满足旧的短时测试。当前专项首次受击 tick=1303、摧毁 tick=27709、初始生命 5117，两个 World 逐 tick 一致。这些是当前工程诊断值，不能当作原版应有时刻。

2026-11-14 导弹批（World v37）完整 Release **99 项中 98 通过、空战役矩阵 1 项按约定跳过、0 失败**，919.30 秒。完整证据为 `build/native-rockets-full-tests.log`，精简断言为 `build/native-rockets-full-audit.txt`；包含原始四舰完整攻击、导弹、原生地图/事件、历史存档兼容、普通战斗、资产流水线和双进程联机。防空专项在 tick=124 记录到对导弹实体的致命伤害。后续队伍命令批的当前验证见上述队伍命令记录。

相关 ASan **23/23 通过**，640.30 秒，日志未报告 AddressSanitizer 错误。覆盖导弹、事件、原生场景/地图、桥梁/修桥/闸门、历史存档、实体生命周期、普通战斗/连发/范围伤害、编队/乘员、AI 侦察/防空和网络协议；原始四舰的长攻击链由上述 Release 完整回归覆盖，ASan 使用有界导弹专项。这不是完整 ASan 或原版整关通关证明。日志为 `build-asan/native-rockets-final-tests.log`，精简断言为 `build-asan/native-rockets-final-audit.txt`。

本地 `review_out/native-campaign/native-rockets-validation.json` 保留本批执行范围及二进制、场景和关键源文件的 SHA256。专项画面位于 `build/native-rockets-audit/` 和 `build/native-teams-audit/`，包括 1280×720 与 1920×1080，已检查实际画面、姿态公共原点及舰体切换；均为机制检查画面，不能替代人工整局验收。

仍需完成的原版校准：

- 起飞槽位的精确轮换、再生起点、Burst/ROF 协调和原始墙钟时序；当前状态机是一项明确实现，尚未逐帧对照 RA2 实机。
- BodyLength 尾迹、非零 SpawnReloadRate 及航母等其他生成器。V3 的 LazyCurve=yes 已由独立弧线近似分支接入，原版弹道仍待校准；本图 DRED 的 LazyCurve=no、Reload=0，不构成所有配置的支持证明。
- 飞行使用连续浮点爬升/巡航/俯冲；原版轨迹、碰撞/地形避让及方向量化尚未校准。当前地形每级使用 104 lepton 对应屏幕高度，亦待原引擎证据和画面校准。
- 最小射程已限制实际发射，尚未实现目标过近时按原版行为自动后退并重新建立射界。
- 防空弹仍使用公共二维追踪/范围命中及既有速度路径，尚未还原全部三维高度、原始追踪加速、丢失目标和近炸细节。
- 原始 V3TAKOFF/V3TRAIL 动画、尾焰、武器专属声音、全部命中特效及导弹专属心控免疫等类型语义尚未完整接线；当前存在通用表现路径。
- 其他任务条件、脚本、局部变量、音视频、目标和胜负，以及第一关从开局到结算的人工验收。

SourceScene v8 的常规武器与四槽弹丸引用另见 [常规武器](native-weapons.md)。原生 DRED/DMISL 仍走本文件所述专门路径，不用普通枪械的虚拟发射器数值代替导弹。

后续 SourceScene v9 已接入 [V3/V3ROCKET](native-v3.md)，并修正共享导弹的绘制顺序：在地面迷雾后绘制一次，敌方仍受实际地面格视野约束。该批沿用 World v40/App v10，LAN 为 v17；当前完整验证见 V3 记录，以上 v37 成绩保留其历史范围。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA；当前写入 World v41，App v10 / SourceScene v9 保持，LAN 为 v18。完整范围、验证及剩余工作见 [原始目标与胜负](native-mission.md)，本文的既有测试成绩保留其历史范围。
