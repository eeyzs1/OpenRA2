#pragma once
// ===================== 内容根 / Mod 叠加载（定制化最终形态） =====================
// 游戏启动后所有「素材相对路径」经本模块解析：
//   1) 内置/发行包：assets/... 、maps/...
//   2) mods/<Name>/... 叠加载（后加载覆盖先加载；INI 可多层合并）
//
// Mod 目录约定（与 assets 镜像，去掉 "assets/" 前缀亦可）：
//   mods/MyMod/mod.ini
//   mods/MyMod/rules/rules.ini
//   mods/MyMod/campaigns/...
//   mods/MyMod/maps/...
//   mods/MyMod/sprites|strings|scripts|sfx|music|gui|voxels|palettes/...
#include <string>
#include <vector>

struct ContentMod {
    std::string id;       // 目录名
    std::string path;     // 绝对或相对游戏根的 mod 根路径
    std::string name;     // 显示名（mod.ini Name=）
    int loadOrder = 0;    // 越小越先；同序按 id
    bool enabled = true;
};

// 解析 CLI / 扫描 mods/ 后调用。baseGameRoot 通常为当前工作目录。
// extraModPaths（--mod）强制 Enabled=true，可覆盖 mod.ini 的 Enabled=no。
void contentInit(const std::vector<std::string>& extraModPaths);

// 追加一个 mod 根；forceEnable=true 时忽略 mod.ini Enabled=no（CLI --mod 用）
void contentAddModPath(const std::string& path, bool forceEnable = false);

const std::vector<ContentMod>& contentMods();

// 解析「游戏相对路径」→ 实际文件路径（后写覆盖：返回优先级最高且存在的文件）
// 例：assets/rules/rules.ini 、maps/foo.txt 、assets/sprites/unit_grizzly_d0.png
std::string contentResolve(const char* gameRelPath);

// 是否存在（任一内容根）
bool contentExists(const char* gameRelPath);

// 合并栈：从低到高优先级（先 base 后 mods），仅包含实际存在的文件
// 用于 rules/strings 等「多层 patch」
std::vector<std::string> contentResolveStack(const char* gameRelPath);

// 列出某虚拟目录下的文件名（不递归）。virtualDir 如 "assets/scripts" 或 "maps"
// 合并各根，同名后者覆盖（返回去重后的相对名列表，已按最终可见集）
std::vector<std::string> contentListFiles(const char* virtualDir, const char* ext /* ".lua" or nullptr */);

// 便捷：printf 风格拼路径再 resolve（内部缓冲）
const char* contentPathFmt(const char* fmt, ...);

// 诊断日志
void contentLogRoots();
