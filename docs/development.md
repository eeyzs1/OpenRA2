# 有边界的开发与验收

本项目以能正常开局、操作、交战、结算和续档的游戏循环为开发主线。完整原版复刻是产品方向；每个开发批次只承担明确、可验收的一部分。原始脚本研究、功能增加和结构整理分别立项。

产品范围、交付顺序和下一批退出条件见[RA2完成标准与交付路线](ra2-completion-plan.md)。后续批次须对应总表中的可观察缺口；相关流程收敛稳定后执行全量验收，并按已关闭的玩家能力汇报进度。场景号、测试数量和原版样本数量分别说明版本与证据，不折算为整体完成率。

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

给现场诊断进程设置`OPENRA2_INSPECT_CONTINUE_TICKS=N`时，只推进已加载现场的副本，并输出`continuation.csv`；App输入还推进其复制的AI控制器。`paths.csv`记录全部已准入路径，`paths-after.csv`记录副本续跑后的路径，`traffic.csv`记录续跑后的交通门状态。路径包含单位编号和世代；原始World与输入文件保持不变。超出原自然窗口的续跑不能作为该局通过。

原版地图诊断的`actors.csv`还导出阻塞计时、下一路径点、实际移动层和该点的占位者。实体与续跑轨迹均附带`generation`，比较位移时用编号和世代共同识别单位，避免将槽位复用误判为瞬移。`ra2.ai_scouting`覆盖远处移动诱饵的首次获取、已有追击、近处目标、实际开火、建筑追击、全图已揭示和续档；设置上述`OPENRA2_LEGACY_CRUSH_INPUT_DIR`时也读取实际Scene46来源检查旧AI合同。

验证旧活跃矿车续档时，可仅给`ra2.source_economy`的回归子进程设置`OPENRA2_LEGACY_MINER_INPUT_DIR=<本次修复前运行保存的8份矿车World60目录>`。专项实际加载这8份Scene37文件，检查旧35/70收益和逐tick复演；不读取上次测试输出作为新行为通过依据。

验证旧采矿节奏时，仅给`ra2.source_economy`的回归子进程设置`OPENRA2_LEGACY_GATHER_INPUT_DIR=<本次修复前保存的4份Scene43/Production6采集中World60目录>`。专项读取实际17 tick中途快照，检查剩余3 tick和后续20 tick周期及逐tick续档一致性；这些历史行为不会被新规则自动升级。

验证当前停靠/转向批次前的卸矿续档时，仅给`ra2.ground_control`设置`OPENRA2_LEGACY_GROUND_INPUT_DIR=<修改前实际保存的矿车目录>`，读取其中4份`miner-0-{CMIN,HARV}-{ore,gems}-inflight-world60.sav`。专项确认Scene44/Production7保持旧卸载并逐tick复演。当前机制入口为`python -X utf8 -B tests/verify.py --build build --tests ra2.ground_control ra2.source_economy`；原程序角度样本、双矿车争用、部分混装、各停靠阶段、命令打断、目标易主/销毁、App原子拒绝与实际渲染分别验收，不能以样本通过代替自然对局或人工实玩。

`ra2.ground_travel`用实际移动、采矿与回厂指令检查弯行和满载回厂周期。测试记录实际坐标、车体角度、阶段和入账，并在弯中及七个矿车阶段逐tick续演。窄路直线掉头覆盖四个方向、跨格前后、通行出口和下单后续档，同一路段有另一辆车时检查原通行阶段。为回归子进程设置`OPENRA2_LEGACY_TRAVEL_INPUT_DIR`可再读实际生成的`turn-midway.sav`和`load-CMIN-20-0-0.sav`，验证World63续档和立即回厂语义。

矿车循环用例从相邻格下达真实采矿指令，并让矿车满载回厂、卸矿和返回同一矿点。两趟分别记录进入采掘和首笔采集的 tick 与格坐标，两处位置均须与目标矿格一致。双矿车用例核对目标被友方采掘占用时，后车改驶空闲矿格后装入第一份矿。

`ra2.crushing`通过真实移动命令检查14组RA2车辆/步兵碾压资格、部署、盟友和无敌边界，各组从本次保存的World逐tick对照600tick。另查定身保护、矿车从步兵所在桥面下方通过，以及有效App11载入后损坏标志或伪署Scene46时的原子拒绝。桥面用例是通行层机制夹具；原版规则和构造默认值的静态对照见`tests/fixtures/ra2-crushing-reference.json`。给回归子进程设置`OPENRA2_LEGACY_CRUSH_INPUT_DIR=<修改前实跑保存的目录>`时，读取14组同名`.sav`和`.final`，核对Scene46续跑600tick后的完整校验值。相关入口为`python -X utf8 -B tests/verify.py --build build --tests ra2.crushing ra2.ground_control ra2.native_bridge`。Scene48另用8组真实Move核对转向等待、实际接触坐标、斜行临时同格、同格三人、停止、混合保护占位、精确半格起点和左右宽弯；转向、接近、临时同格与死亡后的World分别与实际状态副本逐tick续演。非路径的落地/传送/让位查询仍保持严格占位。原版13组隔离执行见`tests/fixtures/ra2-crush-contact-reference.json`。设置`OPENRA2_LEGACY_CRUSH_CONTACT_DIR=<本次修复前实际保存的Scene47目录>`时，再核对14份同名旧档和`.final`的600tick完整校验值。两组历史目录可同时设置。两组真实Lua死亡回调还检查：回调删除后续目标并生出新单位时不追加碾压，以及移动者被删除后立即终止其余目标处理。隔离半格阈值不证明原版整帧碰撞调度、警告让路或死亡动画一致。

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

Scene49的`ra2.crushing`还通过真实斜向Move、Stop和八向步兵Move，核对车辆停在接触半径之外后步兵离格、车辆保持位置，以及下令后的600tick和格内中途的120tick续演。到达断言记录实际抵达时点和坐标，允许后续正常索敌。第九组保留定身保护；另查新来的步兵仍不能进入车辆所在格、相同XY的另一查询层仍有阻挡，非路径查询继续要求空格。设置`OPENRA2_LEGACY_INFANTRY_EXIT_DIR=<本批修正前实跑的Scene48目录>`时，读取`shared-infantry-ordered.sav`和`.final`，核对真实旧档的600tick完整校验值。原版Walk隔离机制对照见`tests/fixtures/ra2-infantry-sharing-reference.json`，未执行完整原版命令、寻路、警告让路或人工实玩。

