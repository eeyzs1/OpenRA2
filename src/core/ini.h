#pragma once
// ===================== 极简 INI 解析器（素材外置化基础） =====================
// 格式约定（RA2 风格）：
//   [Section]        节名区分大小写；同名节后者合并进前者
//   key=value        键区分大小写；同一节内允许重复键（如任务列表 Mission=xxx 多行）
//   ; 或 # 开头      整行注释；值中 " ;" / " #"（空白后）起的内容也视为注释
//   UTF-8 原样保留   值不做转义，首尾空白裁剪
// 文件缺失/无有效节时 load 返回 false，调用方回退内置默认。
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>

struct Ini {
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> kv;
        // 未命中返回 nullptr；重复键取首个
        const char* get(const char* key) const {
            for (const auto& p : kv)
                if (p.first == key) return p.second.c_str();
            return nullptr;
        }
        bool has(const char* key) const { return get(key) != nullptr; }
    };

    std::vector<Section> sections;

    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        sections.clear();
        Section* cur = nullptr;
        char line[1024];
        bool firstLine = true;
        while (fgets(line, sizeof(line), f)) {
            char* s = line;
            if (firstLine) { // 去 UTF-8 BOM
                firstLine = false;
                if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
            }
            // 裁剪行注释：行首 ;/#，或空白后的 ;/#
            for (char* p = s; *p; p++) {
                if ((*p == ';' || *p == '#') && (p == s || p[-1] == ' ' || p[-1] == '\t')) { *p = 0; break; }
            }
            trim(s);
            if (!*s) continue;
            if (*s == '[') {
                char* e = strchr(s, ']');
                if (!e) continue;
                *e = 0;
                const char* nm = s + 1;
                cur = nullptr;
                for (auto& sec : sections)
                    if (sec.name == nm) { cur = &sec; break; }
                if (!cur) { sections.push_back(Section{}); cur = &sections.back(); cur->name = nm; }
                continue;
            }
            if (!cur) continue; // 节外的键忽略
            char* eq = strchr(s, '=');
            if (!eq) continue;
            *eq = 0;
            char* k = s;
            char* v = eq + 1;
            trim(k);
            trim(v);
            if (*k) cur->kv.emplace_back(k, v);
        }
        fclose(f);
        return !sections.empty();
    }

    const Section* find(const char* name) const {
        for (const auto& sec : sections)
            if (sec.name == name) return &sec;
        return nullptr;
    }
    // 一步到位：节+键取值，未命中 nullptr
    const char* get(const char* sec, const char* key) const {
        const Section* s = find(sec);
        return s ? s->get(key) : nullptr;
    }

    // ---- 类型转换（未命中/非法回退默认）----
    static int toInt(const char* s, int def) {
        if (!s || !*s) return def;
        char* e = nullptr;
        long v = strtol(s, &e, 10);
        return (e && e != s) ? (int)v : def;
    }
    static float toFloat(const char* s, float def) {
        if (!s || !*s) return def;
        char* e = nullptr;
        float v = strtof(s, &e);
        return (e && e != s) ? v : def;
    }
    static bool toBool(const char* s, bool def) {
        if (!s || !*s) return def;
        if (!strcmp(s, "1") || !strcmp(s, "yes") || !strcmp(s, "true") || !strcmp(s, "Yes") || !strcmp(s, "True")) return true;
        if (!strcmp(s, "0") || !strcmp(s, "no") || !strcmp(s, "false") || !strcmp(s, "No") || !strcmp(s, "False")) return false;
        return def;
    }
    int getInt(const char* sec, const char* key, int def) const { return toInt(get(sec, key), def); }
    float getFloat(const char* sec, const char* key, float def) const { return toFloat(get(sec, key), def); }
    bool getBool(const char* sec, const char* key, bool def) const { return toBool(get(sec, key), def); }

private:
    // 原地裁剪首尾空白（传引用，指针随之前移）
    static void trim(char*& s) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    }
};
