# 原版地表对象与覆盖物

盟军第一关现已接入原地图全部 344 个地表对象，以及 130 段沙袋、6 处岩石、9 段高架桥图像。加上原先 249 个单位/建筑，场景共有 738 个对象、103 个独立原始类型。新增的 35 种类型包含树木、灯杆、交通灯、矿井和覆盖物，均保留自己的原始身份。

后续批已接入 [高架桥双层通行](native-bridges.md)、[工程师修桥](native-bridge-repair.md)、[原始闸门开合](native-gates.md) 和 [部分作者事件与基地交接](native-events.md)，并补齐净空、小屋免伤和场景禁毁开关。完整事件链与坍塌仍未实现，任务未加入选关，第一关仍无通关证据。苏军第一关仍为地形导入。

## 原始素材与位置

`tools/ra2pack/gen_native_scenery.py` 由场景导出器调用，将 `[Terrain]` 的每条记录及非矿石覆盖物加入 `scene.ini`，此批使用 v2，后续修桥批扩展为 v3，闸门批为 v4，当前事件批为 v5。原始单位/建筑仍为前 249 条，不与地表计数混淆；Scene 元数据分别报告 `AuthoredActors=249`、`SceneryActors=489`。原始地图、房屋、单位/建筑导入见 [原版地图](native-maps.md) 和 [场景实体](native-actors.md)。

```powershell
python tools/ra2pack/gen_native_scene.py all01t.map
cmake --build build --config Release --target ra2
ctest --test-dir build -C Release -R '^ra2.(native_scene|native_scene_assets|native_scenery_assets|app_state)$' --output-on-failure
```

导出仍在忽略目录 `assets/maps/native/all01t/`，原版素材不进入源码仓库。`scene-report.json` 的 `scenery` 记录全部源 SHP、调色板、SHA-256、rules/art 定义和帧数；`Original` / `OriginalFrame` 保留原始记录及覆盖物帧。

