# 原版 EVA 与配乐（2026-11-14）

当前已使用原版 RA2 配乐和盟军 EVA / 苏军 Sofia 播报。菜单循环《Grinder》，对战轮换 13 首原曲，结算播放原版结算音乐；不再扫描、播放此前生成的三首循环曲目。旧文件保留在磁盘，不作为原版资源使用。

## 资源来源与准备

检查发现，项目原有 `THEME.MIX` 虽有正确文件名和长度，但音轨数据全部为零。`full-pack.7z` 中也是同样的占位内容。另一份本地 `tools/ra2pack/dl/RedAlert2.7z` 的 `Red Alert 2/theme.mix` 包含有效原始音频；已核对 16 个 WAV 并恢复到 `tools/ra2pack/game/THEME.MIX`。恢复前副本保留在 `review_out/eva-music/zero-filled-THEME.MIX`，恢复记录在同目录的 `source-restoration.json`。

```powershell
python tools/ra2pack/gen_announcer.py
python tools/ra2pack/gen_music.py
```

`gen_announcer.py` 从 `eva.ini/evamd.ini` 读取原始对话表和阵营文件名，完整复制可用 WAV。当前导出了 459 条可用录音，包含尚未接线的任务对话；运行时仅预载核心 38 个播报标识对应的 76 条盟军/苏军录音。`gen_music.py` 从 `theme.ini/thememd.ini` 读取声音名、正常播放列表和场景，复制 16 首有效 RA2 原曲，其中 13 首可用于对战。完整 `gen_assets.py` 也包含这两步。

素材分别写入忽略目录 `assets/sfx/announcer/` 和 `assets/music/native/`。两个目录的 `source_report.json` 记录 MIX 路径、文件 SHA-256、采样率、声道和帧数。WAV 与原始 MIX 中的文件逐字节一致，没有重采样、归一化、截短或变调。零填充条目不通过 RIFF/WAVE 校验。

本地两套备份中，尤里专属播报和 YR 配乐仍含零填充条目。它们在来源报告中明确标为不可用，未用别的阵营或合成录音冒充。目前四阵营对战默认都使用 RA2 配乐；中国扩展继续显式复用苏军播报，尤里缺失的播报保留字幕与原通用提示音。这部分不是 YR 音频还原完成的证据。

## 事件与播放

`game/eva_cues.h` 定义语义标识，模拟在实际事件处附加标识：生产真正完成并出厂、建筑就绪、遇袭、单位损失/晋升、占领、间谍渗透/偷科技，以及原版四种超武的侦测、就绪和释放。MCV 实际展开后播报新建造选项。接受的生产、暂停、取消、维修、放置拒绝和胜负由应用层的执行回执或状态边沿接线。

`game_announcements.cpp` 只接收本地玩家的消息，按当前阵营选录音，不匹配字幕文本。低电力不覆盖正在显示的指令结果；电力恢复会撤掉尚未播放的低电力请求。玩家 1 与玩家 0 使用同样的生产就绪和晋升事件。

`sfx/announcer.cpp` 使用原表优先级和 `QUEUE` 标记调度。即时高优先级播报可以打断较低级播报；排队请求合并重复项，队列有界，十秒后丢弃陈旧请求。重要播报开始时停止单位回应，并在讲话期间限制新的单位语音。主音量静音立即停止、清理播报，换局/读档也清理旧声音。

`sfx/music.cpp` 独立管理菜单、对战、结算、载入和制作人员名单播放列表。当前 UI 已接菜单、对战和结算；后两种场景资源已准备。M 暂停/继续当前流，不重新开始；关闭音乐后换场景不会自行打开。`assets/music/music.ini` 的 `Edition=ra2`、`Volume=35` 控制音轨集和相对音量。Mod 可覆盖 `music/native/manifest.ini` 的对应场景及同名 WAV；场景 `Track=` 列表替换该场景列表，不自动混入根目录旧文件。

声音调度、曲目轮换和播放时间都在表现层，音乐使用独立 RNG。EVA 标识是瞬时表现元数据，不保存、不入 World 校验；历史文字通知仍按已有格式保存。读档不重播已经过去的原声请求。应用 / World / LAN 格式仍为 v7 / v29 / v4，没有通过音频改动修改战斗或经济规则。

## 验证与边界

| 验证 | 结果 |
|---|---|
| Release 相关 13 项 | 13/13，53.67 秒 |
| ASan 相关 4 项 | 4/4，51.82 秒 |
| 补齐 MCV / 间谍接线后的声音与部署专项 | 2/2，2.85 秒 |
| 补充 ASan 声音专项 | 1/1，6.47 秒 |
| 实际来源核对 | 16 首配乐、459 条对话与本地 MIX 逐字节一致 |

声音专项实际打开设备并播放，以进程混音器静音避免打扰用户；验证完整音乐流、暂停续播、优先级抢占、重复合并、过期、阵营隔离、真实生产/基地展开/间谍/核弹、一次性胜负和场景切换。声音与配乐开启期间，300 tick 战斗回放的 World 校验及 RNG 逐 tick 相同。

```powershell
ctest --test-dir build -C Release -R '^ra2.native_audio$' --output-on-failure
ctest --test-dir build -C Release -R '^ra2.native_audio_assets$' --output-on-failure
```

证据在 `build/native-audio-final-tests.log`、`build/native-audio-completion-tests.log`、`build/native-audio-audit.txt` 和 `build-asan/native-audio-tests.log`。本批未重跑全量 85 项，也未完成原版逐项听感、全部任务对白、YR 缺失录音、武器音效与整体混音的人工验收。原版战役与地图仍需继续实现。
