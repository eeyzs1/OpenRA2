# GUI 学习笔记：成熟 RTS 壳层怎么做（再动手前必读）

> 状态：**学习中 / 暂停乱改**。v3「炭灰扁平方框 + 冰蓝描边」已被判为业余，本文记录对照结论，再改代码前先对齐方向。

相关：`command-shell.md`（契约）、`menu-screens.md`（UV）、`art-visual.md`（世界画风）。

---

## 0. 对上一版失败的诚实诊断

用户说「像刚学美术的学生作品」——对应我们实际做了什么：

| 学生作业特征 | 我们 v3 的对应物 |
|--------------|------------------|
| 每个框都描同一条细彩边 | 按钮 / 值槽 / 监视器 / tip 全是 1px 冰蓝边 |
| 只有一种填充灰度 | 炭灰 `DrawRectangle` 铺满，无材质层次 |
| 从网页 Dashboard 抄「左强调条」 | TitleBtn 左条 + 左对齐字，像后台侧栏不是 RTS 壳 |
| 「去材质 = 现代」 | 去掉 PCB / bevel / 亮心，只剩空框 |
| 场景皮与壳层打架 | 左洞还是 RA2 BIK 金属雷达框，右栏却是扁平导航 |

**根因不是「不够科技」**，而是：**把「现代」误解成「删掉材质语言」**。成熟产品从不这么干。

---

## 1. 对照样本（本谱系 + 当代 RTS）

### 1.1 C&C Remastered（最直接的「现代但仍是 C&C」）