碾压表现用例在六组真实Move死亡帧检查无Die1、对应数量的碾压音效及一次单位损失提示，另以普通致死伤害核对倒地动画。特效不参与World校验，故各移动阶段的120tick续演同时逐字段比较Effect，不能只比较checksum。八组`contact-*.trace`各保存初始状态及600tick模拟校验值，可与本次修改前实跑结果逐项比较。设置`OPENRA2_LEGACY_CRUSH_DEATH_DIR=<修改前实际输出目录>`时，读取真实`contact-0-3.sav`并检查其已编码倒地特效按剩余寿命结束；不能把已经播放1tick的旧动画当作全新20tick动画。原版调用链和依赖边界记录在`tests/fixtures/ra2-crush-death-reference.json`。该表现修正不增加场景、World/App格式或网络协议版本。

Scene50的`ra2.crushing`新增16组预警流程，覆盖实际AI/人工控制、已有移动、冻结、无出口、部署、开火、精英格、PlayerScatter/IQ覆盖、Fraidycat和偏心逃离。80组真实Move从`tests/fixtures/ra2-crush-warning-destinations.txt`读取原版落点，不用实现公式生成期望，并核对每次只消费一个散开随机数。普通自动让路不应产生玩家移动确认事件。原版对照有181组隔离执行，未执行完整原版帧调度或实体行走。

设置`OPENRA2_LEGACY_CRUSH_WARNING_DIR=<修改前Scene49实跑目录>`时，读取8组`warning-N-ordered.sav`、`.trace`、`.final`与`.stop`。`.trace`含初始状态和600tick校验值，`.stop`为原实际命令时点（0表示没有额外停止）；现有封存的封闭出口用例在第61tick停止车辆。测试逐项比较601个校验值，不能临时改输入时点以适配新结果。该目录可与旧Scene46/47/48及旧死亡特效目录同时设置。预警的进度使用现有移动节点，不增加World/App格式字段；Scene50规则头部与新策略字段需要一起验证。

`source_ai.py --runtime-profile`仍导出冻结的实验Scene41 / Production5。普通规则新增字段时，导出器必须剥离旧合同不支持的字段；Scene50的`NativeCrushWarning`段和`Type.*.Fraidycat`不进入该实验配置。`ra2.source_ai`检查RA2/YR导出及原文件不变，原生使用方继续按严格旧合同解码。

本批验收记录为`review_out/ra2-crush-warning-20261123/acceptance-audit.json`：最终完整Release为182/182，四项机制ASan与三项冻结AI使用方ASan通过，后者另执行两项导出夹具。机制ASan之后只修改了导出器和Python测试，原生输入未变。173份本轮压车文件跨稳定Release、机制ASan和最终完整Release逐字节一致。首次完整运行的三个旧配置失败、夹具错误和进度显示漏报均保留，不能把首次运行记为全通过。

Scene51的独立入口为`ra2.drive_blockage`（`ra2_regression --drive-blockage-regression <目录>`）。真实Move后由步兵进入既有路线再Guard，核对普通车辆、多功能步兵车、AI盟友和部署大兵让路；另查冻结、敌军免碾压步兵、无出口、既有移动与车头转正时点。九组有效World在下令后逐tick续跑600tick，五组响应中途和一组转向中途再续演120tick。编号8的手工桥面只是跨层查询的副本机制对照，不把派生缓存当成已编码的来源桥梁；真实原版桥梁的有效World续档另由`ra2.native_bridge`检查。普通规则当前拒绝作者桥梁类型，不能为了手工夹具放宽解码。

设置`OPENRA2_LEGACY_DRIVE_BLOCKAGE_DIR=<修改前Scene50目录>`时，读取两组`blocked-N-ordered.sav`、`.trace`和`.final`，比较初始值及后续600tick全部校验值，保持旧场景无强制反应的结果。纯抽取先用Scene50生成的173份实际文件证明字节不变，再进行Scene51行为修改；不会给纯抽取添加兼容开关。

`ra2.moving_yield`另查Scene51的已占临时等待区：两辆车的退让前缀指向同一等待格，一辆已到达格心，必须先让另一辆退出。用例保留真实命令的最终目标，检查1200tick内两车抵达、占位合法、血量、八次寻路预算和逐tick存档续跑。设置`OPENRA2_RECIPROCAL_BAY_INPUT=<本批首次失败的mp15s4-final-app12.sav>`时，加载原App12并在两份World与AI副本上推进相同1200tick，核对两辆实际被困坦克离位、完整World校验值一致、原世界不变及预算。该可选输入必须是App12；仅推进World的早期诊断只有一辆离位，记录保留，不把它作为完整对局恢复。固定自然对局另从原种子开局，在原180000tick内验收。

Scene51的上一检查点未验收：`review_out/ra2-crush-admission-20261123/natural-release-minimal/result.json`中雪谷固定局在原180000tick时仍有三方存活。原始失败、较宽交通候选和收窄候选的自然局分别保存在独立目录，不能覆盖或混记为通过。完整Release须在解决该门槛并稳定范围后执行。

该检查点的657份输入通过11项专项Release与7项相关ASan；208份新生成输出逐字节一致，39组旧场景实际续档仍精确。产品开关两侧的198份运行时输入及编译选项一致，发布程序均拒绝回归专用入口。状态记录为`review_out/ra2-crush-admission-20261123/verification-status.json`，其`accepted=false`；该记录不代替尚未执行的完整Release或仍失败的自然对局验收。

本次裂缝附近观察修正在`review_out/ra2-snow-outcome-20261123`验证。`--ai-scouting-regression <输出目录>`的九组机制覆盖地面、飞艇、卫星单独照亮、低电、共享视野开关、旧Scene50、重叠裂缝和卫星与现场观察并存；另查观察者离开后的重新遮蔽与中途存档。CTest将输出目录设在对应构建的`ai-gap-audit`，Release与ASan分别保存；隔离复现可用`OPENRA2_GAP_AUDIT_DIR`覆盖。设置`OPENRA2_LEGACY_GAP_INPUT_DIR`读取本次修改前的`gap-6-ordered.sav`、`gap-6.trace`和`gap-6.final`，比较真实Scene50初值及600tick的601个校验值。

