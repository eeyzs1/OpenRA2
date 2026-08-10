---
name: openra2-gui
description: >-
  Designs and reviews OpenRA2 Command Shell GUI (pre-battle menus, settings,
  ESC overlay, HUD chrome). Enforces RA2 industrial shell language, spacing,
  asset discipline, and screenshot verification. Use when changing menu/HUD
  layout, buttons, tips, shells, fonts, or when the user mentions GUI, 菜单,
  壳层, 侧栏, 杂色, 分辨率, Command Shell, or gui-review.
---

# OpenRA2 GUI（指挥壳）

改编自通用 game-UI skills（`game-ui-ux`、`game-ui-design`）+ 本仓
`docs/ra2-reference/style/command-shell.md`。**通用网游 HUD 教条不得盖过 RA2 壳层契约。**

## 何时用

- 改主菜单 / 遭遇战 / 设置 / 联机 / ESC / 侧栏按钮
- 用户抱怨：挤、扁、杂色、廉价、分辨率怪、间距无
- 新增菜单控件或 tip

**不要用：** 纯 sim（`world_*`）、纯网络锁步、改 launch bat。

## 先读

1. `docs/ra2-reference/style/command-shell.md` — 气质与禁令
2. `docs/ra2-reference/menu-screens.md` — `LOAD_*` UV
3. 实现落点：`game_menu.cpp` / `game_settings.cpp` / `game_hud.cpp`（壳层勿堆进 `game.cpp`）

## 工作流（必须按序）

1. **定层** — L0 场景皮 / L1 内容洞 / L2 侧栏仪器（见 command-shell）
2. **复用控件族** — TitleBtn / SideBtn / ValueSlot / Pip / InfoPanel；能复用勿新发明
3. **改布局** — 只在 640×480 逻辑坐标；间距用下方刻度
4. **贴图纪律** — 脏 SHP 勿硬拉（见下）
5. **验收** — `cmake --build build --config Release --target ra2` →
   `ra2.exe --gui-review gui_review_out` → **自己用 Read 看裁剪图** 再回复用户

未截图验收 = 未完成。

## 间距刻度（640 逻辑 px）

| 关系 | 最小 | 推荐 |
|------|------|------|
| 侧栏同族按钮之间 | 8 | 8–10 |
| 表单行（值槽/勾选） | 6 | 8–12 |
| 分区标题 ↔ 首行 | 8 | 12–16 |
| 监视器 ↔ 其下动作钮 | 6 | 8 |
| 底栏「返回」与上一动作钮 | 可拉开 | 勿与上一钮贴边 |

**反模式：** `gap = 2` 把 6 个侧栏钮焊成一面墙。空间不够时：**略减 bh 或上移起点**，不要把 gap 压回 2。

侧栏按钮参考：

- 主菜单 TitleBtn：`bh≈34`，`gap=10`，起点约监视器下
- Setup/Settings SideBtn：保持 ~156×83 比例，`sideGap=10`
- ESC：`bh≈36`，`btnGap=6`

改完若碰自动化点击，同步 `game_tests.cpp` 里的 UI 坐标注释与 `clickUi`/`clickL`。

## 从通用 skill 吸收的原则（映射到本项目）

| 通用原则 | OpenRA2 落点 |
|----------|----------------|
| 信息层级 / 一目了然 | 黄字下令；监视器 tip 短文；勿堆第二套霓虹 |
| 每个元素赚屏幕空间 | 洞内表格式控件；禁止漂浮卡片 |
| 动画是沟通 | 悬停扫光帧 OK；禁止装饰粒子/缓动弹跳 |
| 参考分辨率缩放 | 已有：640 逻辑 → `UI_DRAW_SCALE` RT → canvas letterbox |
| 截图/多分辨率验收 | 至少 `--gui-review`；改点击热区要跑相关 playtest |
| 控制器优先 | 本项目仍以鼠标为主；勿引入仅 hover 才可知的关键操作 |

**故意不抄：** 现代 SaaS 留白、玻璃拟态、锚点响应式重构整页（壳 UV 冻结）。

## 贴图纪律（杂色块根因）

MIX 提取的 `optbtn` / `diplobtn` / `sdbtnbkgd` / `sdmpbtn` / `pips` 常含 SHP 雪花。

- **禁止** 把 72×18 等小图横向硬拉成宽值槽/标题条
- **禁止** 在 `rlScalef(UI_DRAW_SCALE)`（非整数）下用 `DrawRectangleGradientV` 当按钮主填充（易出彩虹色带）
- **主路径：** 干净金属底板 + 两色实心柔光红心
- L0 大图（`load` / BIK / `multi`）仍贴原作

干净重提 SHP 前，可读性 >「贴图纯度」。

## 验收清单

- [ ] 侧栏钮之间能看见壳层缝，不是一条连续色带
- [ ] 值槽/按钮面无雪花杂色（像素抽样或肉眼裁剪图）
- [ ] 中英切换无溢出盖住邻控件
- [ ] tip 在监视器内，不挡主操作钮
- [ ] `--gui-review` 已跑，且助手已看过相关 PNG
- [ ] 若改了钮位，测试点击坐标已更新

## 禁令速查

- 不要发明第二套 UI 皮肤语法
- 不要把大系统塞进 `game.cpp` / `world.cpp`
- 不要从 `applyCmd` 播 SFX/画 UI
- 不要用编辑器直接改 `启动游戏.bat` / `launch.bat`（走 `tools/write_launch_bats.py`）

## 参考

- 气质契约：`docs/ra2-reference/style/command-shell.md`
- UV/素材：`docs/ra2-reference/menu-screens.md`
- 上游灵感（勿原样照搬）：[game-ui-ux](https://skills.sh/gamedev-skills/awesome-gamedev-agent-skills/game-ui-ux)、[game-ui-design](https://skills.sh/omer-metin/skills-for-antigravity/game-ui-design)
