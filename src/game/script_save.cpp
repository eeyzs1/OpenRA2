#include "game/script.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
constexpr size_t MAX_STATE=4*1024*1024, MAX_SOURCES=8*1024*1024;
constexpr uint32_t MAX_ENTRIES=65536, MAX_TABLES=4096;
enum ValueTag : uint8_t { Nil, Boolean, Integer, Number, String, Table, Reference };
struct StackGuard {
    lua_State* vm;int top;
    explicit StackGuard(lua_State* state):vm(state),top(lua_gettop(state)){}
    ~StackGuard(){lua_settop(vm,top);}
};
struct References {
    lua_State* vm;std::vector<int> refs;
    ~References(){for(int ref:refs)luaL_unref(vm,LUA_REGISTRYINDEX,ref);}
    int retain(int index){lua_pushvalue(vm,index);int ref=luaL_ref(vm,LUA_REGISTRYINDEX);refs.push_back(ref);return ref;}
};

struct Encoder {
    lua_State* vm;uint32_t entries=0;size_t keyBytes=0;
    std::map<const void*,uint32_t> tables;
    template<class T> bool put(std::vector<uint8_t>& out,const T& value) {
        if(out.size()+sizeof(value)>MAX_STATE)return false;
        const auto* bytes=reinterpret_cast<const uint8_t*>(&value);
        out.insert(out.end(),bytes,bytes+sizeof(value));return true;
    }
    bool value(std::vector<uint8_t>& out,int index,int depth=0,bool key=false) {
        if(depth>64 || !lua_checkstack(vm,8))return false;
        StackGuard stack(vm);index=lua_absindex(vm,index);
        switch(lua_type(vm,index)) {
            case LUA_TNIL:return !key && put(out,(uint8_t)Nil);
            case LUA_TBOOLEAN:return put(out,(uint8_t)Boolean) && put(out,(uint8_t)(lua_toboolean(vm,index)!=0));
            case LUA_TNUMBER:
                if(lua_isinteger(vm,index))return put(out,(uint8_t)Integer) && put(out,(int64_t)lua_tointeger(vm,index));
                else {const double number=lua_tonumber(vm,index);return std::isfinite(number) && put(out,(uint8_t)Number) && put(out,number);}
            case LUA_TSTRING: {
                size_t length=0;const char* text=lua_tolstring(vm,index,&length);
                if(length>MAX_STATE || !put(out,(uint8_t)String) || !put(out,(uint32_t)length) || out.size()+length>MAX_STATE)return false;
                out.insert(out.end(),text,text+length);return true;
            }
            case LUA_TTABLE:break;
            default:return false; // functions, userdata and live coroutines are not declared save data.
        }
        if(key || lua_getmetatable(vm,index))return false;
        const void* address=lua_topointer(vm,index);
        if(auto found=tables.find(address);found!=tables.end())return put(out,(uint8_t)Reference) && put(out,found->second);
        if(tables.size()>=MAX_TABLES)return false;
        const uint32_t id=(uint32_t)tables.size();tables.emplace(address,id);
        struct Entry {std::vector<uint8_t> key;int value;};
        std::vector<Entry> items;References retained{vm,{}};
        lua_pushnil(vm);
        while(lua_next(vm,index)) {
            if(++entries>MAX_ENTRIES)return false;
            Entry entry;
            if(!value(entry.key,-2,depth+1,true))return false;
            if(entry.key.size()>MAX_STATE-keyBytes)return false;
            keyBytes+=entry.key.size();
            entry.value=retained.retain(-1);items.push_back(std::move(entry));lua_pop(vm,1);
        }
        std::sort(items.begin(),items.end(),[](const Entry& a,const Entry& b){return a.key<b.key;});
        if(!put(out,(uint8_t)Table) || !put(out,id) || !put(out,(uint32_t)items.size()))return false;
        for(const auto& entry:items) {
            if(out.size()+entry.key.size()>MAX_STATE)return false;
            out.insert(out.end(),entry.key.begin(),entry.key.end());
            lua_rawgeti(vm,LUA_REGISTRYINDEX,entry.value);
            if(!value(out,-1,depth+1))return false;
            lua_pop(vm,1);
        }
        return true;
    }
};