原版地图副本诊断另输出`continuation-summary.csv`（各方建筑、单位、击败、产出、伤害及记忆数量）、`continuation-world.sav`与`brains-after.csv`。World保存不含复制的AI控制器，不能代替App12。诊断中产出/伤害计数从该次载入后累计；超出原窗口的续跑只用于定位。相同失败App12在修复前后续跑36000tick时，原先存活的四座玩家0建筑从全部保留变为全部摧毁；两次原文件哈希均不变。自然对局另从原种子开始，已在32321/173872tick内结算；最终完整Release为183/183，相关ASan为8/8，另有一次ASan现场副本诊断通过；本批验收记录见`review_out/ra2-snow-outcome-20261123/acceptance-audit.json`。

最终验收的657份代码/配置输入一致；238份本轮生成文件跨专项Release、ASan和完整Release逐字节一致，40组实际旧档的结果保持。自然局的14份本轮输出与预检一致，32321/173872tick结算均处于原180000窗口。BUILD_TESTING开关两侧的198份运行时编译输入与选项一致，保留182个原有测试并新增受阻散开，发布程序均拒绝两个回归专用入口。旧失败记录原样保留；后续Scene52校准平地覆盖集合，原版完整视野收集、周期与人工实玩仍未校准。

Scene52平地裂缝范围在`review_out/ra2-gap-reference-20261123`单独验证。`ra2.ai_scouting`读取本机原版循环实际产生的373个偏移，逐格核对64×64远程卫星地图；第120tick通过正常Sell命令移除裂缝，并比较完整600tick续档。红色复现的317/373、56格差异和真实Scene51存档原样保留。`OPENRA2_LEGACY_GAP51_INPUT_DIR`读取九组原Scene51观察/离开存档；`OPENRA2_LEGACY_GAP_AREA_INPUT_DIR`读取修改前的`gap-area-ordered.sav`、`.trace`、`.final`和`.commands`，按原实际Sell时点逐项比较601个校验值。这两类输入与已有40组历史结果同时检查。原始CSV不属于通用验证器的代码后缀集合，本批另记录并核对其哈希、修改时间和原程序输出，不能只凭JSON引用声称输入一致。

本批最终完整Release为183/183，专项Release为14/14，相关ASan为8/8。658份代码/配置与另行记录的捕获CSV保持一致；243份本轮状态输出跨三次运行逐字节一致，50组真实历史600tick结果保持。原地图在32321/173000tick内自然结算，种子922/923、180000窗口和胜负断言不变，App12检查点精确。BUILD_TESTING开关两侧的198份运行时输入和选项相同，保留183个测试名，四次发布入口拒绝均符合预期。完整回归第一次启动因参数名错误而未执行测试，记录与随后实际执行的`full-release-final`分开保留。验收见`review_out/ra2-gap-reference-20261123/acceptance-audit.json`。

Scene53高程裂缝的独立入口为`ra2.gap_height`（`ra2_regression --gap-height-regression <目录>`），CTest输出到对应构建的`gap-height-audit`。21组输入及原版实际地面查询结果分别在`ra2-gap-height-cases.csv`和`ra2-gap-height-masks.csv`；地形哈希也必须匹配源夹具。每组验证全4096格，在相对第120tick出售第一个裂缝，双裂缝组在第240tick出售另一个；在实际移除后检查剩余场和清空，比较600tick完整续档。双裂缝组另查独立保留第一个的副本及第180tick中途存档。原版证据通过提供电力状态移除源场，不据此声称原版出售动画时序一致。

`OPENRA2_LEGACY_GAP52_INPUT_DIR`读取先前验收的九组Scene52观察/离开续档和一组面积/出售续档。`OPENRA2_LEGACY_GAP_HEIGHT52_INPUT_DIR`读取本次修正前21组`height-N-ordered.sav`、`.trace`、`.final`和`.commands`；命令文件为`相对帧,x,y`，按实际记录重新绑定Sell，核对601个校验值。零级地面的新复现初始状态与前一面积用例相同，统计独立历史状态时须去重。旧的50组历史结果仍同时检查。新增原生测试保留Game单次初始化和跨配置串行执行；发布程序拒绝该专用入口。原程序数学、高度、邻格初始化及地面坐标/查询函数的具体边界见`ra2-gap-height-reference.json`。

Scene53最终完整Release为184/184，专项Release为15/15，相关ASan为9/9。660份代码/配置和三份另行固定的CSV输入一致；354份本轮输出跨三次运行逐字节一致。81次历史600tick续跑全部精确，其中零级地面起点与此前面积用例重复，不能称为81个独立起点。两张原地图在32321/173000tick自然结算，种子922/923、180000窗口和胜负断言未变；App12检查点精确，最终存档确认为RulesBaseVersion53。发布开关两侧198份运行时输入/选项相同，保留183个旧测试并新增`ra2.gap_height`，六次专用入口拒绝均符合预期。验收见`review_out/ra2-gap-height-20261123/acceptance-audit.json`。

Scene54坡面裂缝的独立入口为`ra2.gap_slope`（`ra2_regression --gap-slope-regression <目录>`），CTest输出到对应构建的`gap-slope-audit`。`ra2-gap-slope-masks.csv`直接保存原版查询捕获的315组遮蔽图，每组覆盖全部4096格；输入为基础高度0..14与坡面0..20的组合，结果共三种形状。机制夹具经既有原版地图入口加载mp15s4，保留合法画布和规则绑定，再设置既有可变高度/ramp；它不是一张复建的原版坡地。四组代表输入在第120tick执行真实Sell，记录实际移除，比较600tick续档。`OPENRA2_LEGACY_GAP_SLOPE53_INPUT_DIR`读取修复前这四组实际Scene53文件，复验旧计算；`OPENRA2_LEGACY_GAP53_INPUT_DIR`与`OPENRA2_LEGACY_GAP_HEIGHT53_INPUT_DIR`另读此前十组观察/面积/离开及21组高程的Scene53输入。旧Scene52与更早输入仍同时检查。CSV需单独固定哈希；失败、超时或未执行的检查均不能计作通过。

Scene54最终专项Release16/16、相关ASan10/10、完整Release185/185通过，662份代码/配置与四份另行固定的CSV一致。315组坡面输入全部吻合，捕获结果有三种遮蔽图；367份本轮输出跨Release/ASan/完整运行逐字节一致。116次历史600tick续跑精确，含继承的重复起点，不能当作116个独立起点。原种子922/923、原180000窗口的两张地图在32321/173000tick自然结算，两处App12检查点各自精确；最终档的RulesBaseVersion为54。发布开关两侧198份运行时输入/选项相同，保留184个旧测试并新增`ra2.gap_slope`，八次专用入口拒绝均符合预期。验收见`review_out/ra2-gap-slopes-20261123/acceptance-audit.json`。