素材选择依据 EA 公开的 [Loading.cpp](https://github.com/electronicarts/CNC_TS_and_RA2_Mission_Editor/blob/main/MissionEditor/Loading.cpp)：剧场扩展名 `.urb/.tem/.sno` 采用对应 iso 调色板，NewTheater `.shp` 路径采用 unit 调色板；`TIBTRE` 按编辑器数据保留 unit 调色板例外。帧画布、身体和配对阴影完整保留，并统一按 16/15 缩放。绘图偏移参照 [IsoView.cpp](https://github.com/electronicarts/CNC_TS_and_RA2_Mission_Editor/blob/main/MissionEditor/IsoView.cpp)，包括高架桥 9–17 帧额外上移及水平修正。

本关两组 `BRIDGE2` 覆盖物均使用原始第 9 帧，它们是完整桥体，之前的道路缺口来自漏画。没有改用损坏桥或通用生成桥素材。矿石仍保留在 Map 的实际数量中；此批不重复生成矿石实体。

## 实际模拟与观察记忆

- 地表对象通过共同的原生实体/定义路径进入 World，以明确的 `kind` 区分地表、普通覆盖物和高架桥图像。它们无所属玩家，不参与敌方自动索敌、生产、驻军或普通选择。
- 地表生命读取原版 `[General] TreeStrength=200`；沙袋读取自身 `Strength=100`，桥体读取 `[CombatDamage] BridgeStrength=1500`。免伤矿井采用其原始 `Immune=yes`；非法武器目标的岩石保留免伤处理。其他完整战斗规则仍待逐项校准。
- 树木进入实际占位；200 点 HE 的真实 CellSpread 伤害可以销毁树木，之后 A* 能以原树格为落点。当前采用整格脚印；原版 `TemperateOccupationBits` / `SnowOccupationBits` 的更细占位尚未实现。
- 沙袋销毁会清除实际覆盖物/阻挡状态，并按存活邻居更新连接位。邻接方向与本关全部 130 段源记录一致。损坏程度和连接位共同选择原始姿态。碾压尚未接入，不能把爆破验收当作碾压验收。
- 释放任何原生场景建筑占位后，会恢复仍存活的重叠阻挡者，避免销毁一个对象后从另一对象的脚印穿过。树木不再使用通用建筑的大爆炸。
- 失去视野后，墙体显示观察时的图像帧；隐藏邻居被毁不会提前改变记忆中的墙头。雷达也读取同一观察记忆和原始 RadarColor，原始房屋采用其真实 RGB。
- 原生单位及驻军升级按各自定义的造价累计阈值，不退回遭遇战模板；实际源图谭雅为 1000，既有模板为 1500。原生任务房屋不再因空开局、基地移交或地表被毁而被遭遇战规则自动判负，胜负留给尚待接入的原始任务事件。

当前应用写入 v10（`RA2GAM10`），为本地记忆增加 `nativeFrame`，读取 v9/v8/v7；旧封套缺少此字段时保留兼容默认值。World v36（`RA2WLD36`）保留分层、原生定义/身份、修桥意图和运行时桥梁破坏开关，另存闸门阶段与事件实例。定义由源场景重建，HP、存活状态、覆盖物及连接位参与 checksum；观察帧和纹理缓存不进入 World 校验或 RNG。LAN 模拟协议为 v12。

## 验证与边界

Release 场景、原实体素材、新地表素材、应用存档四项通过，11.33 秒。素材专项逐条核对 344 条地表和 145 条覆盖物记录，验证 35 种源类型、全部身体帧的调色板像素和文件哈希；另明确检查剧场 iso 调色板与 TIBTRE 例外，不能只验证导出器自己选择的文件。

真实源图检查覆盖树木占位和破坏后的实际 A* 落点、免伤矿井、沙袋阻挡/连接变化、120 tick 续档一致性、隐藏连接变化的记忆冻结、v10 存读档和 GPU 上的旧墙体帧。v7/v29、v8/v30、v9/v31 的历史应用/World 夹具均通过。最终画面检查包含自由女神像、华尔街和盟军基地的两种分辨率；实绘不改变 World 或模拟 RNG。

Release 完整 91 项为 90 通过、空战役矩阵 1 项按约定跳过、0 失败，681.68 秒。此后又修正原生升级造价和任务房屋判负，两处收尾修改完成后的相关 10 项复跑全部通过，28.40 秒；没有将修改前的全量结果写作最终二进制的全量结果。新断言验证谭雅以原始造价在 3500 经验时升至一级，以及地表破坏不会自动淘汰初始空房屋。

相关 ASan 九项通过，209.69 秒，涵盖场景、地图、应用/战役存档、观察记忆、World、登载/卸载及网络协议；上述收尾修改后的场景、应用/战役存档和 World 四项再次通过，127.06 秒。它们不是完整 ASan、原版通关或人工整局验收。

初次实绘发现树木误用 unit 调色板，已按 EA 剧场选择路径修正；初次场景回归的两条失败断言也已纠正：地表可通行查询不包含建筑占位，AP 弹头伤害亦不能直接视作等额 HE 伤害。最终检查改为实际 A* 落点及 200 点 HE 伤害。

本机证据：`build/native-scenery-check-tests.log`、`build/native-scenery-check-detail.log`、`build/native-scenery-full-tests.log`、`build/native-scenery-final-tests.log`、`build/native-scenery-final-detail.log`、`build/native-scenery-audit.txt`、`build-asan/native-scenery-tests.log` 和 `build-asan/native-scenery-final-tests.log`。实际画面在 `build/native-scene-audit/`；导入参考源码与后续桥面/水下分层所需的原始桥梁网格在忽略目录 `review_out/native-campaign/`。

高架桥的分层寻路、占位、点击和绘图已在 [桥梁批](native-bridges.md) 接入，随后 [修桥批](native-bridge-repair.md) 补上工程师修复、净空与场景禁毁，[闸门批](native-gates.md) 补上入口开合。两座桥的岸边高度为 6、下方原地表为 2，基础水面/高度保持原样。部分交接已接入，完整坍塌与任务行为仍待实现；墙体碾压、矿石数量/生长、矿井动画、树木细分占位/死亡、桥梁战斗细节和完整规则也仍待接入。