struct Decoder {
    lua_State* vm;const std::vector<uint8_t>& bytes;size_t offset=0;uint32_t entries=0;
    References tables;
    template<class T> bool get(T& value) {
        if(sizeof(value)>bytes.size()-offset)return false;
        memcpy(&value,bytes.data()+offset,sizeof(value));offset+=sizeof(value);return true;
    }
    bool value(int depth=0,bool key=false) {
        if(depth>64 || !lua_checkstack(vm,8))return false;
        uint8_t tag=0;if(!get(tag))return false;
        switch(tag) {
            case Nil:if(key)return false;lua_pushnil(vm);return true;
            case Boolean: {uint8_t flag=0;if(!get(flag) || flag>1)return false;lua_pushboolean(vm,flag);return true;}
            case Integer: {int64_t number=0;if(!get(number))return false;lua_pushinteger(vm,(lua_Integer)number);return true;}
            case Number: {double number=0;if(!get(number) || !std::isfinite(number))return false;lua_pushnumber(vm,number);return true;}
            case String: {
                uint32_t length=0;if(!get(length) || length>bytes.size()-offset)return false;
                lua_pushlstring(vm,reinterpret_cast<const char*>(bytes.data()+offset),length);offset+=length;return true;
            }
            case Reference: {
                uint32_t id=0;if(key || !get(id) || id>=tables.refs.size())return false;
                lua_rawgeti(vm,LUA_REGISTRYINDEX,tables.refs[id]);return true;
            }
            case Table:break;
            default:return false;
        }
        uint32_t id=0,count=0;
        if(key || !get(id) || !get(count) || id!=tables.refs.size() || id>=MAX_TABLES || count>MAX_ENTRIES-entries)return false;
        entries+=count;lua_createtable(vm,0,(int)count);const int table=lua_gettop(vm);tables.retain(table);
        for(uint32_t entry=0;entry<count;++entry) {
            if(!value(depth+1,true))return false;
            lua_pushvalue(vm,-1);lua_rawget(vm,table);
            const bool duplicate=!lua_isnil(vm,-1);lua_pop(vm,1);if(duplicate)return false;
            if(!value(depth+1) || lua_isnil(vm,-1))return false;
            lua_rawset(vm,table);
        }
        return true;
    }
};

template<class T> bool write(FILE* file,const T& value){return fwrite(&value,sizeof(value),1,file)==1;}
template<class T> bool read(FILE* file,T& value){return fread(&value,sizeof(value),1,file)==1;}
bool writeText(FILE* file,const std::string& text) {
    return text.size()<=MAX_SOURCES && write(file,(uint32_t)text.size()) && fwrite(text.data(),1,text.size(),file)==text.size();
}
bool readText(FILE* file,std::string& text,size_t limit) {
    uint32_t size=0;if(!read(file,size) || size>limit)return false;
    text.resize(size);return fread(text.data(),1,size,file)==size;
}
bool validName(const std::string& name) {
    return name.size()>4 && name.size()<=240 && name.ends_with(".lua") && name.find_first_of("/\\\0",0,3)==std::string::npos;
}
}

bool ScriptSnapshot::write(FILE* file) const {
    if(sources.size()>64 || state.size()>MAX_STATE || (active && !randomState) || (!active && (randomState || !sources.empty() || !state.empty())))return false;
    if(fwrite("RA2LUA1",1,8,file)!=8 || !::write(file,(uint8_t)active) || !::write(file,randomState) || !::write(file,(uint32_t)sources.size()))return false;
    size_t total=0;
    for(const auto& source:sources) {
        total+=source.text.size();
        if(total>MAX_SOURCES || !validName(source.name) || !writeText(file,source.name) || !writeText(file,source.text))return false;
    }
    return ::write(file,(uint32_t)state.size()) && fwrite(state.data(),1,state.size(),file)==state.size();
}