Scene55运输准入的专项入口为`ra2.source_transport`（`ra2_regression --source-transport-regression <目录>`），CTest输出到对应构建的`source-transport-audit`。17组准入查询比较实际源规则容量/尺寸；其中预装载记录是明确供给的机制依赖，不作为旧版本正常装载的证据。六组行程通过实际Board或鼠标智能命令进入，分别检查履带车机器人、三机器人加步兵的容量竞争、夜鹰机器人及原有运输控制；第300tick实际Unload，在第180tick中途及初始状态保存，核对600tick完整续演。每组核对HP、击杀、军衔、经验、combatActor及来源对象身份与增益计时保留。`OPENRA2_LEGACY_TRANSPORT54_INPUT_DIR`读取修改前这六组实际Scene54命令后状态与轨迹；旧的拒绝和路线仍须精确，不改旧头部造档。原版证据仅执行Unit radio15、乘员尺寸求和及同盟谓词，没有执行完整鼠标、路径、装载/卸载调度或空运降落。

Scene55最终专项Release19/19、相关ASan15/15、完整Release186/186通过，664份代码/配置一致；继承的四份CSV另行固定。17组尺寸/容量查询、六组真实装卸及初始/中途存档续跑通过；422份本轮文件跨Release/ASan/完整运行逐字节一致。122次历史600tick续跑含六组实际Scene54运输输入和继承的重复起点，不能当作122个独立起点。原种子922/923、原180000窗口的两张地图在32321/173000tick自然结算，App12检查点精确，最终档的RulesBaseVersion为55。发布开关两侧198份运行时输入/选项相同，保留185个旧测试并新增`ra2.source_transport`，十次专用入口拒绝符合预期。验收见`review_out/ra2-flak-transport-20261123/acceptance-audit.json`。

Scene56乘员归属专项为`ra2.cargo_ownership`，独立入口`ra2_regression --cargo-ownership-regression <目录>`，CTest输出到`cargo-ownership-audit`。六组真实Board→尤里Attack→Unload检查履带车步兵/机器人、两栖船坦克/基地车、IFV炮手及嵌套IFV；在初始和180tick保存，续跑600tick。第200tick检查投影/存活库存，第210tick用Move让尤里离开以隔离二次心控，第300tick卸载后立即核对血量、击杀、军衔和combatActor，后续交战可正常改变属性。两个序列化曲线夹具分别检查World75与基础68/74共同续跑。另查异主货舱的玩家存续、死亡槽位的持久化/排除计数，以及App11的八类原子拒绝。

`OPENRA2_LEGACY_CARGO55_INPUT_DIR=<本次修改前实际Scene55输出目录>`可复验五组原始心控/卸载输入的601条校验值及终值，沿用当时只有第300tick卸载的指令表；新增MCV用例不伪称为修改前输入。显式归属采用World75而非覆盖World64..74字段，旧场景不会被读档自动升级。原版四组实际Gunner模式绑定与隔离易主检查及外围依赖见`tests/fixtures/ra2-cargo-ownership-reference.json`，不能代替完整原版帧循环或人工实玩。

乘员归属用例给观察乘员与尤里设置4000tick夹具保护，避免正确归属后的敌方炮火杀死观察对象；载具不受这项保护，心控仍由实际Attack取得。另用实际Board向已易主的履带车装入新玩家步兵，检查同舱原玩家/新玩家的0、1归属分别保存、计数和卸载，并续跑600tick。旧Scene55输入保留原有无乘员保护的字段与原指令表。

Scene56最终专项Release22/22、相关ASan18/18、完整Release187/187通过，666份代码/配置一致（紧凑排序JSON SHA-256 `6eb889c3245b832e1c3911b20c8363a4a863756a1f00ac4d648952d18af1d364`），四份继承CSV另行固定。六组真实心控/装卸、八类App原子拒绝、死亡货舱与基地车存续、基础68/74共同续跑通过；511份本轮文件跨Release/ASan/完整运行逐字节一致。127次历史600tick续跑含五组实际Scene55归属现场及继承的重复起点，不等同127个独立起点。固定地图原种子922/923、原180000窗口在32321/173000tick自然结算，两个App12检查点分别精确，终档RulesBaseVersion56；两局只随完整套件执行一次。发布开关两侧198份运行时输入/选项相同，保留186个旧测试并新增`ra2.cargo_ownership`，12次专用入口拒绝均符合预期。首轮专项中的硬编码Scene55伪署测试失败、修正后的复跑，以及原程序探针/早期夹具失败日志均保留。验收为`review_out/ra2-allied-transport-20261123/acceptance-audit.json`。


Scene57地面卸载的独立入口为`ra2.ground_unload`（`ra2_regression --ground-unload-regression <目录>`）。真实Board/Unload覆盖履带车三人、IFV一人、两栖运输三车，核对延迟首名、逐名释放与28～32tick间隔。另查重复Unload与单次指令的完整checksum相同、行进结束后卸载、Sleep载具实际唤醒、首名之后Move/Stop/Guard/Attack中断、准备时无出口和释放中受阻重试。初始与中途World均逐tick续跑600tick，World76与显式/继承归属及基础64/68/74共存；App检查任务身份、代际、阶段、计时、标志、长度和旧头部的原子拒绝，另查退休复用不带入任务。

设置`OPENRA2_LEGACY_UNLOAD56_INPUT_DIR`可读取本批修复前实际保存的三组Scene56 `*-ordered.sav`、`.trace`、`.final`并比较600tick。`OPENRA2_LEGACY_CARGO56_INPUT_DIR`读取上一批六组易主卸载、两组World75宽弯和一组混合归属的真档；`OPENRA2_LEGACY_TRANSPORT56_INPUT_DIR`读取六组Scene56登载/卸载旅程。它们与已有Scene46..55历史输入一起运行，不能把旧档改写为新规则后再宣称兼容。来源执行、有效红色复现与本批收据保存在`review_out/ra2-ground-unload-20261123`。原版探针的物理与输入对齐限制见参考JSON。


