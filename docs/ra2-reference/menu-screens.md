# 菜单壳层规格（原作素材对照）

权威素材：本机 MIX → `python tools/ra2pack/gen_menu_gui.py` → `assets/gui/menu/`（gitignore，不进仓库）。
审核：`build\Release\ra2.exe --gui-review gui_review`。

## 保真优先级（产品）

1. **按钮与文字视觉**（红亮心、黄字对比、字号相对槽高）
2. **整体布局与组件风格**（右栏分格、红框值槽、橙圆勾选）
3. **分辨率呈现**（640 逻辑 → POINT 铺满 1440×810）
4. ~~主菜单 BIK 雷达动画~~（次要；静帧/循环均可，不阻塞上三项）

硬约束：壳层在 **640×480** 逻辑坐标绘制，但画到 **1080×810** 最终像素 RT（等比 ×1.6875），再 **1:1** 居中贴到 1440×810（左右黑边）。**禁止**宽高不等拉伸——那是字糊、控件比例/位置全错的主因。

## UV（`load.pcx` / `load.png` 实测冻结）

| 槽 | 常量 | 值 | 说明 |
|----|------|-----|------|
| 侧栏起点 | `LOAD_SIDE_X` | **472** | 右 PCB 从 x=472 起（列亮起实测） |
| 侧栏宽 | `LOAD_SIDE_W` | **168** | 640−472 |
| 监视器 | `LOAD_MON_*` | **486, 48, 140×122** | 右栏预览/示波槽（与原作 CRT 开孔对齐的工程槽） |

本地对照图（不提交）：`tools/visual_audit/menu_ref/load_uv_annotated.png`。

## 页面 ↔ 底图 ↔ 控件

| OpenRA2 | Phase / 入口 | 底图 theme | 控件 |
|---------|--------------|------------|------|
| 主菜单 | `MainMenu` | BIK `ra2ts_l` / 静帧 `titlelg`（640 逻辑）+ **右栏**裁自 `load` 侧栏 | **`sdbtnbkgd` + `sdbtnanm`**（禁止 `sdmpbtn`；按钮在监视器下方右列） |
| 选项 | `Settings` | theme **0** = `load.png` + 左洞 `content_map.png` | `optbtn` / `dropdown` / 橙圆灯勾选 / 侧栏 `sdmpbtn`+红内光 |
| 遭遇战 | `Setup` | theme **0** = `load.png` + 左洞 `content_map.png` | 监视器地图预览；侧栏 `sdmpbtn`+红内光；橙圆灯勾选 |
| 战役选关 | `MissionSelect` | theme **0** = `load.png`；监视器可放 `fsbkgdsm` 阵营徽 | 侧栏 `sdmpbtn`；**勿**把 `fsbkgdsm` 当整页侧栏 |
| 局域网 | `NetLobby` | theme **2** = `multi.png`（缺则回退 `load`） | 侧栏 `sdmpbtn` |
| 局内暂停 | ESC overlay | theme **1** = 左 `bkgdmd` + 右裁 `load` 侧栏 | 侧栏 `sdmpbtn`（非纯文字列表） |

## 控件原尺寸（提取后）

| 资产 | 尺寸 | 用途 |
|------|------|------|
| `sdmpbtn_*.png` | 156×83 | 壳层侧栏动作钮 |
| `sdbtnbkgd_00.png` | 168×42 | 主菜单钮底板 |
| `sdbtnanm_*.png` | 156×42 | 主菜单悬停动画 |
| `optbtn_*.png` | 72×18 | 选项值槽（横向贴满，高度勿 2D 拉伸） |
| `diplobtn_*.png` | 72×18 | 侧栏标题条 |
| `dropdown_*.png` | 30×23 | 下拉箭头 |
| `pips_00/01` | 10×7 | 关/开勾选 |

## 禁止事项

- 主菜单误用 `sdmpbtn`；壳层误用 `sdbtnanm` 拉满。
- `fsbkgdsm` 当选项/遭遇战侧栏（它是战役阵营徽记）。
- 壳图/UI RT 用 BILINEAR；壳层必须 POINT。
- **宽高不等拉伸** 640→16:9（会糊字且比例全错）。
- 主菜单 idle 误用 `sdbtnanm` 溅射红斑；idle 应为水平红亮心，悬停才扫光。
- 提取时过度擦除 SHP 边框「雪花」伤真像素（见 `gen_menu_gui.py`）。

## theme 约定（`drawRa2Shell`）

| theme | 底图 |
|-------|------|
| 0 | `load.png` |
| 1 | Allied 暂停：左 `bkgdmd`，右裁 `load` 侧栏 |
| 2 | `multi.png`（联机大厅） |
