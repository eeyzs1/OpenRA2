====================================================
  OpenRA2 自定义内容 — 怎么用（看这一份就够）
====================================================

一句话：把文件放进本目录，保存后【重新启动游戏】就会生效。
本目录路径：游戏根目录下的 userdata\content\
（和 启动游戏.bat / ra2.exe 同级的那一层里的 userdata\content）

不需要改程序，不需要命令行。


----------------------------------------------------
1. 改单位数值（遭遇战里立刻能感到）
----------------------------------------------------

打开文件：
  userdata\content\rules\units.csv

用记事本或 Excel 编辑。表头不要删。

【最简单】改已有单位（例如灰熊坦克 Grizzly）：
  找到被 # 注释掉的示例行，删掉行首的 #，改数字，保存。

  示例（造价 900、血量 500）：
    Grizzly,,500,900,,,,,,

  列含义（从左到右）：
    Name          单位英文代号（必须和游戏里已有的一致，如 Grizzly / GI / Rhino）
    Base          一般留空
    HP            生命值
    Cost          造价（侧栏显示的钱）
    Speed         速度（可留空）
    Sight         视野（可留空）
    WeaponDamage  武器伤害（可留空）
    WeaponRange   射程（可留空）
    Buildable     可留空
    DisplayName   显示名（可留空；填了会改中文/显示名）

启动游戏 → 遭遇战 → 造出该单位，看造价和血量是否变了。

【进阶】用新名字，但玩起来仍是某种已有单位：
    HeavyGrizzly,Grizzly,600,1200,,,50,6,replace,重装灰熊

  含义：登记名字 HeavyGrizzly，并把灰熊本体改成这些数值（遭遇战造灰熊就是新数值）。
  地图里也可以写：unit 0 HeavyGrizzly x y


建筑同理，编辑：
  userdata\content\rules\buildings.csv
  例如：ConYard,,4000,,,,加固建造厂


----------------------------------------------------
2. 加一关自己的战役
----------------------------------------------------

需要三个文件：

  A) 地图
     userdata\content\maps\我的关.txt
     （可先复制 mods\ExampleCustom\maps\example_demo.txt 再改）

  B) 关卡说明
     userdata\content\campaigns\我的关.ini
     至少包含：
       [General]
       Name=我的关卡
       NameEn=My Mission
       Faction=Allies
       AI=Soviet
       MapFile=maps/我的关.txt
       Objective=0

  C) 登记到列表
     打开 userdata\content\campaigns\missions.csv
     写成：
       Mission
       我的关.ini

重启 → 战役菜单 → 列表末尾应多出这一关。


----------------------------------------------------
3. 换贴图 / 文字 / 音效
----------------------------------------------------

把文件放到与 assets 相同的相对路径下，文件名必须一致才会覆盖，例如：
  userdata\content\sprites\unit_grizzly_d0.png
  userdata\content\strings\zh.ini


----------------------------------------------------
4. 自检（可选，给会开命令行的人）
----------------------------------------------------

在游戏根目录执行：
  build\Release\ra2.exe --content-check

应看到：
  CONTENT-CHECK PASS: userdata/content enabled
  CONTENT-CHECK: missions=70   （若你加了关会变多）

改完灰熊 CSV 后可加：
  build\Release\ra2.exe --content-check expect-grizzly-cost=900


----------------------------------------------------
5. 做成整包给别人玩
----------------------------------------------------

复制 mods\ExampleCustom\ 改名，编辑里面的文件，
把 mod.ini 里 Enabled=yes，对方解压到 mods\ 下即可。
或：ra2.exe --mod mods\你的包名


----------------------------------------------------
做不到的事（请先知道）
----------------------------------------------------

- 不能凭空发明「全新兵种逻辑」（例如全新 AI 技能），只能改已有单位或基于已有单位做变体。
- 不能在侧栏多出一个完全独立的新图标格子（不替换原单位）；要用「直接改 Name」或 Buildable=replace。


----------------------------------------------------
常见问题
----------------------------------------------------

Q: 改了没变化？
A: 必须重启游戏；确认改的是 userdata\content\ 而不是别处；
   CSV 行首不要留着 #；文件请保存为 UTF-8。

Q: 战役列表没有我的关？
A: 检查 missions.csv 是否写了文件名；ini 与 maps 路径是否对；
   MapFile= 必须是 maps/文件名.txt。

Q: 想恢复原版？
A: 删掉或重新注释掉你加的 CSV/关卡行，重启即可。
