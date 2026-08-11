OpenRA2 Mods（可选整包）
======================

自己玩、随手改：请用 userdata\content\（见该目录 README.txt）。

本目录适合「打包分享」：
  1. 复制 ExampleCustom 改名
  2. 改 mod.ini：Name=…、Enabled=yes
  3. 对方放到 mods\ 下，或 ra2.exe --mod mods\包名

优先级：assets → mods（Enabled）→ userdata/content（始终最高）

布局：rules/、campaigns/、maps/、scripts/、sprites/…（可去掉 assets/ 前缀）

限制：新兵种逻辑仍需改程序；数值用 CSV/INI；Buildable=replace 可让侧栏吃到改动。
