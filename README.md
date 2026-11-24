# OpenRA2

OpenRA2 是一个受《红色警戒 2》启发的 Windows 即时战略项目，使用 C++23、raylib 和 Lua。当前可进行遭遇战、局域网对战和盟军第一关。默认采用红警 2 基础规则，可在开局时选择《尤里的复仇》或融合规则。完整战役、规则与操作手感仍在逐项校准。

## 快速开始

### 1. 准备环境与素材

- 安装 Visual Studio 2022 或 Build Tools，选择 **使用 C++ 的桌面开发**并安装 Windows SDK。另需 CMake 3.24 或更新版本，以及 Python 3。
- 首次配置时，CMake 会下载 raylib 5.5 和 Lua 5.4.6。
- 从你本机的《红色警戒 2》和《尤里的复仇》安装目录准备 MIX 文件，放到 `tools/ra2pack/game/`。素材提取器使用绝对路径，请确认 [`tools/ra2pack/ra2lib.py`](tools/ra2pack/ra2lib.py) 中的 `GAME_DIR` 指向实际的 MIX 目录。

在仓库根目录用 PowerShell 准备基础图像、地形、音效和来源规则包。

```powershell
python -X utf8 -B -m pip install -r tools/ra2pack/requirements.txt
python -X utf8 -B tools/ra2pack/gen_assets.py
python -X utf8 -B tools/ra2pack/gen_terrain.py
python -X utf8 -B tools/ra2pack/gen_audio.py
python -X utf8 -B tools/ra2pack/make_fusion_units.py
```

运行还需要准备本地的 `assets/gui/sidebar_allied.png` 和 `assets/gui/bottombar.png`。已有完整素材树时可以直接复用。可为鼠标和状态图标运行 `gen_ui_fidelity.py`，为主菜单资源运行 `gen_menu_gui.py`。菜单视频导出需要 FFmpeg。素材位置、覆盖和生成细节见[素材管线](docs/asset-pipeline.md)。

需要在遭遇战菜单中使用两张原版地图时，另行导出地图。盟军第一关随 `gen_assets.py` 在本机存在 `all01t.map` 时导出。

```powershell
python -X utf8 -B tools/ra2pack/gen_skirmish_maps.py
```

### 2. 构建并启动

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
.\build\Release\ra2.exe --windowed
```

也可在构建后运行 [`launch.bat`](launch.bat)。去掉 `--windowed` 即可按游戏设置启动。启动失败时，在终端检查 `ASSET-CHECK`、`GUI-MISSING` 和 `SFX-MISSING` 的文件名。用启动脚本运行时可查看 `launch_log.txt`。

## 玩法与操作

| 入口 | 当前内容 |
| --- | --- |
| 遭遇战 | 提供程序生成的大陆、岛屿和湖泊。导出素材后可选择 `mp08t2`（湖泊之战）和 `mp15s4`（雪谷）。基地建设、采矿、生产、作战和结算组成完整对局流程。 |
| 战役 | 盟军第一关 `all01t` 支持任务简报、作者事件、桥梁与闸门、重开和续档。关卡范围详见[战役说明](assets/campaigns/README.md)。 |
| 局域网 | 主机与客户端通过 TCP 进行确定性锁步对战。双方使用相同的模拟版本和规则，当前协议为 LAN96。详见[联机规则](docs/lan-rules.md)。 |
| 自定义内容 | `mods/` 与 `userdata/content/` 支持本地内容覆盖，`mods/ExampleCustom/` 提供关卡、规则和 Lua 示例。 |

矿车进入目标矿格后开始采掘。武装矿车驶入泊位后对正卸矿，卸空后加速驶出。超时空矿车获准后直接传送至泊位并对正。履带坦克具备原地转向、边走边转与独立炮塔瞄准。同层同高的直角宽弯按曲线弧长推进，车头随行驶进度转动。RA2新局按原始类型标志决定碾压资格，车辆实际接近到半格以内才结算碾压，直接移除步兵并播放碾压声。普通伤害仍播放倒地动画。履带车辆建立新行驶段时会警告前格步兵，符合原始AI、精英或玩家设置条件的步兵可散到相邻格；普通人工控制默认不自动散开。步兵收到移动令后，可从已与车辆共享的原格离开。旧存档按各自的场景合同续跑。[当前架构](docs/architecture.md) · [规则与机制对照](docs/ra2-fidelity.md)

友军步兵后来进入车辆已经下达的路线并警戒时，普通Drive车辆会对正车头，再按原版受阻分支要求步兵散开。多功能步兵车也可触发，不依赖碾压能力。此时玩家大兵可以解除部署让路；正在执行移动令、冻结、敌军免碾压占用和无合法出口等情况仍分别处理。

车辆在临时等待区互相阻挡时，会识别对方等待结束后的实际离位方向，沿合法退路或重新选择出口后继续原目标。

裂缝产生器继续遮蔽卫星照亮；进入附近的单位可以实际观察敌方目标，离开后该区重新被遮蔽。新RA2对局按本机原版的地面雾格查询记录处理裂缝：默认半径10在零级地面覆盖373格，一级平地对应342个地面格；高差边界和多个裂缝的重叠覆盖按投影查询解析。

默认使用经典左键下令，右键取消。设置页可切换到右键下令方案，拖动左键用于框选。

| 默认按键 | 功能 |
| --- | --- |
| 左键 / 拖动左键 | 选择、下令 / 框选 |
| A + 下令鼠标键 | 攻击移动 |
| S / X / G / T | 停止 / 散布 / 警戒 / 选同类 |
| D / U | 部署 / 卸载乘员 |
| Ctrl + 数字 / 数字 | 设置编队 / 选取编队 |
| R / Shift + R / Delete | 设置集结点 / 清除集结点 / 出售建筑 |
| F5 / F9 | 单机快速存档 / 快速读档 |

## 验证与开发

产品入口为 `ra2.exe`，自动化回归入口为 `ra2_regression.exe`。验证器默认先编译，每次运行将实际测试结果写入 `review_out/`。涉及地图、图像或声音的测试需要先准备对应素材。

```powershell
python -X utf8 -B tests/verify.py --build build --tests ra2.ground_control ra2.ground_travel ra2.group_orders
python -X utf8 -B tests/verify.py --build build --all
```

- [`src/`](src/) 存放共享运行库、游戏入口、图像、声音和网络代码。测试实现仅链接到回归程序。
- [`tests/`](tests/) 存放验证入口和回归夹具，包括历史存档样本。
- [`tools/`](tools/) 提供本地素材导入和开发工具。
- 开始改动前请读[仓库规则](AGENTS.md)和[开发流程](docs/development.md)。版本与兼容范围以[架构文档](docs/architecture.md)为准，旧批次记录见[历史目录](docs/history/README.md)。

原版安装文件、生成素材、玩家存档和本地回归输出由 `.gitignore` 管理。用于回归的历史样本位于 `tests/fixtures/`。
