---
name: openra2-gui
description: >-
  Designs and reviews OpenRA2 command-shell GUI (direction B: industrial modern).
  Enforces steel plate material hierarchy, scarce steel-cyan accents, warm amber
  command ink, spacing, asset discipline, and screenshot verification. Use when
  changing menu/HUD layout, buttons, tips, shells, fonts, or when the user
  mentions GUI, 菜单, 壳层, 侧栏, 科技风, Tempest, Command Shell, or gui-review.
---

# OpenRA2 GUI（方向 B：工业现代指挥壳）

契约：`docs/ra2-reference/style/command-shell.md`（已锁定 B）  
学习笔记：`docs/ra2-reference/style/gui-study-notes.md`

## 气质（硬）

- 冷钢板底 + 暖琥珀命令字
- 面板有顶亮底暗凹凸；**禁止**全员冰蓝描边墙
- 钢青强调只打悬停顶缘 / 标题底条 / 主焦点
- 按钮是居中字的仪器零件，**禁止**网页左轨导航
- 侧栏：淡贴 load L0 + 钢板罩（同世界）

## 工作流

1. 读 command-shell（B）
2. 复用 `menuDrawPlate` / `menuDrawWell` / `menuDrawButtonFace`
3. 改完：`cmake --build build --config Release --target ra2` → `ra2.exe --gui-review …` → Read 看图

## 间距

主菜单 TitleBtn：`bh≈34`，`gap=10`。Setup：`sideGap=10`。改热区同步 `game_tests.cpp`。

## 禁

- 脏 SHP 硬拉；Gradient 钮面；扁平空心彩框；左强调条当主识别
