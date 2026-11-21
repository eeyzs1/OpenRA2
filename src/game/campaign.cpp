#include "game/campaign.h"
#include "core/ini.h"
#include "core/content.h"
#include "raylib.h"
#include <cstring>
#include <cstdio>
#include <unordered_map>

// ===================== 外部战役加载（assets/campaigns/） =====================
static bool trigCondByName(const char* s, TrigCond& out) {
    if (!s) return false;
    if (!strcmp(s, "Always")) { out = TrigCond::Always; return true; }
    if (!strcmp(s, "Time")) { out = TrigCond::Time; return true; }
    if (!strcmp(s, "PlayerBldLost")) { out = TrigCond::PlayerBldLost; return true; }
    if (!strcmp(s, "PlayerAllDead")) { out = TrigCond::PlayerAllDead; return true; }
    if (!strcmp(s, "UnitInRect")) { out = TrigCond::UnitInRect; return true; }
    if (!strcmp(s, "MoneyBelow")) { out = TrigCond::MoneyBelow; return true; }
    if (!strcmp(s, "UnitLost")) { out = TrigCond::UnitLost; return true; }
    if (!strcmp(s, "Script")) { out = TrigCond::Script; return true; }
    if (!strcmp(s, "BldCaptured")) { out = TrigCond::BldCaptured; return true; }
    if (!strcmp(s, "ObjAllPrimary")) { out = TrigCond::ObjAllPrimary; return true; }
    if (!strcmp(s, "UnitCountBelow")) { out = TrigCond::UnitCountBelow; return true; }
    if (!strcmp(s, "PhaseAt")) { out = TrigCond::PhaseAt; return true; }
    return false;
}
static bool trigActByName(const char* s, TrigAct& out) {
    if (!s) return false;
    if (!strcmp(s, "SpawnAt")) { out = TrigAct::SpawnAt; return true; }
    if (!strcmp(s, "Eva")) { out = TrigAct::Eva; return true; }
    if (!strcmp(s, "GiveMoney")) { out = TrigAct::GiveMoney; return true; }
    if (!strcmp(s, "RevealMap")) { out = TrigAct::RevealMap; return true; }
    if (!strcmp(s, "Win")) { out = TrigAct::Win; return true; }
    if (!strcmp(s, "Lose")) { out = TrigAct::Lose; return true; }
    if (!strcmp(s, "Objective")) { out = TrigAct::Objective; return true; }
    if (!strcmp(s, "Script")) { out = TrigAct::Script; return true; }
    if (!strcmp(s, "CompleteObj")) { out = TrigAct::CompleteObj; return true; }
    if (!strcmp(s, "SetPhase")) { out = TrigAct::SetPhase; return true; }
    if (!strcmp(s, "EnableTag")) { out = TrigAct::EnableTag; return true; }
    if (!strcmp(s, "Reinforce")) { out = TrigAct::Reinforce; return true; }
    if (!strcmp(s, "TimerStart")) { out = TrigAct::TimerStart; return true; }
    if (!strcmp(s, "TimerAbort")) { out = TrigAct::TimerAbort; return true; }
    return false;
}

// 逗号分隔单位列表："Conscript,Rhino" → units
static bool parseUnitList(const char* s, std::vector<UnitType>& out) {
    if (!s) return false;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", s);
    bool ok = true;
    for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        UnitType ut;
        if (!resolveUnitSpawn(tok, ut)) { ok = false; break; }
        out.push_back(ut);
    }
    return ok && !out.empty();
}

static bool parseBldList(const char* s, std::vector<BldType>& out) {
    if (!s || !*s) return true; // 空=不限制
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", s);
    bool ok = true;
    for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        if (!*tok) continue;
        BldType bt;
        if (!bldTypeByName(tok, bt)) { ok = false; break; }
        out.push_back(bt);
    }
    return ok;
}

static bool parseUnitListAllowEmpty(const char* s, std::vector<UnitType>& out) {
    if (!s || !*s) return true;
    return parseUnitList(s, out);
}

