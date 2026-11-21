# 原始 V3 / V3ROCKET

本地 all01t 原生场景现在为原图三辆 V3 安装独立发射器，生成真正的 V3ROCKET 飞行实体。它们保留原始初始位置、42/256 血量比例和 Sleep 指令，没有添加预置单位或替代任务事件。普通遭遇战 V3 仍使用原有规则；第一关尚未作为完整任务加入选关。

## 原始数据与实现

SourceScene v9 新增 V3ROCKET 运行时类型，总定义数从 105 变为 **106**；初始对象仍为 738、实际使用类型仍为 103。CMIN、DMISL 和 V3ROCKET 是三个额外运行时定义。`NativeRockets` v2 严格读取 1～16 组配置，拒绝重复或交叉的发射器/导弹类型；原 v1 单组格式继续支持。

| 项目 | 本机原始值 |
|---|---|
| V3 槽位 | Spawns=V3ROCKET；1 槽；SpawnRegenRate=400；SpawnReloadRate=0；NoSpawnAlt=yes |
| V3Launcher | Damage=1 是占位；ROF=150；Range=18；MinimumRange=5；Spawner=yes |
| V3ROCKET | AircraftType；Strength=50；special_2；Speed=15；ROT=3；不可选中 |
| 起飞 | PauseFrames=0；TiltFrames=60；PitchInitial=0.21；PitchFinal=0.5；SpawnDelay=2 |
| 飞行 | Acceleration=0.4；TurnRate=0.05；本图 Altitude=1550；LazyCurve=yes；BodyLength=256 |
| 发射点 | V3.PrimaryFireFLH=-160,0,75，保留负的纵向偏移 |
| 普通弹头 | Damage=200；V3WH；CellSpread=1；PercentAtMax=0.25 |
| 精英弹头 | EliteDamage=400；V3EWH；CellSpread=2；PercentAtMax=0.5 |
| special_1 覆盖 | 本图普通 V3WH 为 **50000%**；V3EWH 为 80%，分别保留 |

来源为本机 `rules.ini`、`art.ini` 和 `all01t.map` 的逐节合并。源文件哈希、核心/地图弹头差异及原始体素哈希见 `review_out/native-campaign/native-v3-source-audit.json` 和场景的 `scene-report.json`。地图 SHA256 为 `ad64c9293a8bccb85c38f5871a974ed9c09b8b5da11bb346e990423bf625c79c`。高倍 Verses 仅扩展专门导弹弹头解析，普通枪械和类别评分保留之前的解析范围。