本批最终相关Release23+1项、ASan19+1项及完整Release188/188通过。后补的时空用例只改变一份回归文件，运行时与规则输入保持一致；补测和最终完整套件使用同一669项最终输入。603份本轮新文件（原601份加两份活动时空卸载存档）按这两阶段在Release、ASan与最终完整运行间逐字节一致，145次历史600tick续跑精确（继承记录包含重复起点，不能当成145个独立现场）。两张固定原地图沿用种子922/923和180000tick窗口，自然结算于32321/173000，各有两份精确App12检查点。BUILD_TESTING开关保留相同199项runtime编译输入与参数，187个旧测试名全部保留，14次shipping回归参数拒绝通过。首轮23项通过早于Sleep修正，其596份新文件在修正后全部字节不变；瞬间卸空与Sleep停滞的有效失败日志和真档保留。首次完整运行在129项后因旧时空夹具的即时卸载断言停止，记录作为失败且未完成保留；更新夹具后又补查World76/App11中的活动光束和卸载任务共存。验收收据为`review_out/ra2-ground-unload-20261123/acceptance-audit.json`。


Scene58的独立入口为`ra2.transport_exit`（`ra2_regression --transport-exit-regression <目录>`）。履带车、IFV及两栖运输通过真实Board、向北Move与Stop建立朝向，再Unload；检查东北车体、西南相邻落点和同向第二格的离舱目的地。原始131072组查询与160组释放捕获独立比较实际数学结果。受控第二格阻挡检查第二轮退路，正后近格单独开放仍须等待远格，转向中途Move/Stop继续经过既有指令准入。起转和中途World逐tick续跑600tick，App11保留World76的朝向及任务组合；无效IsTrain与错署Scene57拒绝且活动世界不变。

`OPENRA2_LEGACY_TRANSPORT_EXIT57_INPUT_DIR`指向本批有效红色复现的三份实际Scene57 `*-ordered.sav`、`.trace`及`.final`。另用`OPENRA2_LEGACY_UNLOAD57_INPUT_DIR`核对上一批12个流程和6种布局，`OPENRA2_LEGACY_TRANSPORT57_INPUT_DIR`核对六组真实登载旅程，`OPENRA2_LEGACY_CARGO57_INPUT_DIR`核对六组易主和三组混合归属/宽弯续跑。所有存档来自此前真实运行，不能改写场景号后宣称兼容。`Type.IsTrain`新字段也从实验Scene41导出中剥离；来源AI消费者检查仍与普通调度分开。


本批相关Release26/26、ASan24/24、完整Release189/189通过，均使用同一671项代码/配置输入与六份额外CSV输入。642份本轮状态文件跨三次运行逐字节一致，181次历史600tick续跑精确（继承记录含重复起点，不能视为181个独立现场）。原地图继续种子922/923与180000tick窗口，自然结算于32321/173000，各有两份精确App12检查点。BUILD_TESTING开关保留相同199项runtime编译输入/参数，188个旧测试名保留，16次shipping参数拒绝通过。原始131072组查询、160组释放、三组有效Scene57红色复现及探针构建/验证修正记录保留。验收收据为`review_out/ra2-transport-exit-20261123/acceptance-audit.json`。


Scene59同盟登车的专项入口是ra2.allied_boarding（ra2_regression --allied-boarding-regression <目录>），CTest写入对应构建的allied-boarding-audit。五组真实流程覆盖履带车、IFV、夜鹰、两栖运输和同舱混合归属；经典左键还检查实际联机命令排队及乘员/载具的世代绑定，现代右键经过实际输入路由。第240tick的乘员方Unload不得越权，第300tick由载具方正常卸载。初始与180tick存档逐tick续跑600/420tick，检查归属、血量、击杀、军衔、经验及combatActor。

九个保护检查覆盖敌军、中立、过大乘员、IFV尺寸、真实部署大兵、外方选择、实际装满载具以及YR/融合旧策略。三条流程在第45tick直接调整夹具既有AI结盟状态或通过changeActorOwner易主，另以两名盟友竞争一个IFV位置，检查到达时复查；这些关系调整是机制夹具，不声称有外交界面。三条流程在第30tick保存后续演570tick。混合归属的活动World76写入App11，比较后续600tick，并核对未知Scene60和含IsTrain的伪署Scene57在活动世界不变时拒绝。没有要求字段合同相同的59改署58必定拒绝。

OPENRA2_LEGACY_ALLIED_BOARD58_INPUT_DIR读取修改前五组实际Scene58失败流程的ordered/midway存档、轨迹及第300tick的命令表。ordered逐tick比较600tick，midway比较剩余420tick；不能把420tick统计为600tick。该批有效失败的setup、所有权/属性、控制权限和两段续演均通过，只有盟友准入及两个鼠标光标失败。原版证据、两次COM调用适配失败和修正后的探针均保留在review_out/ra2-allied-boarding-20261123；依赖与哈希见新参考JSON。


本批最终相关Release28/28、ASan26/26、完整Release190/190通过，均使用同一673项代码/配置输入及六份另行固定CSV。698份本轮状态文件跨三次运行逐字节一致；186次历史600tick续跑精确（继承记录含重复起点，不是186个独立现场），另有五份实际Scene58中途档的420tick结果保持。五组真实同盟登车/卸载、九个保护检查、三条途中复查流程和混合归属World76/App11续跑通过；两个损坏/不支持的App输入原子拒绝。原版14组查询/选定动作尾部与四组乘员列表/实际Gunner归属执行保留。固定原地图沿原种子922/923及180000tick窗口，在32321/173000自然结算，各有两份精确App12检查点，仅随完整套件运行。产品ON/OFF保持199项相同runtime输入和参数，189个旧测试名保留，18次发布入口拒绝通过。首轮相关套件27通过/1失败的硬编码Scene58夹具、零测试执行的缺头文件构建失败及COM探针调用修正记录均保留。验收收据为review_out/ra2-allied-boarding-20261123/acceptance-audit.json。


Scene60装载接触专项是ra2.transport_boarding（ra2_regression --transport-boarding-regression <目录>），CTest输出对应构建的transport-boarding-audit。12条真实Board行程记录实际退场坐标，覆盖八向步兵、IFV工程师、两栖运输坦克、夜鹰恐怖机器人和同盟步兵，初始及30tick档逐tick续演。200个位置查询读取原版捕获CSV，不由当前公式生成；CSV另行固定SHA256。八条600tick流程检查载具移动、Stop、改令Move、目标槽位复用、额外占位、偏离格心、驶向岸边和容量竞争。ra2.air_ground另检查真实飞行接人，保留邻格等落地、落地后同格装载及900tick存档复演；落地占位查询保持严格。额外占位是供给的重叠/恢复计时机制夹具，不能当作合法造车行程或原版机车证据。

