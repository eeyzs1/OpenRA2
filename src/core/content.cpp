#include "core/content.h"
#include "core/ini.h"
#include "raylib.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

static std::vector<ContentMod> g_mods;
static std::string g_fmtBuf;

static bool fileOk(const std::string& p) {
    return !p.empty() && FileExists(p.c_str());
}

static std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char sep = '/';
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}

static std::string stripAssetsPrefix(const char* rel) {
    if (!rel) return {};
    if (!strncmp(rel, "assets/", 7)) return rel + 7;
    if (!strncmp(rel, "assets\\", 7)) return rel + 7;
    return rel;
}

// 对单个 mod，生成候选物理路径（按常见布局）
static void modCandidates(const ContentMod& m, const char* gameRel, std::vector<std::string>& out) {
    if (!gameRel || !*gameRel) return;
    std::string stripped = stripAssetsPrefix(gameRel);
    // mods/X/rules/... （推荐：去掉 assets/）
    if (stripped != gameRel)
        out.push_back(joinPath(m.path, stripped));
    // mods/X/assets/rules/... （完整镜像）
    out.push_back(joinPath(m.path, gameRel));
}

static void collectCandidates(const char* gameRel, std::vector<std::string>& out /* base-first */) {
    out.clear();
    if (!gameRel || !*gameRel) return;
    // 1) 发行包原路径
    out.push_back(gameRel);
    // 2) 各 mod（按 loadOrder 升序 = 先加载的在前；后加载覆盖）
    std::vector<ContentMod> mods = g_mods;
    std::stable_sort(mods.begin(), mods.end(), [](const ContentMod& a, const ContentMod& b) {
        if (a.loadOrder != b.loadOrder) return a.loadOrder < b.loadOrder;
        return a.id < b.id;
    });
    for (const ContentMod& m : mods) {
        if (!m.enabled) continue;
        modCandidates(m, gameRel, out);
    }
}

static void loadModIni(ContentMod& m) {
    std::string iniPath = joinPath(m.path, "mod.ini");
    Ini ini;
    if (!ini.load(iniPath.c_str())) {
        m.name = m.id;
        return;
    }
    if (const Ini::Section* g = ini.find("Mod")) {
        if (const char* v = g->get("Name")) m.name = v;
        else m.name = m.id;
        if (const char* v = g->get("LoadOrder")) m.loadOrder = Ini::toInt(v, m.loadOrder);
        if (g->has("Enabled")) m.enabled = Ini::toBool(g->get("Enabled"), true);
    } else {
        m.name = m.id;
    }
}

static ContentMod makeModFromPath(const std::string& path) {
    ContentMod m;
    m.path = path;
    // 规范化去掉尾部分隔符
    while (!m.path.empty() && (m.path.back() == '/' || m.path.back() == '\\')) m.path.pop_back();
    fs::path p(m.path);
    m.id = p.filename().string();
    if (m.id.empty() || m.id == "." || m.id == "..") m.id = m.path;
    loadModIni(m);
    return m;
}

void contentAddModPath(const std::string& path, bool forceEnable) {
    if (path.empty()) return;
    ContentMod m = makeModFromPath(path);
    if (forceEnable) m.enabled = true;
    for (auto& e : g_mods) {
        if (e.path == m.path || e.id == m.id) {
            if (forceEnable) e.enabled = true;
            return;
        }
    }
    g_mods.push_back(std::move(m));
}

