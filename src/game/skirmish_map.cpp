#include "game/skirmish_map.h"
#include "game/world.h"
#include "game/native_map.h"
#include "game/native_scene_contract.h"
#include "core/content.h"
#include "core/sha256.h"
#include <charconv>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace OriginalSkirmishMaps {
namespace {
std::string value(const Ini::Section* s,const char* key) {const auto* v=s?s->get(key):nullptr;return v?v:"";}
bool number(const Ini::Section* s,const char* key,int& out,int low,int high) {
    const auto text=value(s,key);const auto parsed=std::from_chars(text.data(),text.data()+text.size(),out);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && out>=low && out<=high;
}
std::string fileHash(const std::string& path) {
    std::ifstream f(path,std::ios::binary);if(!f)return {};Sha256 hash;std::array<char,16384> bytes;
    while(f){f.read(bytes.data(),bytes.size());hash.update(bytes.data(),(size_t)f.gcount());}
    return f.eof()?digestHex(hash.digest()):std::string{};
}
std::string trim(std::string s) {
    const auto a=s.find_first_not_of(" \t\r");return a==std::string::npos?std::string{}:s.substr(a,s.find_last_not_of(" \t\r")-a+1);
}
bool imageSize(const std::string& path,int width,int height) {
    std::ifstream f(path,std::ios::binary);unsigned char h[24]{};
    if(!f.read((char*)h,sizeof(h)) || std::memcmp(h,"\x89PNG\r\n\x1a\n",8))return false;
    const auto n=[](const unsigned char* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];};
    return n(h+16)==(unsigned)width && n(h+20)==(unsigned)height;
}
}
const Choice* find(std::string_view id) {for(const auto& c:choices)if(id==c.id)return &c;return nullptr;}

bool load(std::string_view id,Data& result,int players) {
    Data prepared;prepared.choice=find(id);if(!prepared.choice || players<2 || players>prepared.choice->players)return false;
    const std::string directory="assets/maps/native/"+std::string(id)+"/";
    const auto path=contentResolve((directory+"skirmish.ini").c_str());
    if(path.empty() || !prepared.spec.load(path.c_str()))return false;
    for(const auto& s:prepared.spec.sections){std::set<std::string> seen;
        for(const auto& [key,_]:s.kv)if(!seen.insert(key).second)return false;}
    const auto* header=prepared.spec.find("SkirmishMap");int version=0,count=0;
    if(!number(header,"Version",version,1,1) || value(header,"Id")!=id
        || !number(header,"Players",count,2,8) || count!=prepared.choice->players
        || !number(header,"Types",prepared.types,1,128) || !number(header,"Actors",prepared.actors,0,65536)
        || !number(header,"Overrides",prepared.overrides,0,128))return false;
    prepared.terrainPath=contentResolve((directory+"terrain.nmap").c_str());
    prepared.terrainHash=fileHash(prepared.terrainPath);
    if(prepared.terrainHash.empty() || prepared.terrainHash!=value(header,"TerrainSHA256"))return false;
    std::vector<Vec2i> unused;
    if(!loadNativeMap(prepared.terrainPath.c_str(),prepared.terrain,players,unused)
        || prepared.terrain.native.assetId!=id || prepared.terrain.native.sourceHash!=value(header,"SourceSHA256")
        || prepared.terrain.native.width!=prepared.choice->width || prepared.terrain.native.height!=prepared.choice->height)return false;
    const int side=prepared.terrain.w;
    if(!imageSize(contentResolve((directory+"terrain.png").c_str()),side*TILE_W,side*TILE_H)
        || !imageSize(contentResolve((directory+"terrain-owners.png").c_str()),side*TILE_W,side*TILE_H))return false;
    for(int i=0;i<count;++i){Vec2i p;const auto* row=prepared.spec.find(("Start."+std::to_string(i)).c_str());
        if(!number(row,"X",p.x,0,side-1) || !number(row,"Y",p.y,0,side-1) || !prepared.terrain.passable(p.x,p.y))return false;
        prepared.starts.push_back(p);}
    for(int i=0;i<prepared.types;++i){const auto* row=prepared.spec.find(("Type."+std::to_string(i)).c_str());int kind=0;
        if(!number(row,"Kind",kind,1,2) || value(row,"Id").empty() || value(row,"Building")!="1"
            || value(row,"Selectable")!="0" || value(row,"Significant")!="0")return false;}
    for(int i=0;i<prepared.actors;++i){const auto* row=prepared.spec.find(("Actor."+std::to_string(i)).c_str());int type=0,x=0,y=0;
        if(!number(row,"Type",type,0,prepared.types-1) || !number(row,"X",x,0,side-1)
            || !number(row,"Y",y,0,side-1) || !prepared.terrain.inBounds(x,y))return false;}
    result=std::move(prepared);return true;
}

