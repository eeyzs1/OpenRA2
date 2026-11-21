# 来源武器目录与棱镜分裂

本文封存 Scene32 批次的实现和证据。后续 Scene33 的格子扫描与保存格式变化见 [来源棱镜的格子扫描与入格顺序](native-shrapnel.md)，Scene34 / NativeIFV2 的乘员映射、维修及炮塔见 [IFV 实现记录](native-ifv.md)。Scene35 的共享时空控制器见 [时空武器记录](native-temporal.md)。Scene36 的乘员身份、要塞时空和城市驻军见 [乘员记录](native-crew.md)。下文距离排序及待处理清单描述的是当时版本。

本批为 SourceScene32 / NativeWeapons v1、NativeProduction5、World57 / App11、LAN49。来源规则的弹道不再依赖不完整的融合目录；棱镜的普通及精英分裂使用各自的来源武器。完整武器保真仍未完成。

## 资料边界与实现

`gen_native_weapon_catalog.py` 从当前已导入的武器槽和演员 DeathWeapon 出发，递归解析显式 ShrapnelWeapon / AirburstWeapon 引用。两个版本分别保留：

| 本地来源 | 武器 | 弹道 | 弹头 |
|---|---:|---:|---:|
| RA2 local.mix 基础资料 | 116 | 35 | 53 |
| YR localmd.mix 基础资料 | 164 | 37 | 72 |

这不是全部原始演员武器的导入清单，也不代表原版最终补丁版本。原始 SHA、名称、护甲倍率、范围、ROF、Burst、弹速，以及激光、声波、Temporal 和分裂引用均有独立审计。原始段落见本地 `review_out/weapon-actions-20261117/source-action-sections.json`。

`native_weapon_catalog_parse.cpp` 构建不可变目录，检查引用、重复名称、有限数值和有界无环图。单位槽的镜像数值必须与其目录项一致。`NativeActorDefinition` 与 World 共同持有目录，复制世界和保留载员定义时不会悬空。

`projectileForWeapon` 优先读取来源目录。FlakTProj 的 Arcing / Inaccurate 等字段因此进入现有发弹路径，地面 flak 的公共攻击指令会实际发射该来源抛物弹。现有弹速转换、飞行积分和散布公式继续使用，不能据此称为完整原版弹道。

`world_native_lasers.cpp` 负责确定性激光及分裂，`game_render_world.cpp` 只读取效果进行绘制。Comet 是 100 基础伤害、最多五枚 30 伤害 CometFragment；SuperComet 是 150 基础伤害、最多五枚 50 伤害 SuperCometFragment，后者再分出最多三枚 CometFragment。军衔加成作用于选中的主武器，子武器保持各自来源值。实际扣血还取决于原始护甲倍率。

直接命中建筑不触发该分裂分支。建筑可以作为周边分裂的接收对象；当前选择仍按距离和实体编号稳定排序。目标图在扣血回调前冻结并使用带代次的实体引用，避免槽位复用或新生对象意外进入旧图。原始格子扫描及弹丸更新时序的差异列在后文。

激光效果使用来源 LaserDuration 和房屋色，普通 Muzzle 提示被抑制。显示尺寸和 GPU 缓存不控制伤害、目标或随机数。

## 保存与网络

World57 的现有武器引用继续承载普通槽；独立目录武器使用 `2000000 + index`，由保存的 Scene32 源文重建指针。没有新增 World/App 二进制字段。旧程序会在不支持的 Scene32 处拒绝新来源保存；当前程序继续读取旧 Scene1..31，并保留其历史目录路径。

LAN49 隔离新的模拟行为。旧保存不会被磁盘上的新规则静默替换。六份新旧规则的真实 App 已分别在 Release/ASan 中续跑 5000 tick，实际 World/App 保存逐字节一致；其中四份有存活棱镜坦克，两份新规则输入中还有带来源绑定的在途弹丸。

## 验证记录

修改前的独立程序为 6 个控制通过、4 个机制失败：两版本都只有棱镜主目标扣血、没有五次分裂，并把地面 flak 回退到 Cannon。使用同一复现源、当前引擎对象和两版本原始规则后，十项全部通过。

