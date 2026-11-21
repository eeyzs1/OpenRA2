# 有边界的开发与验收

本项目以能正常开局、操作、交战、结算和续档的游戏循环为开发主线。完整原版复刻是产品方向；每个开发批次只承担明确、可验收的一部分。原始脚本研究、功能增加和结构整理分别立项。

## 一个批次的范围

动手前记录四项：玩家或开发者遇到的具体问题、涉及的模块、保持的合同、完成标准。先验证问题真实存在，区分代码阅读提出的风险与运行中已经复现的错误。审查请求先交付审查；得到实施授权后再修改。

本轮收敛包括合同、乘员字段、IO、容器遍历、归属入口、生产执行、测试构建、旧档导入和开发规范，以及基线已发现的固定对局失败。新增战役、扩展玩法、实验全局 AI 转正不属于本轮。

每次修改只处理一个原因。能保持语义的抽取先做；改变行为的修正单独给出触发条件和回归。模块分拆以职责和共享状态为依据，避免把同一段流程简单切成更多文件。新增抽象应消除实际重复，且不掩盖调用方的规则差异。

## 验证顺序

1. 记录现有失败，保存固定输入和原始用户文件；不把工作区的全部 Git 差异都归于当前批次。
2. 执行改动直接相关的检查。行为修复先构建能暴露问题的真实流程用例；夹具搭建失败不能算作复现成功。
3. 对涉及生命期、引用和二进制解析的改动执行相关 ASan 检查；对纯结构调整比较本次新生成的状态、回放和校验值。
4. 阶段稳定后，执行一次完整 Release 套件；测试构建调整还要检查 `BUILD_TESTING=ON/OFF`、产品入口和原有测试名称。
5. 对照范围逐项验收，报告实测与限制。相关检查完成后收尾，不因发现相邻研究题目而自动延长批次。

原生测试在 Release、ASan 之间也按串行执行，避免争抢模拟预算和污染性能结果。正常开局整局、机制夹具、渲染短测和人工实玩分别记录。机制通过不能证明整局结束，World tick 耗时不能等同游戏 FPS，自动点击不能等同人工体验验收。

定位未结束的固定自然对局时，可仅给回归子进程设置`OPENRA2_MATCH_DIAGNOSTICS=<新的输出目录>`。`--skirmish-regression ... --require-outcomes`在未分出胜负时保留该次World与App/AI现场；不改变地图、种子、模拟窗口或胜负断言。随后可用`--original-skirmish-regression <新的诊断目录> <现场文件>`只读导出实体和AI记录。诊断保存本身不构成通过。

给现场诊断进程设置`OPENRA2_INSPECT_CONTINUE_TICKS=N`时，只推进已加载现场的副本，并输出`continuation.csv`；原始World与输入文件保持不变。超出原自然窗口的续跑不能作为该局通过。

验证旧活跃矿车续档时，可仅给`ra2.source_economy`的回归子进程设置`OPENRA2_LEGACY_MINER_INPUT_DIR=<本次修复前运行保存的8份矿车World60目录>`。专项实际加载这8份Scene37文件，检查旧35/70收益和逐tick复演；不读取上次测试输出作为新行为通过依据。

验证旧采矿节奏时，仅给`ra2.source_economy`的回归子进程设置`OPENRA2_LEGACY_GATHER_INPUT_DIR=<本次修复前保存的4份Scene43/Production6采集中World60目录>`。专项读取实际17 tick中途快照，检查剩余3 tick和后续20 tick周期及逐tick续档一致性；这些历史行为不会被新规则自动升级。

验证当前停靠/转向批次前的卸矿续档时，仅给`ra2.ground_control`设置`OPENRA2_LEGACY_GROUND_INPUT_DIR=<修改前实际保存的矿车目录>`，读取其中4份`miner-0-{CMIN,HARV}-{ore,gems}-inflight-world60.sav`。专项确认Scene44/Production7保持旧卸载并逐tick复演。当前机制入口为`python -X utf8 -B tests/verify.py --build build --tests ra2.ground_control ra2.source_economy`；原程序角度样本、双矿车争用、部分混装、各停靠阶段、命令打断、目标易主/销毁、App原子拒绝与实际渲染分别验收，不能以样本通过代替自然对局或人工实玩。

`ra2.ground_travel`用实际移动、采矿与回厂指令检查弯行和满载回厂周期。测试记录实际坐标、车体角度、阶段和入账，并在弯中及七个矿车阶段逐tick续演。窄路直线掉头覆盖四个方向、跨格前后、通行出口和下单后续档，同一路段有另一辆车时检查原通行阶段。为回归子进程设置`OPENRA2_LEGACY_TRAVEL_INPUT_DIR`可再读实际生成的`turn-midway.sav`和`load-CMIN-20-0-0.sav`，验证World63续档和立即回厂语义。