[Ares 自定义导弹说明](https://ares-developers.github.io/Ares-docs/new/custommissiles.html) 明确了 AircraftType 导弹、暂停/抬头、速度和 LazyCurve 的弧线语义；[YRpp RocketLocomotionClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/RocketLocomotionClass.h) 提供阶段、速度、精英标志和俯仰状态的结构参考。这些不是本工程与原版逐帧一致的证明。

`world_native_rockets.cpp` 复用槽位和实体生命周期：零暂停直接进入抬头；原始 60 帧转换为 120 tick；离车释放槽位，400 帧再生转换为 800 tick。已发射的导弹锁定地面落点，独立存活，可由实际防空弹击毁；抬头中的导弹随发射车销毁。V3Launcher 的 1 点占位伤害不创建额外炮弹或附加直伤。

原生 V3 绕过旧原型的手动部署模式和自动部署分支，部署指令返回不支持，光标也不提示部署。此改动只在存在对应原生发射器配置时生效。

LazyCurve 当前采用确定性的制导弧线近似：爬升到配置高度，或剩余水平距离不足当前高差的 1.5 倍时，开始按来源 TurnRate 连续转向锁定落点；没有巡航平飞段。速度仍按原始百分比换算到 0～255 lepton 标度，并使用来源加速度和偏航参数。**这不是重建原引擎的抛物线方程**，峰高、转弯、碰撞与墙钟时序仍需原版实机校准。

## 图像与迷雾

V3 和空载 V3WO 使用本机 VXL/HVA，共用投影锚点和画布；槽位未就绪时切换空载车体。空载条件亦可核对 [Phobos 的 NoSpawnAlt 绘制分支](https://github.com/Phobos-developers/Phobos/blob/378e678617fd66d71d898c583031364cc8da9ebe/src/Ext/TechnoType/Hooks.MatrixOp.cpp#L713)。V3ROCKET 使用原始 VXL/HVA，导出 17 档俯仰 × 8 朝向的 136 帧，保留固定颜色和统一原点，不绘制普通飞机阴影。

导弹现在由 `game_native_actors.cpp` 在地面迷雾之后绘制，避免高空投影落在黑色迷雾区域时被遮掉。敌方导弹仍按其实际地面格的可见性过滤；裁剪使用抬升后的贴图边界。`game.cpp` 只增加一次绘制调用，模拟和绘图保持分离。

## 存档与验证

V3 批没有新增可变状态，写入 **World v40 / App v10**；SourceScene v9 内嵌两套不可变配置。实际升级前的 World40/Scene8 单组导弹存档继续加载，并与恢复后的 World 逐 tick 比较。行为协议提升为 **LAN v17**，避免不同模拟行为版本混连。

```powershell
python tools/ra2pack/gen_native_scene.py all01t.map
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_v3|native_rockets|native_rockets_assets|native_scene_assets)$' -V
```

`ra2.native_v3` 以清除其他单位的明确机制夹具测试实际命中；没有把这些夹具称为自然战役流程。检查包括：

- 5 / 5.5 / 8 / 12 / 18 格发射、过近/过远拒射、零暂停和 120 tick 抬头。
- 真实 50 HP 空中实体、负 FLH、单槽补弹、连续攻击的第二枚发射和原始空载车体。
- 普通/精英弹头对 flak 目标的实际 180 / 360 点伤害，及两种弹头对原始 special_1 雕像地基的不同结果。
- 原始 Patriot 的实际 75 点防空弹击杀，击毁后不产生地面爆炸伤害。
- 抬头/下降/补弹续档，发射车和目标销毁、实际编号复用及旧 World40 存档。
- 严格多发射器解析、两种分辨率的导弹像素出现/消失比较、敌方视野过滤和模拟校验不变。

2026-11-14 本批完整 Release **105 项中 104 通过、空战役矩阵 1 项按约定跳过、0 失败**，861.81 秒；包含原始四舰长攻击链、全部原生机制、普通玩法、历史存档、素材、HUD 和双进程联机。日志为 `build/native-v3-full-tests.log`，精简记录为 `build/native-v3-full-audit.txt`。测试前后的构建和关键场景输入哈希一致。

相关 **ASan Release 28/28 通过**，408.25 秒，日志未报告 AddressSanitizer 错误；覆盖导弹、原生队伍/事件、桥梁、普通战斗、部署、生命周期和续档。使用 `RA2_ASAN=ON` 的 Release 配置，非完整 ASan 测试集。日志为 `build-asan/native-v3-final-tests.log`，精简记录为 `build-asan/native-v3-final-audit.txt`。

执行配置、专项画面与二进制/输入哈希写入本地 `review_out/native-campaign/native-v3-validation.json`，最终结果以该记录和测试日志为准。

## 尚未完成

原版逐帧弹道、精确起飞/再生起点和 ROF/Burst 协调；目标过近时的原版自动退让；BodyLength 尾迹、V3TAKOFF/V3TRAIL、专属音效；非零 Reload 和其他生成器；导弹全部类型免疫、三维防空与地形碰撞细节。完整任务触发链、任务音视频、胜负和人工通关验收仍需继续。共享 DRED 路径的其他边界见 [原生导弹](native-rockets.md)。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA；当前写入 World v41，App v10 / SourceScene v9 保持，LAN 为 v18。完整范围、验证及剩余工作见 [原始目标与胜负](native-mission.md)，本文的既有测试成绩保留其历史范围。