永久专项在两种构建中各通过 42 条断言，覆盖真实攻击扣血、普通/精英分裂、建筑和友军排除、来源抛物弹、图校验、所有直接槽引用、独立目录弹丸、发射者阵亡、World/App 恢复及两种画面尺寸的实际像素贡献。相位冻结的防守者仅隔离机制，未作为自然对局证据。可见激光与移除激光的画面对照分别保存。

首次 Release 147/150，相关 ASan 52/55；两种构建的三项失败都是需要同步更新的夹具：规则重开和联机夹具只修改镜像伤害，合成 Scene28 则残留 Scene32 目录。首轮同时运行，涉及共享文件的最终检查改为跨构建串行。修正后 ASan 十一项全部通过；串行 Release 八项通过，三个网络检查的主机绑定失败，同一二进制/参数单独复测三项全部通过。端口失败的根因未确定，日志保留。没有第二次完整 150/55 全绿运行。

两种构建共 340 个非测试、非旧格式写入器对象在夹具修正前后逐字节相同。因此本批七局自然对局仍对应最终模拟实现：

| 场景 | 自然结束 tick / 观察结果 |
|---|---:|
| RA2 八人海岛，1717 | 158812 |
| YR 八人大陆，1701 | 349031 |
| YR 四人大陆，1701 | 360000，未决胜 |
| 德国/古巴 Hard | 23874 |
| 韩国/利比亚 Easy | 19466 |
| 法国/伊拉克 Brutal | 20762 |
| 英国/尤里 Hard | 8527，英国早败且严格经济门槛失败 |

七局 57 个 App 检查点精确。没有延长观察窗、删除阻碍或强制胜负。对局耗时来自并行功能验证，不是空闲性能成绩。

最终有 **60 对**跨构建实际保存逐字节相同：永久机制专项 12 对，六份真实 App 的 0/120/3000/5000 tick World/App 48 对。另有 24 份历史规则保存与上一批原始结果完全相同。首次进程采集未取得退出码，修正句柄捕获后十二个进程均退出 0，两轮 96 份保存也完全相同；未用推测值补写第一次退出码。

七局自然观察的旧 PowerShell 退出码字段同样没有可靠采集，因此只使用真实结果 JSON、保存和精确应用续跑作为观察证据，不把这些空字段视为进程成功证明。

全部续跑结束后，空闲 Release 性能七项通过，51.33 秒。最终 **10901 个冻结输入没有变化**。证据已封存于本地 `review_out/weapon-actions-20261117/validation.json` 和 `closed-batch.json`；总项目仍未完成。

## 尚未完成的原版行为

- 分裂的原始格子顺序、同一建筑多格命中、空缺时的地面光束和随机数推进、弹丸入队/更新时序；目前即时图不能等同原版逐帧结算。已从本地 RA2 程序只读提取半径 0..5 的 89 个顺序项，独立覆盖原始 CometFragment 的 3 和 SuperCometFragment 的 5；尚未替换当前距离排序。未运行原版入口，也没有独立 YR 二进制校准。
- 海豚 IsSonic / AmbientDamage 和 ChronoBeam.Temporal 现已保留，但沿途声波及持续时间抹除控制器尚未实现。不能把数据字段存在当作能力完成。
- IFV 的 RA2 十三/YR 十七组 WeaponN、精英及载员模式未导入；来源 `nativeWeapon` 的提前返回绕过现有普通 `ifvWeapon` 选择。工程师维修另有现有处理，也不代表完整 IFV 保真。
- 全部地形/高度碰撞、FlakScatter / Level 特殊分布、Airburst、死亡武器的完整执行、棱镜塔支援与其他专属动作仍需接通或校准。
- 武器 Report 还走通用音效枚举。原始 RA2 精英 SuperComet 的 Report 为空、YR 有 PrismTankAttack，这一差异尚未正确播放；新激光也不能据其外观声称原音效已还原。

[YRpp 的弹道定义](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/BulletTypeClass.h) 保留独立分裂武器，开发者的[分裂说明](https://phobos.readthedocs.io/en/latest/Fixed-or-Improved-Logics.html#shrapnel-enhancements) 说明了原本的建筑触发限制与多格建筑可被重复选取。其 [CellSpread 距离定义](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/CellSpread.h) 与本地 RA2 顺序表的覆盖范围一致。这些资料帮助核对字段与限制，不能替代原版实机验收。

冷态约三秒峰值、真实主循环、整局与跨机器/人工实玩，以及其余 37 个原版任务继续保留为项目未完成项。影片按用户要求排除。
