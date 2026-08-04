# Fetch Yuri's Revenge MIX (RA2MD.MIX)

## 状态（2026-08-05）

已从 Archive.org `red-alert-2_202103` / `Red Alert 2.7z` 装入：

- `tools/ra2pack/game/RA2MD.MIX`
- `LANGMD.MIX` / `MAPSMD03.MIX` / `MULTIMD.MIX` / `THEMEMD.MIX`

并已用 `gen_assets.py` 提取 12 个尤复建筑 SHP（含心灵控制器 `YAPPET`、机器人控制 `GAROBO`、坦克碉堡 `ngtbnkmk`）。

下载包缓存：`tools/ra2pack/dl/RedAlert2.7z`（约 277MB，可删以省空间）。

## 中国网络备忘

| 途径 | 说明 |
|------|------|
| Internet Archive | 翻墙后可用；直链 `ia601800.us.archive.org` 往往比 `archive.org` 稳 |
| 阿里云盘 / 百度 | 国内可达；常需登录转存 |

## 重新提取

```bat
cd tools\ra2pack
python gen_assets.py --only=bld_bioreactor,bld_gatlingcannon,bld_grinder,bld_geneticmutator,bld_psychicdominator,bld_psychictower,bld_tankbunker,bld_battlebunker,bld_industrialplant,bld_machineshop,bld_techpowerplant,bld_robotcontrol
```

Rules 映射注意：`psychicdominator`→`YAPPET`，`robotcontrol`→`GAROBO`（不是旧文档里的 YAPSYC/NAROBP）。