`ra2.moving_yield`用两组相邻满格的步兵重现雪谷尽头的相向退路。两组交换队首，占位里至少有一名真正反向通行的单位。用例核对备用出口、步幅、同层单格三人上限、原命令终点、寻路预算和存档续跑。可用已有原版地图诊断入口从同一份未结算的App12分别续跑修复前后版本，按同一采样间隔核对实际脱困。固定自然对局从开局重新运行。

`ra2.ground_travel`也比较超时空矿车在泊位忙碌时单次和重复下达同一回厂指令的等候、充能、抵达和入账时点。对照用例从等候档续演，并检查确认自动选中的精炼厂及改点另一座厂。单独占用外侧格的夹具核对矿车首次传送直接到泊位、在泊位入账及外侧格腾空后驶离。可以用`OPENRA2_LEGACY_CHRONO_APRON_DIR`加载旧World64的最后一tick充能、刚到外侧和等候确认三份真档。回归覆盖矿车待跳与坦克宽弯的同档续演，也检查矿车转正前收到停止、移动或换厂指令的离位。编队移动用例让同组孤岛矿车提前触发连通性查询，核对刚落在泊位内的矿车仍会出泊、抵达编队格并保留货物。

双超时空矿车用例先用移动指令把一辆停在外侧格，两车再分别指定同一座厂。回归检查等候车沿地面让位、前车先出泊、两次入账、World70逐tick续演及伪署为World69的App原子拒绝。设置`OPENRA2_CHRONO_QUEUE_WORLD69_INPUT`可附加读取修复前该用例实际生成的`chrono-queued-exit-before-yield.sav`，核对初始状态并续跑至两辆车完成卸矿。

卸矿首笔用例检验默认第32 tick的批款、后续30 tick的批次间隔，以及World71刚进入卸矿和下一个tick的逐tick续演。设置`OPENRA2_LEGACY_UNLOAD_START_INPUT_DIR`可读取修正前实际生成的`dock-phase-4.sav`和`miner-current-stage-3.sav`两份World64，用保存的零计时于第30 tick完成首笔入账。

武装矿车正常卸空后的出泊用例记录开始计算离泊速度、首次位移、首次占外侧格、经过格心和释放预约时的速度。专项也检查重复确认、停止、外侧格实际阻挡，以及World73等待和驱动阶段的续演与伪署拒绝。设置`OPENRA2_LEGACY_DOCK_EXIT_INPUT`指向修正前实跑保存的World64 `dock-phase-5.sav`，可复验读取后的第一tick继续驶出。设置`OPENRA2_LEGACY_DOCK_WAIT_INPUT`指向上批实跑保存的World72 `dock-phase-5.sav`，可复验等待结束后按原定速轨迹驶出。

`ra2.ground_control`也从本次运行生成的World73分别载入等待出泊和刚开始驶出的武装矿车。同组另一辆车位于断开的地块，先尝试不可达的落点。用例对照同一现场的单车下令，核对编队移动获准、驶出前的泊位预约、正常抵达和下单后立即保存的逐tick续演。

同一回归还检查左右90度和180度原地转向期间的停止指令。它分别量测车体、炮塔和实际位置，验证停止姿态续档及重新启程。

过弯对照分别以ROT5和ROT10载入同一灰熊规则，使用普通移动指令检查相同路径上的位置、弯道进度和朝向，并分别测量原地对正的时间。给回归子进程设置`OPENRA2_LEGACY_CURVE_INPUT=<修改前实跑保存的World64弯中档>`可复验载入姿态、续演和到达。

追击过弯用例从普通攻击指令进入追击，在弯中捕获一次到达原出口的实际寻路重算。测试检查该tick的位移与切线朝向、重算前后的续档，以及对同一目标进入射程。

直角宽弯用普通移动指令实跑左右两侧，插值测量车头转至5度、45度、85度时的坐标，并检查两格心之间的弧长。回归在拐角交接前后读取World74，分别验证出口预先被占、弯中受阻、按同一出口改令、与超时空回厂或加速出泊同档，以及App伪署时的原子拒绝。设置`OPENRA2_LEGACY_WIDE_HEADING_DIR`可读取该目录的四份真实World68宽弯前后半存档和随附的World69混合档，核对原二次宽弯续演。设置`OPENRA2_LEGACY_WIDE_TURN_INPUT`可读取实际旧World64紧弯存档。

连续弯道用例验证两个相邻拐角通过普通移动指令衔接。第二弯的出口有友车时，检查进弯、在弯道末端等候、腾空后继续行驶和实际格占用。弯中和等候时分别保存并逐tick续演。