// 逗号分隔阵营列表："Soviet,Soviet" → aiFactions
static bool parseFactionList(const char* s, std::vector<Faction>& out) {
    if (!s) return false;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", s);
    bool ok = true;
    for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        Faction f;
        if (!factionByName(tok, f)) { ok = false; break; }
        out.push_back(f);
    }
    return ok && !out.empty();
}

// C1/类型名：优先整数；否则按条件解析单位/建筑名
static int parseCondTypeParam(TrigCond cond, const char* raw, int fallback) {
    if (!raw || !*raw) return fallback;
    bool digits = true;
    for (const char* p = raw; *p; ++p)
        if (!(*p == '-' || (*p >= '0' && *p <= '9'))) { digits = false; break; }
    if (digits) return Ini::toInt(raw, fallback);
    if (cond == TrigCond::UnitLost || cond == TrigCond::UnitCountBelow) {
        UnitType ut;
        if (unitTypeByName(raw, ut)) return (int)ut;
    }
    if (cond == TrigCond::PlayerBldLost || cond == TrigCond::BldCaptured) {
        BldType bt;
        if (bldTypeByName(raw, bt)) return (int)bt;
    }
    TraceLog(LOG_WARNING, "RA2 campaign: unknown type name '%s' for cond", raw);
    return fallback;
}