来源：[EA Remaster sidebar preview](https://www.ea.com/games/command-and-conquer/command-and-conquer-remastered/news/remaster-update-sidebar-preview)

学到的：

- **先保气质，再加可用性**：加建造 Tab、减少滚动、图标化、适配高分——但明确写要 *capture the visual spirit of the legacy UI*。
- **现代 ≠ 换皮**：侧栏仍是金属仪器栏，不是扁平方块导航。
- **资产是画的**：按钮/侧栏按 Gold Edition 精神重绘到 4K，不是程序方框描边冒充。

对 OpenRA2：`LOAD_*` 几何可以冻，但控件必须像「仪器零件」，不能像 CSS `border:1px solid cyan`。

### 1.2 OpenRA（开源同谱系的工程正确答案）

来源：OpenRA Chrome / widget 体系；高分缩放把 `chrome.png` 拆成 SVG glyphs + 像素 sidebar。

学到的：

- **Chrome 是资产系统**：9-slice 面板、按钮态（idle/hover/disabled）来自图集，不是每帧 `DrawRectangleLinesEx`。
- **缩放是一等公民**：DPI / 多分辨率 glyph，避免「逻辑 640 硬画细线 → 放大后像毛线」。
- **逻辑与皮肤分离**：YAML 布局 + Logic；我们至少要保证「控件族有固定画法」，别每屏临时发明第三种边。

对 OpenRA2：脏 SHP 不能硬拉，但 **程序近似也要模拟 9-slice 面板的明暗结构**（顶亮底暗、槽深、钮面），不是单色矩形。

### 1.3 Tempest Rising（当代「不丢灵魂」的 C&C 继承人）

来源：Creative Bloq 访谈；[Dynasty UI motion](https://designedbythomas.co.uk/portfolio/tempest-dynasty-ui-game-motion-design/)

学到的：

- 口号接近：**拖进当代，但不丢掉让人回来的理由**。
- UI 被描述成 **AoE + StarCraft + C&C 的优雅杂交**——功能现代，气质仍是战争指挥台。
- **阵营有材质人格**：GDF 偏高科，Dynasty 故意做成遗留/MS-DOS 感——说明「复古元素」可以是 *设计选择*，不是缺陷。
- 可读性优先于炫技：信息层级清楚，动效服务反馈。

对 OpenRA2：**不要学 SaaS 后台；学「升级过的西木指挥台」**。黄字红心、金属槽在本谱系里是身份，不是耻辱。

### 1.4 Stormgate / Beyond All Reason（布局与系统，不是壳层皮肤课）

学到的：

- 成熟 HUD 先解 **眼动距离、锚点、控件一致性、缩放**（Stormgate 中央锚底栏；BAR FlowUI 统一 primitive）。
- 它们的「现代」主要在 **信息架构与交互**，不是把每个面板画成同色描边方框。
- 有统一绘制框架（FlowUI / 设计系统）→ 视觉不会像学生每人画一种框。

对 OpenRA2：先统一 **3～5 个控件 primitive 的材质语法**，再谈颜色。

---

## 2. 成熟壳层的共同规律（可验收）

1. **材质层级 ≥ 色票**  
   外壳 / 凹槽 / 钮面 / 亮心 / 文字，至少 4 层明度差；不是「深底 + 彩边」两层。

2. **强调色是稀缺资源**  
   只用在：焦点、主行动、进度/危险。禁止每个矩形都描强调色。

3. **按钮像零件，不像列表项**  
   有 idle 材质、hover 反馈（亮心/扫光/抬起）、press 下沉。左对齐 + 左边条是网页导航，不是 RTS 壳。

4. **场景皮与壳同属一个世界**  
   左洞 BIK/标题是 2000 金属雷达美学时，右栏不能突然变成 2024 后台侧栏。

5. **现代升级优先序**（Remaster / Tempest 共识）  
   ① 可读与点击 ② 分辨率与清晰度 ③ 交互 QoL  
   **最后才**考虑是否改气质。气质改了也要仍像「战争仪器」。

6. **程序 UI 要伪装成画出来的**  
   无美术图集时：用 inset 明暗、槽深、有限高光模拟面板——不是删掉深度假装极简。

---

## 3. OpenRA2 应站的位置（纠正后的产品判断）

| 轴 | 错误方向（v3） | 正确方向 |
|----|----------------|----------|
| 身份 | 通用「现代科技」后台 | **RA2 / C&C 谱系指挥壳**（Remaster 级清晰度） |
| 材质 | 扁平方框 | 金属面板 + 凹槽 + 钮面亮心（贴图或高质量程序近似） |
| 字色 | 近白 + 冰蓝边 | 高对比命令字（原作黄/琥珀系可保留）+ 少而狠的强调 |
| 侧栏 | 网页左轨导航 | 仪器栏：监视器看、下面钮下令（`menu-screens.md`） |
| 「现代」落点 | 换皮扁平化 | 间距、字清晰、脏图不硬拉、交互反馈干净 |

与 `art-visual.md` 原句对齐：**「菜单右栏是 PCB + CRT，不是现代导航栏」**——v3 正违反了这条。

---

## 4. 下一轮改代码前的门禁（未勾选 = 不准动手）

- [x] 与用户确认目标：**B** 同谱系当代（Tempest 式工业现代）
- [x] 更新 `command-shell.md` + skill 锁定 B
- [x] 控件族：Plate / Well / ButtonFace；强调色不全边框
- [x] 主菜单 + setup/settings gui-review 已出图
- [x] 删除「扁平左轨 = 现代」教条

---

## 5. 参考链接（学习用）

- [C&C Remastered sidebar preview](https://www.ea.com/games/command-and-conquer/command-and-conquer-remastered/news/remaster-update-sidebar-preview)
- [OpenRA：与 Remaster 的哲学差异](https://www.openra.net/news/devblog-20200629/)
- [Tempest Rising：不丢灵魂的现代化](https://www.creativebloq.com/3d/video-game-design/how-tempest-rising-uses-unreal-engine-5-to-channel-the-heart-of-90s-strategy-games-without-losing-its-soul)
- [Tempest Dynasty UI motion（阵营材质人格）](https://designedbythomas.co.uk/portfolio/tempest-dynasty-ui-game-motion-design/)
- Stormgate / BAR：布局与 FlowUI（学系统一致性，不抄扁平壳）