`ra2.ground_control`用两辆满载矿车检查一层坡差和两层陡坎的泊位，并在排队时验证续档。设置`OPENRA2_LEGACY_CLIFF_BAY_INPUT`可附加读取修复前实际生成的`cliff-bay-before-credit.sav`，检查旧档矿车带货驶出、改道和从可用泊位入账。

`ra2.ground_control`也检查卸矿时改点精炼厂、途中改点和重复点击、部分混装入账、目标销毁或换主、停止取消和World65续档。设置`OPENRA2_LEGACY_TRAVEL_INPUT_DIR`时，专项还会从实际World63中的卸货现场改点精炼厂，验证空车离位后按该档的回厂规则抵达。

混装电力用例在矿石和宝石入账之间增建或移除一座磁能反应炉。两组分别验证恢复供电和供电不足时的分批入账、供需相等时的精炼器增益，以及电力变化前后的续档和离位。

指定的泊位已有矿车时，`ra2.ground_control`用直接回厂和离位改道两种命令验收等候、实际卸货点和World66续档。专项还检查寻路额度用尽时接收命令并续档、自动回厂选空闲厂和App拒绝不匹配的目标引用。武装矿车的新版入泊检查使用真实回厂指令，观察第一次东向请求的泊位格、开始卸矿和实际驶出。设置`OPENRA2_LEGACY_DOCK_FACING_INPUT_DIR`可同时读取旧停靠存档。

## 常用命令

项目使用 C++23、CMake、raylib 和 Lua。Windows 配置与运行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
.\build\Release\ra2.exe --windowed
```

验证入口默认先构建，再执行准确的测试名称，并写出 `build.log`、`ctest.log`、`result.json`。默认输出目录唯一，不覆盖旧证据：

```powershell
python -X utf8 -B tests/verify.py --build build --list-tests
python -X utf8 -B tests/verify.py --build build --tests ra2.ai_attack ra2.ai_scouting ra2.attack_move
python -X utf8 -B tests/verify.py --build build --tests ra2.match_completion
python -X utf8 -B tests/verify.py --build build --all
```

`--list-tests` 只列出配置，不能证明测试执行。`--no-build` 明确使用已有二进制；只有刚完成对应构建时使用。验证器拒绝 `BUILD_TESTING=OFF` 的残留 CTest 配置、零测试、跳过、部分执行、非零退出，以及运行期间代码或二进制变化。

构建结构调整前，可将 `--list-tests` 输出保存为 JSON，再用 `--require-tests <文件>` 要求原测试名称仍存在。纯结构收敛可以添加 `--state-baseline <路径到SHA256的JSON>` 和 `--minimum-state-files <数量>`，只比较本次实际写出的状态；ASan 路径会映射对应的 Release 逻辑路径。行为有意改变时，应检查确定性与续档，不能继续要求旧策略的完整运行轨迹字节相同。

ASan 在单独目录配置；运行时需要与工具链匹配的运行库：

```powershell
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DRA2_ASAN=ON
python -X utf8 -B tests/verify.py --build build-asan --tests ra2.entity_identity ra2.lifecycle ra2.app_state
```

若运行库不在 PATH，给验证器追加 `--runtime-dir "实际MSVC工具链的bin目录"`。测试状态以实际进程退出、CTest 执行记录和 ASan 报告为准。不要把构建成功、缺库启动失败或旧二进制测试当作当前代码通过。

启用ASan的构建可以根据实际耗时单独设置进程的墙钟超时，保留原测试输入、模拟tick窗口和断言。先记录超时及已经完成的场景，再补验未完成检查；不能把超时改记为通过，也不能通过延长游戏时间或取消胜负要求掩盖卡局。

## 兼容与数据保护

- 场景定义、World 文件、App 封套和联机协议分别版本化；权威位置见[当前架构](architecture.md)。纯重构不升级文件格式。
- 一次尚未发布的语义修正批次按需要隔离联机协议；不要为每次编辑递增。格式变更必须解释新增数据和迁移边界。
- 保留确有历史文件或明确合同支持的读取分支；重复的版本判定集中到表。删除旧版本须单独定义退役范围，不能在清理重复时顺带删除。
- 用户存档先备份，只读验证，转换另存。GAME6 只接受已经验证的无脚本遭遇战/World29组合，不猜测未知旧格式。
- 证据采用一份批次基线、少量分项结果和最终验收。保留失败；不为每个小改动重新生成大量封存档案。

## 后续工作的准入

当前范围验收结束后，再选择一个玩家可感知的问题进入下一批次。优先修正常流程中的阻塞、崩溃、失同步和存档风险，其次处理已测得的性能瓶颈与局部耦合，最后增加内容和校准原版细节。

大型 ECS 替换、所有 World 字段私有化、全部原版脚本与战役移植都需要单独范围和成本评估。它们不作为本轮收敛的隐含后续步骤。