// 单关文件：[General] + [Wave.N]... + [Trig.N]... + [Objective.N]...
static bool loadMissionFile(const char* path, MissionDef& md) {
    Ini ini;
    if (!ini.load(path)) return false;
    const Ini::Section* g = ini.find("General");
    if (!g) return false;
    const char* v;
    if (!(v = g->get("Name"))) return false;
    md.name = v;
    if ((v = g->get("NameEn"))) md.nameEn = v;
    if ((v = g->get("Brief"))) md.brief = v;
    if ((v = g->get("BriefEn"))) md.briefEn = v;
    if (!(v = g->get("Faction")) || !factionByName(v, md.playerFaction)) return false;
    if (!(v = g->get("AI")) || !parseFactionList(v, md.aiFactions)) return false;
    md.mapSize = Ini::toInt(g->get("MapSize"), 96);
    md.mapType = Ini::toInt(g->get("MapType"), 0);
    md.money = Ini::toInt(g->get("Money"), 10000);
    md.objective = Ini::toInt(g->get("Objective"), 0);
    md.objectiveTick = Ini::toInt(g->get("ObjectiveTick"), 0);
    if ((v = g->get("MapFile"))) md.mapFile = v;
    if ((v = g->get("NativeScene"))) md.nativeScene = v;
    if ((v = g->get("NativeTextFile"))) md.nativeTextFile = v;
    if (!md.nativeScene.empty()) {
        // Install an entry only when its real content is present. A missing
        // original scene must never silently start a generated skirmish.
        if (md.mapFile.empty() || contentResolve(md.nativeScene.c_str()).empty()
            || contentResolve(md.mapFile.c_str()).empty()) return false;
        if (!md.nativeTextFile.empty()) {
            const auto textPath=contentResolve(md.nativeTextFile.c_str());Ini texts;
            if (textPath.empty() || !texts.load(textPath.c_str())) return false;
            if (const auto* section=texts.find("Text")) md.nativeTexts=section->kv;
            if (md.nativeTexts.empty()) return false;
            for(auto& [key,text]:md.nativeTexts) {
                std::string decoded;
                for(size_t i=0;i<text.size();++i) {
                    if(text[i]=='\\' && i+1<text.size()) {
                        const char code=text[++i];decoded+=code=='n'?'\n':code=='s'?';':code=='h'?'#':code;
                    } else decoded+=text[i];
                }
                text=std::move(decoded);
            }
            auto sourceText=[&](const char* field,std::string& destination) {
                const char* label=g->get(field);if(!label)return true;std::string key=label;
                for(char& c:key)if(c>='A' && c<='Z')c+='a'-'A';
                for(const auto& entry:md.nativeTexts)if(entry.first==key){destination=entry.second;return true;}
                return false;
            };
            if(!sourceText("NativeNameKey",md.name) || !sourceText("NativeBriefKey",md.brief))return false;
        }
    }
    md.noStartForce = Ini::toBool(g->get("NoStartForce"), false);
    md.track = Ini::toInt(g->get("Track"), 0); // 0=融合 1=官方
    if ((v = g->get("LineId"))) md.lineId = v;
    md.lineIndex = Ini::toInt(g->get("LineIndex"), 0);
    if ((v = g->get("Country")) && strcmp(v, "None") != 0) {
        if (!countryByName(v, md.playerCountry))
            TraceLog(LOG_WARNING, "RA2 campaign: %s bad Country=%s", path, v);
    }
    if ((v = g->get("AllowedBuildings"))) {
        if (!parseBldList(v, md.allowedBuildings))
            TraceLog(LOG_WARNING, "RA2 campaign: %s bad AllowedBuildings", path);
    }
    if ((v = g->get("AllowedUnits"))) {
        if (!parseUnitListAllowEmpty(v, md.allowedUnits))
            TraceLog(LOG_WARNING, "RA2 campaign: %s bad AllowedUnits", path);
    }
    if ((v = g->get("BriefArt"))) md.briefArt = v;
    md.winOnAllPrimary = Ini::toBool(g->get("WinOnAllPrimary"), md.objective == 2);
    md.startPhase = Ini::toInt(g->get("Phase"), 0);
    md.timerVisibleDefault = Ini::toBool(g->get("TimerVisible"), false);
    for (const Ini::Section& sec : ini.sections) {
        if (sec.name.rfind("Wave.", 0) == 0) {
            MissionWave w{};
            w.atTick = Ini::toInt(sec.get("At"), 0);
            if (!(v = sec.get("Units")) || !parseUnitList(v, w.units)) {
                TraceLog(LOG_WARNING, "RA2 campaign: %s [%s] bad Units, wave skipped", path, sec.name.c_str());
                continue;
            }
            md.waves.push_back(std::move(w));
        } else if (sec.name.rfind("Objective.", 0) == 0) {
            MissionObjective o;
            if ((v = sec.get("Text"))) o.text = v;
            if ((v = sec.get("TextEn"))) o.textEn = v;
            o.primary = Ini::toBool(sec.get("Primary"), true);
            o.gateWin = Ini::toBool(sec.get("GateWin"), o.primary);
            if ((v = sec.get("NativeCompleteTrigger"))) o.nativeCompleteTrigger=v;
            o.nativeCompleteOnVictory=Ini::toBool(sec.get("NativeCompleteOnVictory"),false);
            if (!o.text.empty()) md.objectives.push_back(std::move(o));
        } else if (sec.name.rfind("Trig.", 0) == 0) {
            Trigger t;
            if (!(v = sec.get("Cond")) || !trigCondByName(v, t.cond)) {
                TraceLog(LOG_WARNING, "RA2 campaign: %s [%s] bad Cond, trigger skipped", path, sec.name.c_str());
                continue;
            }
            if (!(v = sec.get("Act")) || !trigActByName(v, t.act)) {
                TraceLog(LOG_WARNING, "RA2 campaign: %s [%s] bad Act, trigger skipped", path, sec.name.c_str());
                continue;
            }
            for (int i = 0; i < 5; i++) {
                char k[8];
                snprintf(k, sizeof(k), "C%d", i);
                const char* cv = sec.get(k);
                if (i == 1 && (t.cond == TrigCond::UnitLost || t.cond == TrigCond::PlayerBldLost
                               || t.cond == TrigCond::BldCaptured || t.cond == TrigCond::UnitCountBelow)) {
                    // 支持 C1=Tanya / CType=Tanya / BType=NukeSilo
                    const char* named = sec.get("CType");
                    if (!named) named = sec.get("BType");
                    if (!named) named = sec.get("UType");
                    if (named) t.c[i] = parseCondTypeParam(t.cond, named, 0);
                    else t.c[i] = parseCondTypeParam(t.cond, cv, Ini::toInt(cv, 0));
                } else {
                    t.c[i] = Ini::toInt(cv, 0);
                }
                snprintf(k, sizeof(k), "A%d", i);
                t.a[i] = Ini::toInt(sec.get(k), i == 3 ? -1 : 0);
            }
            if (t.cond == TrigCond::BldCaptured && t.c[2] <= 0) t.c[2] = 1;
            if ((v = sec.get("Units"))) parseUnitList(v, t.units);
            if ((v = sec.get("Msg"))) t.msg = v;
            if ((v = sec.get("MsgEn"))) t.msgEn = v;
            if ((v = sec.get("Tag"))) t.tag = v;
            t.once = Ini::toBool(sec.get("Once"), true);
            t.enabled = Ini::toBool(sec.get("Enabled"), true);
            t.requiresPhase = Ini::toInt(sec.get("RequiresPhase"), -1);
            md.triggers.push_back(std::move(t));
        }
    }
    return true;
}