设置OPENRA2_LEGACY_BOARD59_INPUT_DIR为本批native-repro/states可读取12份实际修正前Scene59 ordered存档及对应trace/final，验证完整600tick旧轨迹；不要用本轮新规则输出替代旧输入。有效App11接近现场续演600tick，未知来源版本和截断输入必须原子拒绝。建筑、YR和融合已有入口以及地面转弯/桥层检查分别运行。既有source_transport混装检查改为在原第300tick卸载前的299tick观察，600tick总窗口和卸载命令未变；allied_boarding满舱夹具等待真实接近完成后再查满员拒绝，不再假设相邻格一帧装满。


同格装载改变对局轨迹后，首轮完整Release为190通过/1失败：mp15s4/seed923在原180000tick窗口剩两方，七个残存单位均在进攻方从未观察的格，六个处于己方地面战斗单位不能连通的地形。只读诊断的6000tick副本仍剩五个，不计为自然对局通过。诊断的scout-grid.csv记录已有己方雾、观测时间和地形，导出前后副本World字节相同。

ra2.ai_scouting追加四个真实AI.update对照：Scene60与实际Scene59数据分别比较近处近期陈旧岛、远处更早观察岛或从未探索岛。旧排序让未探索孤立地形落后于反复重访，并清零已观察孤立地形的时间；新Scene60保留实际年龄并合并这两类的优先级。Scene59机制对照由实际旧档重建受控地形，不冒充历史整局续演。两套初始/已发令App11各续演600tick，同时比较完整AI状态和World；旧行为红复现与首个岛屿前提不成立的夹具均保留。

追加AI修正后按实际变化选择八项ASan：侦察、建造、地面与空中运输、App、冻结全局AI及两个来源运行消费者。前32项运输/移动ASan证据保留；World装载、移动、IO和归属源码未随AI排序再次改变。最终相关Release和完整Release生成完整新状态集合，追加ASan逐字节比较其实际产生的AI状态子集。


本批最终相关Release34/34、追加AI ASan8/8、完整Release191/191通过，使用同一675项代码/配置输入（紧凑排序JSON的SHA256为395eebadc237d7bb010283b71f5e31524becda93ae5b977f4f5613c7c468c11d）及七份另行固定CSV。834份新状态跨最终相关Release与完整Release逐字节一致；其中87份AI状态还与追加ASan逐字节一致。先前32/32机制ASan及818份一致状态的证据保留，随后World装载/移动/IO/归属源码未再改变；新增的中央常量只供AI排序使用。每次最终相关/完整Release的198次已记录历史600tick续跑保持（含重复起点，不是198个独立现场），另有五份Scene58中途档420tick结果精确。追加的两份Scene59侦察机制对照来自实际旧数据的受控重建，单独记录。

原版211个隔离执行、当前200位置查询、12条实际Board行程、八条600tick流程和四个孤立侦察对照通过。初始/已发令App11侦察现场各续演600tick，同时比较完整AI与World。最终两场固定原地图在原922/923种子及180000tick窗口内分别于31900/167436自然结算，各有两份精确App12检查点；预备候选对局另有记录。产品ON/OFF保持199项相同runtime输入与参数，190个旧测试名保留，20次发布入口拒绝通过。首次完整套件190通过/1失败的未结算现场、13份实际自然输出（失败局没有新的结算截图）、原版失败探针、装载/占位与侦察夹具失败均保留。收据为review_out/ra2-transport-boarding-20261124/acceptance-audit.json。未进行人工实玩；原版完整命令、机车、AI和整帧调度仍未校准。

`ra2.building_repair`从实际RepairBuilding指令检查10条来源维修流程：全局28tick相位、8HP步长、原始逐步费率、精炼厂赠车扣除、尾笔、足额与不足额资金；各流程保存初始及第13tick现场并核对600tick续演。另查维修开关、停电、补钱后重新开启、准入保护与活动App11的五个原子拒绝。设置`OPENRA2_LEGACY_BUILDING_REPAIR60_INPUT_DIR=<修正前实际输出目录>`时，再读取10份Scene60初始档及该次trace/final，核对其完整600tick旧结果。原版隔离探针和当前指令流程分别验收；修正前证据为`review_out/ra2-building-repair-20261124/native-red`。

现有`--play-test`菜单回归按来源步长与周期等待实际修满，保留原修满断言；本批菜单58项通过。此调整仅为测试准备，固定自然对局的180000tick窗口、种子和结算断言不变。

`ra2.building_sale`（`ra2_regression --building-sale-regression <目录>`）从10条真实SellBuilding指令核对本地原版金额与完成时入账，各保存下单/第10tick/完成档并续演600tick。两条额外行程真正支付2000生产精炼厂、合法放置、获得且保留一辆矿车、再出售返300；不通过免费建厂替代生产断言。六个控制覆盖重复/外方/维修、完成前销毁与过时命令、易主/中立和余额保护；完成前最后一tick的World与App11续演一致，八个坏包原子拒绝。原版268组执行的受控依赖与未覆盖边界见tests/fixtures/ra2-building-sale-reference.json。

设置`OPENRA2_LEGACY_BUILDING_SALE61_INPUT_DIR=<修正前本次实跑目录>`时，专项加载10份实际Scene61下单档及该次trace/final，逐tick核对其原来已即时返款的600tick结果，避免重复发款。修正前证据为review_out/ra2-building-sale-20261124/native-red；对应建筑出售新规则为Scene62 / Production11。相关入口是`python -X utf8 -B tests/verify.py --build build --tests ra2.building_sale ra2.building_repair ra2.source_economy ra2.native_production ra2.app_state`。

Scene62出售批次相关Release18/18、ASan7/7及完整Release193/193通过；225份选定新状态在全量逐字节复现，171份ASan状态相同。完整输入检查58/58及14张本次截图完成，用户设置、快速存档与原截图已恢复。固定原图沿原种子和180000tick窗口于31900/167436自然结算。证据为review_out/ra2-building-sale-20261124/acceptance-audit.json；原版全时序与人工实玩仍未验收。