void contentInit(const std::vector<std::string>& extraModPaths) {
    g_mods.clear();
    // 扫描 mods/ 下每个子目录（尊重各 mod.ini Enabled）
    if (DirectoryExists("mods")) {
        FilePathList list = LoadDirectoryFilesEx("mods", nullptr, false);
        for (unsigned i = 0; i < list.count; i++) {
            const char* p = list.paths[i];
            if (!DirectoryExists(p)) continue;
            // 跳过隐藏/点目录
            const char* base = strrchr(p, '/');
            const char* base2 = strrchr(p, '\\');
            const char* name = base2 && (!base || base2 > base) ? base2 + 1 : (base ? base + 1 : p);
            if (name[0] == '.') continue;
            contentAddModPath(p, false);
        }
        UnloadDirectoryFiles(list);
    }
    // CLI --mod：强制启用并叠加载
    for (const auto& p : extraModPaths)
        contentAddModPath(p, true);
    // 用户内容根：始终启用、最高优先级（双击启动即可吃到 userdata/content）
    if (!DirectoryExists("userdata"))
        MakeDirectory("userdata");
    if (!DirectoryExists("userdata/content"))
        MakeDirectory("userdata/content");
    contentAddModPath("userdata/content", true);
    for (ContentMod& m : g_mods) {
        if (m.path == "userdata/content" || m.id == "content") {
            m.enabled = true;
            m.loadOrder = 10000;
            if (m.name.empty() || m.name == "content") m.name = "User Content";
        }
    }
    contentLogRoots();
}

const std::vector<ContentMod>& contentMods() { return g_mods; }

std::string contentResolve(const char* gameRelPath) {
    std::vector<std::string> cands;
    collectCandidates(gameRelPath, cands);
    std::string best;
    for (const auto& c : cands)
        if (fileOk(c)) best = c; // 后者覆盖
    return best;
}

bool contentExists(const char* gameRelPath) {
    return !contentResolve(gameRelPath).empty();
}

std::vector<std::string> contentResolveStack(const char* gameRelPath) {
    std::vector<std::string> cands, out;
    collectCandidates(gameRelPath, cands);
    std::unordered_set<std::string> seen;
    for (const auto& c : cands) {
        if (!fileOk(c)) continue;
        // 规范化去重（同一文件不同写法仍可能重复；以字符串为准）
        if (seen.count(c)) continue;
        seen.insert(c);
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> contentListFiles(const char* virtualDir, const char* ext) {
    // 收集各根下该目录的文件，后者覆盖同名
    std::unordered_map<std::string, std::string> byName; // filename -> full path (unused, keep last)
    std::vector<std::string> order;

    auto scanDir = [&](const std::string& dir) {
        if (!DirectoryExists(dir.c_str())) return;
        FilePathList list = LoadDirectoryFilesEx(dir.c_str(), ext, false);
        for (unsigned i = 0; i < list.count; i++) {
            const char* full = list.paths[i];
            if (DirectoryExists(full)) continue;
            const char* base = strrchr(full, '/');
            const char* base2 = strrchr(full, '\\');
            const char* name = base2 && (!base || base2 > base) ? base2 + 1 : (base ? base + 1 : full);
            std::string key = name;
            if (!byName.count(key)) order.push_back(key);
            byName[key] = full;
        }
        UnloadDirectoryFiles(list);
    };

    // base
    scanDir(virtualDir);
    // strip assets/ for mods
    std::string stripped = stripAssetsPrefix(virtualDir);

    std::vector<ContentMod> mods = g_mods;
    std::stable_sort(mods.begin(), mods.end(), [](const ContentMod& a, const ContentMod& b) {
        if (a.loadOrder != b.loadOrder) return a.loadOrder < b.loadOrder;
        return a.id < b.id;
    });
    for (const ContentMod& m : mods) {
        if (!m.enabled) continue;
        if (stripped != virtualDir)
            scanDir(joinPath(m.path, stripped));
        scanDir(joinPath(m.path, virtualDir));
    }
    return order;
}

const char* contentPathFmt(const char* fmt, ...) {
    char raw[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(raw, sizeof(raw), fmt, ap);
    va_end(ap);
    g_fmtBuf = contentResolve(raw);
    if (g_fmtBuf.empty()) g_fmtBuf = raw; // 回退原路径（便于缺文件报错信息）
    return g_fmtBuf.c_str();
}

void contentLogRoots() {
    TraceLog(LOG_INFO, "CONTENT: base=./assets + ./maps ; mods=%d", (int)g_mods.size());
    for (const auto& m : g_mods) {
        TraceLog(LOG_INFO, "CONTENT: mod id=%s name=\"%s\" order=%d enabled=%d path=%s",
                 m.id.c_str(), m.name.c_str(), m.loadOrder, (int)m.enabled, m.path.c_str());
    }
}