bool ScriptSnapshot::read(FILE* file) {
    ScriptSnapshot parsed;char magic[8]{};uint8_t enabled=0;uint32_t count=0;
    if(fread(magic,1,8,file)!=8 || memcmp(magic,"RA2LUA1",8) || !::read(file,enabled) || enabled>1
        || !::read(file,parsed.randomState) || (enabled && !parsed.randomState) || !::read(file,count) || count>64)return false;
    parsed.active=enabled!=0;size_t remaining=MAX_SOURCES;std::string previous;
    for(uint32_t index=0;index<count;++index) {
        ScriptSource source;
        if(!readText(file,source.name,240) || !validName(source.name) || (!previous.empty() && source.name<=previous)
            || !readText(file,source.text,std::min(remaining,(size_t)4*1024*1024)))return false;
        remaining-=source.text.size();previous=source.name;parsed.sources.push_back(std::move(source));
    }
    uint32_t length=0;if(!::read(file,length) || length>MAX_STATE)return false;
    parsed.state.resize(length);if(fread(parsed.state.data(),1,length,file)!=length)return false;
    if((!parsed.active && (parsed.randomState || !parsed.sources.empty() || length)) || (parsed.sources.empty()!=parsed.state.empty()))return false;
    *this=std::move(parsed);return true;
}

bool ScriptEngine::captureSnapshot(ScriptSnapshot& snapshot) {
    lastError.clear();ScriptSnapshot result;result.active=enabled();
    if(!result.active){snapshot=std::move(result);return true;}
    result.randomState=random.s;
    if(!sourcesLoaded){lastError="A script failed to initialize; its state cannot be saved.";return false;}
    result.sources=sources;
    if(sources.empty()){snapshot=std::move(result);return true;}
    auto* vm=static_cast<lua_State*>(L);StackGuard stack(vm);
    const bool previous=readOnlyApi;readOnlyApi=true;
    const bool ok=[&]() {
        lua_getglobal(vm,"OnLoad");const bool loadHook=lua_isfunction(vm,-1);lua_pop(vm,1);
        lua_getglobal(vm,"OnSave");
        if(!loadHook || !lua_isfunction(vm,-1)){lastError="Enabled scripts must define both OnSave and OnLoad before saving.";return false;}
        if(lua_pcall(vm,0,1,0)!=LUA_OK){const char* message=lua_tostring(vm,-1);lastError=message?message:"OnSave failed";return false;}
        Encoder encode{vm,0,0,{}};
        if(!lua_istable(vm,-1) || !encode.value(result.state,-1)) {
            lastError="OnSave must return bounded plain data: tables, strings, finite numbers and booleans.";return false;
        }
        return true;
    }();
    readOnlyApi=previous;
    if(ok)snapshot=std::move(result);
    return ok;
}

bool ScriptEngine::prepareRestore(World* target,int mode,int mission,const ScriptSnapshot& snapshot) {
    shutdown();
    if(!snapshot.active)return !snapshot.randomState && snapshot.sources.empty() && snapshot.state.empty();
    if(!snapshot.randomState){lastError="Invalid saved Lua random state.";return false;}
    if(!open(target,mode,mission,true)){lastError="Installed scripts failed to initialize during restore.";return false;}
    if(sources!=snapshot.sources){lastError="Installed Lua sources differ from those used by this save.";return false;}
    random=Rng(snapshot.randomState);
    if(sources.empty())return snapshot.state.empty();
    auto* vm=static_cast<lua_State*>(L);StackGuard stack(vm);
    lua_getglobal(vm,"OnSave");const bool saveHook=lua_isfunction(vm,-1);lua_pop(vm,1);
    lua_getglobal(vm,"OnLoad");
    if(!saveHook || !lua_isfunction(vm,-1)){lastError="Saved scripts do not provide OnSave/OnLoad.";return false;}
    Decoder decode{vm,snapshot.state,0,0,{vm,{}}};
    if(snapshot.state.size()>MAX_STATE || !decode.value() || decode.offset!=snapshot.state.size() || !lua_istable(vm,-1)) {
        lastError="Invalid Lua saved-state data.";return false;
    }
    if(lua_pcall(vm,1,0,0)!=LUA_OK){const char* message=lua_tostring(vm,-1);lastError=message?message:"OnLoad failed";return false;}
    return true;
}