Scene63继续使用ra2.building_sale。18条真实出售指令包含8条盟苏建造厂、损伤、AI、打包开关及真实changeActorOwner转移用例；每条保留下单、第10tick及完成状态并续演600tick。8条真实输入路径分别操作盟苏出售按钮或默认Delete，检查发出的世代绑定SellBuilding、完成返款、不生成MCV及渲染前后World不变；4条独立Deploy检查打包选项。额外最后工厂用例实际支付建筑进度，出售后暂停已付工作，并通过真实取消命令精确退款。

设置OPENRA2_LEGACY_CONYARD62_INPUT_DIR到本批native-red/states时，读取26条实际Scene62指令/输入后的World和该次trace/final，比较完整600tick；另从8份实际输入前状态重新执行当前输入代码，核对其发令及World与原Scene62结果相同。旧Scene61出售和Scene60维修目录仍同时启用。修正前8条指令及8条输入失败、原版r1缺PadAircraft依赖故障、r2的32项通过均保留在review_out/ra2-conyard-sale-20261124。所有这些是自动机制/输入证据，人工实玩仍未进行。

Scene63相关Release22/22、ASan9/9及完整Release193/193通过，336份选定新状态在全量逐字节复现，282份ASan状态一致。8份实际Scene62输入结果重新生成后与旧文件字节一致；26条旧轨迹完整600tick相同。完整输入58/58通过，14张新菜单截图及用户文件恢复完成；4张专门建造厂截图已目检。固定原图沿原种子及180000tick窗口于31900/167436自然结算。见review_out/ra2-conyard-sale-20261124/acceptance-audit.json；原版全时序、幸存乘员与人工实玩仍未验收。

## Scene64建筑出售撤出

独立入口为`ra2.building_survivors`（`ra2_regression --building-survivors-regression <目录>`）。10条真实SellBuilding覆盖盟苏建造厂、电厂、矿厂、无乘员防御、AI、占领与夺回；下单及撤出后保存，每条逐tick续演600tick。六个机制控制检查地形阻挡、三人满格、分配失败、撤出前销毁、第一次更新前易主及当前房屋阵营改变；失败尝试不会在出口腾空或读档后再发生。建筑保留创建时的阵营快照，允许当前房屋阵营改变，人数与类型分别读取这两种状态。受控来源规则将AlliedCrew指定为CTECH，核对真实身份、50HP、Pistola、不可训练、续档及选中渲染；这是可控规则检查，不是原版正常抽样频率证明。

World77/App11检查待撤出阶段的600tick续演及13组损坏记录的原子拒绝。现有cargo_ownership、ground_unload、ground_control、ground_travel继续断言实际基础64..74布局；解析封套后检查原字段，运输的COW1/TUN1与SLS1并存。不能把77当成75/76别名。`OPENRA2_LEGACY_SALE_SURVIVORS63_INPUT_DIR`可读取本批修改前实际生成的10条Scene63轨迹和结局，逐tick验证原无撤出行为。原版参考包含1,712次查询与12次出售任务前缀，明确区分真实指令、受控分配/Unlimbo依赖及项目时序。

验证目录为`review_out/ra2-sale-survivors-20261124`。使用`run_verify.py`固定历史环境和七份CSV；每次输出采用新目录，先保存LastTest.log再查询CTest清单。相关与完整运行的状态比较只计算本轮新文件的交集。完整套件保留原地图种子922/923和180000tick窗口，并恢复用户设置、快速存档和既有截图。

本批相关Release30/30、后补规则拒绝1/1、完整首轮修正Release4/4、ASan13/13及追加ASan4/4、完整Release194/194通过。完整运行复现941份选定新状态，前阶段760份、修正阶段195份ASan状态与各自Release逐字节相同（合并885份）；后补前后70份正常状态保持相同。最终683项代码/配置输入；30项相关门禁早于后补与完整首轮修正；最后4项Release/4项ASan及最终完整套件使用相同输入。首次全量191通过/3失败的真档与日志保留；修正创建阵营快照的验证及Temporal封套断言后，完整输入58项与14张新截图通过，用户设置、快速存档及既有截图恢复。固定原图沿原种子922/923与180000tick窗口，于31900/167436自然结算，各有2份精确App12检查点。产品开关保留200项相同runtime输入/参数、193个旧测试名及新增出售撤出入口，26次发布入口拒绝通过。前批5191份封存证据不变。验收收据为review_out/ra2-sale-survivors-20261124/acceptance-audit.json；原版完整调度、地图Unlimbo和人工实玩仍未验收。

## Scene65附近散开验收

ra2.building_survivors通过普通SellBuilding命令覆盖两座建造厂中央乘员离场、384组原版格/子位置/frame目的地、单次Scatter抽样，以及Stop、新Move、销毁、易主、封闭外侧与逐tick续档。设置OPENRA2_LEGACY_SCATTER64_INPUT_DIR指向本批修改前的真实Scene64出售World/trace目录，可逐tick核对十条600tick结果。ra2.crushing保留80条合成Scene64相邻格样本，并以实际Move加受控外侧地形封闭核对当前80条明确回退样本；它们不是自然对局。原版Nearby矩阵的CSV必须作为额外输入固定哈希。相关入口包含building_survivors、crushing、drive_blockage、group_orders、ground_control、ground_travel、moving_yield和app_state；稳定后执行完整Release。

自动返修的附加验收位于ra2.ai_attack/ra2.service：缺电/缺钱的新订单和已有订单、正常有偿工作、免费摘除寄生、玩家等待及Scene64策略均通过真实bot.update和World保存续演检查。雪地图首次固定窗口失败后保留原始World/App12和13份新自然输出；超窗诊断不计作自然对局通过。资金、种子922/923、180000tick窗口和胜负断言保持原值。

## Scene66附近高度选格验收

ra2.crushing从实际Move预警对照1,008条原程序高度选格，逐行记录目的地和一次[0,4]抽样。修改前258/1008匹配、750失败，准入与保存续演控制有效。OPENRA2_LEGACY_NEARBY_HEIGHT65_INPUT_DIR读取红测生成的六组真实Scene65高地预警输入/600tick轨迹；OPENRA2_LEGACY_NEARBY65_INPUT_DIR供building_survivors读取十条实际Scene65出售轨迹。两份高度CSV在通用工具外固定哈希；不能把供应桥面位控制当成原地图标志映射。

## 玩家步兵散开回归