static void resolveMissionLinks(std::vector<MissionDef>& v) {
    for (size_t i = 0; i < v.size(); i++) {
        MissionDef& md = v[i];
        md.nextMission = -1;
        if (md.lineId.empty()) continue;
        for (size_t j = 0; j < v.size(); j++) {
            if (j == i) continue;
            if (v[j].lineId == md.lineId && v[j].lineIndex == md.lineIndex + 1) {
                md.nextMission = (int)j;
                break;
            }
        }
    }
}

// ===================== 战役进度（userdata/campaign_progress.ini） =====================
static std::unordered_map<std::string, int> g_campProgress;
static bool g_campProgressLoaded = false;

void campaignLoadProgress() {
    g_campProgress.clear();
    Ini ini;
    if (ini.load("userdata/campaign_progress.ini")) {
        if (const Ini::Section* s = ini.find("Progress")) {
            for (const auto& p : s->kv)
                g_campProgress[p.first] = Ini::toInt(p.second.c_str(), 0);
        }
    }
    g_campProgressLoaded = true;
}

void campaignSaveProgress() {
    MakeDirectory("userdata");
    if (FILE* f = fopen("userdata/campaign_progress.ini", "wb")) {
        fprintf(f, "; OpenRA2 campaign unlock progress (next unlocked lineIndex per LineId)\n[Progress]\n");
        for (const auto& p : g_campProgress)
            fprintf(f, "%s=%d\n", p.first.c_str(), p.second);
        fclose(f);
    }
}

int campaignProgress(const std::string& lineId) {
    if (!g_campProgressLoaded) campaignLoadProgress();
    if (lineId.empty()) return 999; // 无线=全解锁
    auto it = g_campProgress.find(lineId);
    return it == g_campProgress.end() ? 0 : it->second;
}

void campaignSetProgress(const std::string& lineId, int clearedNextIndex) {
    if (lineId.empty()) return;
    if (!g_campProgressLoaded) campaignLoadProgress();
    int cur = campaignProgress(lineId);
    if (clearedNextIndex > cur) {
        g_campProgress[lineId] = clearedNextIndex;
        campaignSaveProgress();
    }
}

bool campaignMissionUnlocked(int missionIndex) {
    const auto& tbl = missionTable();
    if (missionIndex < 0 || missionIndex >= (int)tbl.size()) return false;
    const MissionDef& md = tbl[missionIndex];
    if (md.lineId.empty()) return true;
    return md.lineIndex <= campaignProgress(md.lineId);
}

int campaignFindNextMission(int missionIndex) {
    const auto& tbl = missionTable();
    if (missionIndex < 0 || missionIndex >= (int)tbl.size()) return -1;
    return tbl[missionIndex].nextMission;
}

