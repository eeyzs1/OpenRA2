# 来源 IFV 的乘员模式、维修与炮塔

本记录对应 IFV 批次的 SourceScene34 / NativeWeapons3 / NativeIFV2、NativeProduction5、World58 / App11、LAN51。后续 Source35 / World59 的共享时空控制器见 [时空武器记录](native-temporal.md)。IFV 批次接通来源 IFV 的完整编号表；特殊武器的全部原版行为仍在推进，不能把模式表接通等同于完整武器保真。

## 来源和模式

`gen_native_weapon_catalog.py` 分别读取已安装 RA2 / YR 的原始规则，导出 FV 的十三 / 十七组普通、精英编号武器，以及乘员的原始 IFVMode。来源标识和 SHA 保留在 profile 中；没有把 local 基础资料称作最终补丁版本。

`native_ifv_parse.cpp` 严格检查有界模式、武器引用和炮塔索引。`world_native_ifv.cpp` 根据载员的不可变来源定义选择逻辑模式，再按原始 `*TurretWeapon` 映射选编号武器，按 `*TurretIndex` 选视觉炮塔。三者分别保存于嵌入的来源文本。武器晋升取车辆军衔；乘员自身军衔不代替车辆军衔。

| 模式 | 来源武器 | 炮塔 |
|---|---|---:|
| 0 空车 | HoverMissile / HoverMissileE | 0 |
| 1 工程师 | RepairBullet | 2 |
| 2 机枪 | CRM60 | 1 |
| 3 防空兵 | CRFlakGuyGun | 1 |
| 4 手枪 | CRMP5 | 1 |
| 5 狙击 | AWPE | 1 |
| 6 磁暴 | CRElectricBolt | 3 |
| 7 爆炸 | CRNuke | 3 |
| 8 心灵冲击 | CRMindControl | 3 |
| 9 辐射束 | CRRadBeamWeapon | 3 |
| 10 时间抹除 | CRNeutronRifle | 3 |
| 11 恐怖分子 | CRTerrorBomb | 3 |
| 12 特殊模式 | CowShot | 3 |
| 13..16，YR | CRPsychicJab / CRVirusGun / CRSuperMindBlast / CRMissileLauncher | 3 |

空车原先已有正确的普通和精英导弹。此前错误是来源武器查询提前返回，令乘员模式失效。CRMindControl 使用 PsiPulse 弹头造成范围伤害，不是心灵控制。模式十二已由嵌入规则改动验证；这不表示原版所有牛和市民演员都已导入。

NativeIFV2 显式保留车辆的 BurstDelay0 来源帧区间，并在选择武器时换算为逻辑 tick。当前默认区间与既有来源单位槽一致，为 6–10 tick。第一轮实现误用了目录武器的 3–5 tick 默认值；独立复现四处失败后已修正，永久专项也检查实际二连射与精英四连射。早期 NativeIFV1 的真实保存继续使用其历史节奏，并有固定保存夹具；不在载入时静默改写旧对局。完整原始发弹相位和随机序列仍在校准。

真实登车和卸载会取消旧模式未完成的连射。已经飞出的弹丸保留发射时的目录引用；其武器不随载员变化。独立武器沿用从 2000000 开始的保存引用。

## 维修

`world_native_repair.cpp` 使用 RepairBullet 的 -50 伤害、1.8 格射程和 ROF80，即本项目的 160 逻辑 tick。车辆会被动寻找射程内的受损友军，也可接受右键维修指令并追踪到射程内。维修量封顶于来源 Strength；满血停止。恐怖机器人清除有独立回归。

