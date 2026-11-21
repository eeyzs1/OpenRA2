# 修改前真实 World60 乘员夹具

四份文件由修复 `OpenTransportWeapon` 之前的实际诊断器保存，合同为 Scene36 / NativeCrew1 / World60。诊断器执行真实登车和开火；没有通过替换标识生成历史夹具。

`provenance.json` 记录每份文件的 SHA256、诊断器源文件及可执行文件哈希、实际进程退出和原始路径。GI、重装大兵保留旧 M60 选择；海豹对建筑保留旧 Sapper 选择；精英军团兵保留独立 Temporal 图。加载后重新保存必须保持整个 World60 文件逐字节相同。

这些文件用于历史读取与确定性验证，不能据此声称旧行为符合原版 YR。机制与验证范围见 [乘员选枪说明](../../../docs/crew-weapon-selection.md)。