const std::vector<MissionDef>& missionTable() {
    static const std::vector<MissionDef> tbl = [] {
        std::vector<MissionDef> v;
        // 叠加载所有 campaign.ini：默认追加；任一文件 [Mod] ReplaceMissions=yes 则清空后重加
        auto stacks = contentResolveStack("assets/campaigns/campaign.ini");
        if (stacks.empty() && FileExists("assets/campaigns/campaign.ini"))
            stacks.push_back("assets/campaigns/campaign.ini");
        for (const auto& listPath : stacks) {
            Ini list;
            if (!list.load(listPath.c_str())) continue;
            bool replace = false;
            if (const Ini::Section* mod = list.find("Mod"))
                replace = Ini::toBool(mod->get("ReplaceMissions"), false);
            if (const Ini::Section* ms = list.find("Missions")) {
                if (Ini::toBool(ms->get("Replace"), false)) replace = true;
            }
            if (replace) v.clear();
            // 列表文件所在目录（用于 Mission=相对路径）
            std::string listDir = listPath;
            auto slash = listDir.find_last_of("/\\");
            if (slash != std::string::npos) listDir = listDir.substr(0, slash);
            else listDir = ".";
            if (const Ini::Section* ms = list.find("Missions")) {
                for (const auto& p : ms->kv) {
                    if (p.first != "Mission") continue;
                    // 1) 与 campaign.ini 同目录
                    std::string local = listDir + "/" + p.second;
                    // 2) 内容根 assets/campaigns/...
                    std::string virt = std::string("assets/campaigns/") + p.second;
                    std::string path = local;
                    if (!FileExists(path.c_str())) {
                        path = contentResolve(virt.c_str());
                        if (path.empty()) path = virt;
                    }
                    MissionDef md{};
                    if (loadMissionFile(path.c_str(), md)) v.push_back(std::move(md));
                    else TraceLog(LOG_WARNING, "RA2 campaign: %s load failed, mission skipped", path.c_str());
                }
            }
        }
        // missions.csv：一行一个任务文件名，追加（后于各 campaign.ini）
        for (const auto& csvPath : contentResolveStack("assets/campaigns/missions.csv")) {
            FILE* cf = fopen(csvPath.c_str(), "rb");
            if (!cf) continue;
            char buf[512];
            bool haveHdr = false;
            int missionCol = 0;
            std::string csvDir = csvPath;
            auto slash = csvDir.find_last_of("/\\");
            if (slash != std::string::npos) csvDir = csvDir.substr(0, slash);
            else csvDir = ".";
            int added = 0;
            while (fgets(buf, sizeof(buf), cf)) {
                std::string line = buf;
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                    line.pop_back();
                size_t lead = 0;
                while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) lead++;
                line = line.substr(lead);
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                if (!haveHdr) {
                    // 允许 "Mission" 表头或直接数据行
                    if (line == "Mission" || line.rfind("Mission,", 0) == 0) {
                        haveHdr = true;
                        continue;
                    }
                    haveHdr = true; // 无表头：本行即数据
                } else if (line == "Mission") {
                    continue;
                }
                // 取第一列
                std::string file = line;
                auto comma = file.find(',');
                if (comma != std::string::npos) file = file.substr(0, comma);
                while (!file.empty() && (file.back() == ' ' || file.back() == '\t')) file.pop_back();
                if (file.empty() || file == "Mission") continue;
                std::string local = csvDir + "/" + file;
                std::string virt = std::string("assets/campaigns/") + file;
                std::string path = local;
                if (!FileExists(path.c_str())) {
                    path = contentResolve(virt.c_str());
                    if (path.empty()) path = virt;
                }
                MissionDef md{};
                if (loadMissionFile(path.c_str(), md)) {
                    v.push_back(std::move(md));
                    added++;
                } else {
                    TraceLog(LOG_WARNING, "RA2 campaign: missions.csv entry %s failed", path.c_str());
                }
            }
            fclose(cf);
            if (added > 0)
                TraceLog(LOG_INFO, "RA2 campaign: missions.csv %s added %d mission(s)", csvPath.c_str(), added);
            (void)missionCol;
        }
        TraceLog(LOG_INFO, "RA2 campaign: %d installed mission(s); no built-in campaign fallback", (int)v.size());
        resolveMissionLinks(v);
        campaignLoadProgress();
        return v;
    }();
    return tbl;
}