原程序的资格检查先于实际维修。只读 RA2 GAME.EXE 核对了 Unit::GetFireError 的负伤害分支和目标 IsStrange 虚函数：车辆取 !NonVehicle，Aircraft 查询着地状态，建筑要求 UndeploysInto 且不是 ConstructionYard。因此不能因为 RA2 Mechanical 对建筑的 Verses 为 100%，就允许维修所有建筑。YR 的一格可展开建筑条件参考了 [YRpp ObjectClass](https://github.com/Phobos-developers/YRpp/blob/master/ObjectClass.h)；尚无独立 YR 程序校准。二者 Mechanical 的建筑倍率也分别保留，YR 为零。

输入、光标、模拟执行和回执共用维修判定。普通维修不再经过旧版每 25 tick、范围四、加二十的近似逻辑。旧来源和融合规则继续保留各自历史行为。自动搜索使用附近格子的派生占位索引，距离平局按 EID 决定；没有为这项搜索增加全场逐实体扫描。

## 表现与保存

`native_ifv_art.py` 将选定版本的 FV 车体和四套原始 VXL/HVA 写入各自 profile 的 ifv 目录，并记录归档路径和 SHA。临时导出也写入自己的输出目录。`VxlRt::renderIFV` 为全部炮塔、朝向和 HVA 相位建立共同体素原点；`sprites_ifv.cpp` 缓存具体模型、相位和房屋色，Game 只读取选定的炮塔。画面和选择框共用该车体锚点。

没有新增 World/App 二进制字段。Scene34 的文本随实际保存保留并重建定义；旧程序在来源版本检查处拒绝新场景。Scene1..33 不从磁盘上的新 profile 静默更新。

## 验证与限制

修改前同一独立复现为 36 对照通过、30 机制失败；初次模式与维修实现后 66 项通过。永久 IFV 专项覆盖实际登车、各军衔与卸载、模式十二与非同号映射、维修操作和冷却、旧连射取消、在途弹丸、发射者死亡、World/App 复演、异常规则及实际炮塔像素。首次专项的两项截图测试错误和后续验证分别保留。最终两种构建各通过 140 条 IFV、51 条格序及 42 条历史武器断言。

首轮完整非性能 Release 为 152/153，唯一失败为合成 Scene28 残留新版字段；夹具降级转换修正后相关 Release19/19、ASan14/14。随后自然四人 YR 在 360000 tick 未决胜，连射区间独立复现四处失败；已采用 NativeIFV2 修正，并保留该失败及中止的旧八人诊断。最终相关 Release21/21、ASan14/14，未再次运行完整全量。

最终 **134 对实际保存**跨构建逐字节相同，其中机制 30 对、十三份真实 App 的 0/120/3000/5000 tick World/App 共 104 对。72 份 Scene31..33 历史结果与上一批完全相同，八份 IFV v1 结果也与修正前程序完全相同。最终 10951 个冻结输入零变化；退役的 v1 阶段在修订前另核对 10938 项零变化。

三个多人场景分别在 198256 / 305190 / 270115 tick 自然结算；四组国家/难度双人也结束，共 59 个定期 App 检查点精确。德国/古巴、韩国/利比亚、法国/伊拉克、英国/尤里分别为 64485 / 19466 / 125881 / 8527 tick；英国在有效出兵前早败，严格经济检查仍失败。未延长观察窗或人工改写胜负。

本批功能任务全部结束后，七项既有性能检查通过，53.28 秒；环境记录中仅有正常 Windows 跨设备服务，没有本批测试、编译或其他已知计算进程。这不替代冷态、真实主循环、整局、GPU、人工或跨机器验收。完整证据已封存于 `review_out/ifv-20261117/validation.json` 和 `closed-batch.json`。

以下仍属于完整项目工作：

- 在本记录的 Scene34 中，Temporal 尚未接入。后续 Scene35 已加入共享控制器，见 [时空武器记录](native-temporal.md)；完整原始时序与组合仍需继续校准。
- CRNuke 的完整辐射场、Virus 的死亡云与传播、RadBeam 专属表现、原始 InfDeath、全部 Report 和维修声音。字段导入不等于动作全部执行。
- 完整负伤害弹丸和范围治疗；本批实际校准的是原始瞬时 RepairBullet。
- 原始弹丸/Logic 时序、三维碰撞和随机序列，以及独立 YR 程序和人工听感验收。