bool composeRules(const std::string& rules,const Data& map,std::string& result) {
    if(!map.choice || rules.size()>4*1024*1024)return false;
    std::istringstream source(rules);std::vector<std::string> lines;std::string line;bool scene=false;
    int version=0,versionLine=-1,typesLine=-1,mapLine=-1,sceneEnd=-1;std::string mapAsset;
    for(int i=0;std::getline(source,line);++i){lines.push_back(line);const auto t=trim(line);
        if(t.starts_with('[')){if(scene && sceneEnd<0)sceneEnd=i;scene=t=="[Scene]";}
        if(!scene)continue;const auto eq=t.find('=');if(eq==std::string::npos)continue;const auto key=trim(t.substr(0,eq));
        if(key=="Version"){versionLine=i;const auto n=trim(t.substr(eq+1));const auto parsed=std::from_chars(n.data(),n.data()+n.size(),version);
            if(parsed.ec!=std::errc{} || parsed.ptr!=n.data()+n.size())return false;}
        if(key=="Types")typesLine=i;if(key=="Map")mapLine=i;
        if(key=="MapAssetSHA256")mapAsset=trim(t.substr(eq+1));
    }
    if(versionLine<0 || typesLine<0 || mapLine<0 || sceneEnd<0)return false;
    World parsed;parsed.numPlayers=2;parsed.players.resize(2);
    if(version==nativeSkirmishMapSceneVersion) {
        parsed.map=map.terrain;parsed.map.bldOccRef=nullptr;
        if(!parsed.restoreNativeDefinitions(rules))return false;
        // Restarts retain the embedded starts/scenery/rules, including map-local overrides.
        if(mapAsset!=map.terrainHash)return false;
        result=rules;return true;
    }
    if(!nativeOrdinaryRulesScene(version) || !parsed.restoreNativeDefinitions(rules) || !parsed.usesRulesProfile()
        || parsed.nativeTypes.size()+map.types>512)return false;
    const int base=(int)parsed.nativeTypes.size();
    lines[versionLine]="Version = "+std::to_string(nativeSkirmishMapSceneVersion);
    lines[typesLine]="Types = "+std::to_string(base+map.types);lines[mapLine]="Map = "+std::string(map.choice->id);
    std::map<int,std::string> techOverrides;
    for(int i=0;i<map.overrides;++i){const auto* row=map.spec.find(("Override."+std::to_string(i)).c_str());int tech=0,index=-1;
        if(!number(row,"TechLevel",tech,-1,1000))return false;
        for(int j=0;j<base;++j)if(parsed.nativeTypes[j]->id==value(row,"Id"))index=j;
        if(index<0)return false;techOverrides[index]=std::to_string(tech);}
    std::ostringstream output;int productionType=-1;
    for(int i=0;i<(int)lines.size();++i){const auto t=trim(lines[i]);
        if(t.starts_with('[')){productionType=-1;const std::string prefix="[NativeProduction.Type.";
            if(t.starts_with(prefix) && t.back()==']'){const auto n=t.substr(prefix.size(),t.size()-prefix.size()-1);
                const auto p=std::from_chars(n.data(),n.data()+n.size(),productionType);if(p.ec!=std::errc{})return false;}}
        if(i==sceneEnd)output<<"RulesBaseVersion = "<<version<<"\nMapRuleTypes = "<<base<<"\nMapSourceSHA256 = "<<map.terrain.native.sourceHash<<"\nMapAssetSHA256 = "<<map.terrainHash
            <<"\nMapStarts = "<<map.starts.size()<<"\nMapActors = "<<map.actors<<'\n';
        if(productionType>=0 && techOverrides.contains(productionType) && trim(t.substr(0,t.find('=')))=="TechLevel")
            output<<"TechLevel = "<<techOverrides[productionType]<<'\n';
        else output<<lines[i]<<'\n';
    }
    for(int i=0;i<(int)map.starts.size();++i)output<<"\n[MapStart."<<i<<"]\nX = "<<map.starts[i].x<<"\nY = "<<map.starts[i].y<<'\n';
    for(int i=0;i<map.types;++i){output<<"\n[Type."<<base+i<<"]\n";
        for(const auto& [key,value]:map.spec.find(("Type."+std::to_string(i)).c_str())->kv)output<<key<<" = "<<value<<'\n';}
    for(int i=0;i<map.actors;++i){output<<"\n[MapActor."<<i<<"]\n";
        for(const auto& [key,value]:map.spec.find(("Actor."+std::to_string(i)).c_str())->kv){
            if(key=="Type"){int type=0;const auto p=std::from_chars(value.data(),value.data()+value.size(),type);if(p.ec!=std::errc{})return false;
                output<<key<<" = "<<base+type<<'\n';}else output<<key<<" = "<<value<<'\n';}}
    result=output.str();return result.size()<=4*1024*1024;
}
}
