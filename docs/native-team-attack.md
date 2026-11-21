# 原始队伍类别攻击

原始脚本 Attack(0) 现按类别选择敌人，调用公共追击/攻击命令；目标被毁后继续寻找同类目标，没有目标时下一 tick 推进脚本。它与 Attack Waypoint(1) 的固定对象/格子语义分别执行。完整第一关尚未开放选关，以下是模拟机制验收。

## 原图记录与编组边界

第一关原图的 10 份脚本含类别攻击，使用 1（任意）、2（建筑）、4（步兵）、5（车辆）。原始脚本、TeamType、TaskForce、类型资料和来源哈希保存在本地 `review_out/native-campaign/team-attack-source-audit.json`。

两支 `097531FC` / `09752C8C` 队伍属于 BadGuy3，每支要 8 名 E2；但原图 22 名预置 E2 全属于 BadGuy1。启用原父触发器只建立这两支空队伍，不会借走盟友的兵。招募资料参考 [Team production](https://opents-developers.github.io/OpenTS/systems/ai-team-production/#recruitment)，目前没有实现跨房屋招募，没有改原图、补兵或修改编制。

专项另直接创建原图 `0BF26C8C`（BadGuy1，2 名 E2），保留其原始 `0A64854C` 脚本 `0,2 → 49,0 → 0,1`。桥梁脚本接入前的 World v39 中两兵在追击途中阵亡；常规武器批则在项目 tick 128 实际命中建筑。这些均是直接创建测试队伍的历史机制结果，不算第一关自然触发或通关，也没有将“必须阵亡”固定为验收目标。

后续 [原生 AI 批](native-ai.md) 按来源空队伍坐标和逐行招募修正候选选择。当前专项先招到西侧原有两名 E2；保留开局断桥时仍存在可达目标，不能再把断桥当作隔离全部敌军的证明。受控恢复桥面的机制分支在项目 tick140 实际命中，记录 7 点伤害。新的成绩与旧 tick128 记录分开，仍不代表自然 AI 或整关通关。

SourceScene v10 接入 [原始断桥](native-demolition.md) 后，本场景的建筑类别扫描在断桥区域找不到可达目标，不能继续硬性要求跨河命中。后续 [脚本控制批](native-script-control.md) 已接入原始指令 49，负例现验证空扫描后锁存成功并进入任意目标攻击；成功标志不代表实际跨河摧毁了建筑。正向追击夹具先执行原图爆破，再明确恢复两组源桥面后创建测试队伍，不改联盟、演员位置或原始脚本。该受控夹具在 tick 129 实际命中，单次伤害 5，并验证续档；工程师实际经过闸门修桥由断桥专项另行覆盖。连续消灭两个步兵、评分与目标保持仍由标记过的独立战斗场景验证。

## 目标资料与执行

SourceScene v7 为 70 个原始军队/建筑类型保存五项评分系数、SpecialThreatValue、LegalTarget、InvisibleInGame、Storage、Factory、UndeploysInto、伪装识别和主/副武器的射程、AA/AG、Verses。核心规则与地图局部覆盖单独核对；DRED/V3 的虚拟发射器没有显式 Verses，导出记录其默认值来源。ThreatPosed 不用作目标评分。

| 类别 | 当前选择范围 |
|---|---|
| 0 或未知值 | 无目标，下一 tick 推进 |
| 1、8 | 可攻击敌人 |
| 2 | 建筑 |
| 3 | 非零 Storage 的单位或建筑 |
| 4 | 步兵 |
| 5 | 车辆、停机飞机、可反部署建筑（基地除外） |
| 6 | 生产建筑 |
| 7 | 有主武器的建筑 |
| 9 | 正供电建筑，评分加供电项 |
| 10、11 | RA2 附加类别，当前明确阻塞 |

最后加入的有效成员从自身位置扫描。选择使用原始伤害倍率、目标剩余血量、专门威胁值与距离系数；同分按稳定实体顺序，空中候选先于地面。盟友、非法/隐藏类型、地形/覆盖物、不可识别伪装等不会当作普通敌人；玩家队伍须发现过地面目标，电脑战役队伍不借用玩家迷雾。

扫描与计分的实现参考 [Attack](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-attack/)、[target selection](https://opents-developers.github.io/OpenTS/systems/target-selection/) 和 [YRpp 类别枚举](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/GeneralDefinitions.h)。**这些参考不证明完整 RA2 实机一致性**：地面整图评分的 leptons 距离分支目前跟随参考实现，原版类别 10/11、移动区、原始对象枚举顺序、武器实时选择与评分调整仍需校准。原场景尚无运行时 declared enemy/anger 模型，EnemyHouseThreatBonus 与 OnlyTargetHouseEnemy 不冒充已实现；后者保留显式阻塞。

公共攻击命令拒绝路径时保留既定目标，30 tick 后重试，不把不可达误当作类别已清空。选定对象销毁或变为友军后重新选择；实体 ID 复用不会接管旧目标。没有弹药或攻击能力的结束分支保留在解释器中；完整空军资料、编队、重整与队伍反击仍需补齐。

选目标的原始武器资料不等于全部原始武器已经进入实战结算。普通单位武器、射程、伤害和 AI 行为仍须逐项接入和实机校准；当前开火走既有公共战斗系统。新生产实体的完整原始身份也尚待覆盖。

武器接入前的审计记录于 `review_out/native-campaign/native-weapon-source-audit.json`：E2 原始伤害 10、ROF 25 原帧，而当前资产规则伤害 15、冷却 25 项目 tick；按目前 15→30 Hz 换算应为 50 tick。E1 的 ROF 20 原帧应为 40 tick，当前为 20；Tanya 的 ROF 10 原帧应为 20 tick，当前为 5。这些直接武器差异已在 SourceScene v8 中接入实际结算，具体范围及剩余专属/驻军武器见 [常规武器](native-weapons.md)；本批目标机制成绩仍保留其原历史范围。

## 持久化与验证

本批以 World v39（`RA2WLD39`）新增“下一 tick 推进”标志，目标、游标、重试期限和标志均参与 checksum。继续读取 v38 及已有历史格式；普通应用夹具增加 v10/World v38，另验证升级前实际生成的原生 v38 存档。旧 SourceScene 没有目标资料时明确阻塞类别攻击，仍能读档续跑。该批应用封套为 v10，LAN 模拟协议为 v15。常规武器批写入 World v40 / LAN v16；当前 V3 批沿用 World v40，LAN 升为 v17（见 [原始 V3](native-v3.md)），继续读取 v39，见 [常规武器](native-weapons.md)。

```powershell
cmake -S . -B build
cmake --build build --config Release
python tools/ra2pack/gen_native_scene.py all01t.map
ctest --test-dir build -C Release -R '^ra2\.(native_team_attack|native_targets_assets)$' -V
```

专项还覆盖：评分与最近目标的反例、目标保持、原目标被毁后的同类重选、各角色类别、显式 LegalTarget 反例、选定后封路及恢复、待推进状态续档、非法状态/源资料拒绝、迷雾与伪装。独立战斗与封路场景使用标记过的原脚本副本，清空其他演员并保护测试队员；不当作原关卡截图或自然战斗结果。

本批完整 Release **102 项中 101 通过、空战役矩阵 1 项按约定跳过、0 失败**，833.40 秒。完整运行后仅修正覆盖报告对已实现类别 Attack 的识别，模拟代码没有变化；重建后的最终相关 Release **11/11 通过**，24.33 秒。

最终相关 ASan **25/25 通过**，631.74 秒，日志未报告 AddressSanitizer 错误。包含类别攻击、移动/接管、原场景/事件、桥梁/修桥/闸门、导弹、编队、生命周期、战斗、存档与网络；四舰长攻击链由上述完整 Release 覆盖。这不是完整 ASan 或原版整关通关证明。

本批历史完整 Release 日志为 `build/native-team-attack-full-tests.log`，报告修正后的相关日志为 `build/native-team-attack-final-related-tests.log`；ASan 日志为 `build-asan/native-team-attack-final-tests.log`。对应精简文件为同目录的 `native-team-attack-full-audit.txt`、`native-team-attack-final-related-audit.txt`、`native-team-attack-final-audit.txt`。当时构建与关键文件哈希位于本地 `review_out/native-campaign/native-team-attack-validation.json`。当前断桥批的测试夹具更正、验证及格式见 [断桥记录](native-demolition.md)；其受控恢复桥面的追击画面为 `build/native-team-attack-audit/repaired-bridge-conscript-pursuit-{1280,1920}.png`。源资料、机制场景和整关通关证据分别记录。

仍需完成：上述 RA2 精确语义、原始武器与视听校准、49 等任务切换/跳转指令、运输/卸载/空投、局部变量及相应条件、任务目标、媒体和胜负流程。不得仅据本专项开放“完整第一关”。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA；当前写入 World v41，App v10 / SourceScene v9 保持，LAN 为 v18。完整范围、验证及剩余工作见 [原始目标与胜负](native-mission.md)，本文的既有测试成绩保留其历史范围。