`ra2.player_scatter`使用`ra2_regression --player-scatter-regression <目录>`，输出到对应构建的`player-scatter-audit`。固定CSV是原版实际命令回执，384行中仅sequence1和27映射到项目现有状态，96条检查目的地、路线保留和精确RNG。六条实际下令流程覆盖站立、已有Move、已部署、Guard、移动Fraidycat和无出口，含下单后及中途续档；另查X键入队、归属/世代/定身/输入锁、混合车辆与控制器清理。

`OPENRA2_LEGACY_PLAYER_SCATTER66_INPUT_DIR`读取六组实际Scene66的`journey-N-before.sav`、`.case`和`.trace`，重新绑定Scatter并比较命令后的初值及600tick全部结果，不能以新规则构造伪旧档替代。纯整理与行为改变分别保留证据；新回归入口只编译到ra2_regression，shipping程序拒绝。

本批最终相关Release15/15、ASan10/10、完整Release195/195通过，使用同一688项代码/配置和额外固定CSV。原版384条命令矩阵加6条事件准入共390条通过；项目映射sequence1/27的96条真实Scatter命令对照由0/96变为96/96。辅助函数返回/清理的纯整理先保留315份输出逐字节一致；六组实际Scene66下令前档重发Scatter并各续跑600tick，保持全部601个校验值。完整运行复现727份选定新状态，ASan的657份状态与Release逐字节相同。两张固定原图仍用种子922/923与180000tick窗口，在31911/116755自然结算，各有2份精确App12检查点。BUILD_TESTING开关保留相同200项runtime输入与编译选项，194个旧测试名全部保留，28次shipping参数拒绝通过；完整输入58项、14张新截图通过，用户文件恢复。原始探针IAT失败、探针第三版构建失败、有效红测及机械整理时保留的失败均原样保留。验收收据为`review_out/ra2-player-scatter-20261124/acceptance-audit.json`。

## Unit车辆散开回归

本批扩展现有`ra2.player_scatter`入口，不新增运行时测试方法或CTest名称。三个捕获CSV记录原版目的地、准入和Facing计时器；项目映射960条目的地、96条已有状态准入及6条计时边界。13条真正命令流程检查初始/命令后/中途保存与600tick完整续跑，活动卸载从真实Board→Unload进入，宽弯从实际L形Move进入。红测所有准备/保存/续跑控制通过，不能把之前0项执行的编译失败作为红色复现。

`OPENRA2_LEGACY_VEHICLE_SCATTER67_INPUT_DIR`读取`vehicle-journey-N-before.sav`、`.case`和`.trace`，重发绑定世代的Scatter后比较初值与600tick全部校验值；保持真实Scene67来源，不构造伪旧头部。原有Scene66步兵与更早历史消费者同时保留。

本批最终相关Release16/16、ASan11/11、完整Release195/195通过，使用同一689项代码/配置和额外固定CSV。原版2,715项包括1,680条目的地、1,024条准入、6条真实计时器边界及5条排队任务控制；项目映射四种现有速度类型的960条目的地与96条准入，修正前均0匹配，修正后全匹配。共用Nearby的纯改名/参数整理先保留226份输出逐字节一致。13条实际流程覆盖坦克/舰船/气垫、已有路线、Guard、转向、Stop、时空矿车、活动卸载、无出口、IsTrain和宽弯；各有下令前/后与中途保存及600tick续跑，轮式车辆另由命令矩阵覆盖。13份实际Scene67下令前档重发Scatter保持全部601个校验值；继承六组Scene66步兵控制。完整运行复现907份选定新状态，ASan的837份状态与Release逐字节相同。两张固定原图仍用种子922/923与180000tick窗口，在31911/116755自然结算，与Scene67时点相同，各有2份精确App12检查点。195个旧测试名全部保留，CMake/入口/发布参数未改；本批未重复产品ON/OFF检查。完整输入58项、14张新截图通过，用户文件恢复。首次夹具编译失败（0项执行）、有效红测16条失败及机械整理保留的失败均未覆盖；原有6770份封存证据保持。验收收据为`review_out/ra2-vehicle-scatter-20261124/acceptance-audit.json`。

## MOVE-WALK流程验收

`ra2.infantry_travel`（`ra2_regression --infantry-travel-regression <目录>`）使用实际捕获CSV，对照720条真实Move首帧与849帧连续位置，并覆盖命令、跨格到达、奇偶tick保存、Stop、改令、定身恢复、受阻清口、编队和只读展示。受控速度数据792项与实际位移分开统计。裸机车Stop/MoveTo观测不替代原版UI命令调度证据。

`OPENRA2_LEGACY_WALK68_INPUT_DIR`读取本次行为改动前实际生成的8组`walk-journey-N-ordered.sav`、`.trace`和`.case`，按原第6tick的Stop/改令时点续跑600tick，比对全部601个校验值。原始红测保存/流程控制通过，两处位置对照失败；机械提取再证明58份输出字节相同。

消费集成失败及只读诊断保留于`consumer-related-20261125`和`consumer-inspect-*`，后续通过为`boarding-integration-20261125-r1`与最终三个验证阶段。最终全量前先收敛真实流，源码改变后再做相关验证；本批没有把固定比赛窗口、种子或胜负门槛调成新的通过条件。

M1/MOVE-WALK本批相关Release24/24、ASan11/11、完整Release196/196通过，使用同一693项代码/配置及额外固定CSV。普通速度八方向的720条首帧位置从136匹配变为720匹配，32组连续轨迹的849帧从187匹配变为849匹配；792条受控速度舍入全部匹配。8条Move/Stop/改令/速度/续档流程、定身恢复、受阻清口再令、编队与展示独立性通过；8份实际Scene68行走档各保留600tick的601个校验值。原有标量速度计算先纯提取，58份输出字节不变。消费检查曾有效失败于容量争抢与移动载具登车；修正动态格角的合法直角前缀与仍有效的登车路径保留，原输入/窗口/断言不改，两流程恢复通过。1291份选定状态在完整运行精确复现，ASan的747份与Release逐字节相同。两张原图沿922/923及180000tick原窗口自然结算于17484/92448，各两份App12检查点精确。产品ON/OFF的201项runtime输入与参数相同，195个旧测试名保留并新增infantry_travel；30次shipping参数拒绝、完整输入58项与14张新截图通过，用户文件恢复。源码与早期探针/红测/失败消费记录保留，旧Scene68的7195份封存工件不变。收据：`review_out/ra2-walk-steps-20261124/acceptance-audit.json`。
