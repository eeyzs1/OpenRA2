#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

// Shared primitives only: each format owns its field order and limits.
struct BinaryStateStream {
    FILE* file;
    bool ok=true;
    explicit BinaryStateStream(FILE* stream):file(stream){}

    template<class T> void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if(ok && std::fwrite(&value,sizeof(value),1,file)!=1)ok=false;
    }
    template<class T> void read(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if(ok && std::fread(&value,sizeof(value),1,file)!=1)ok=false;
    }
    void writeBytes(const void* bytes,size_t count) {
        if(ok && count && std::fwrite(bytes,1,count,file)!=count)ok=false;
    }
    void readBytes(void* bytes,size_t count) {
        if(ok && count && std::fread(bytes,1,count,file)!=count)ok=false;
    }
    void writeText(const std::string& text) {
        const uint32_t count=(uint32_t)text.size();write(count);writeBytes(text.data(),count);
    }
    void readText(std::string& text,size_t limit=1u<<20) {
        uint32_t count=0;read(count);
        if(!ok || count>limit){ok=false;return;}
        text.resize(count);readBytes(text.data(),count);
    }
};

// Mode adapter for formats whose read/write traversal is deliberately shared.
// Explicit construction keeps the mode separate from the stream's error state.
struct BinaryStateIO : BinaryStateStream {
    bool reading;
    explicit BinaryStateIO(FILE* stream,bool load=false):BinaryStateStream(stream),reading(load){}

    template<class T> void value(T& v) {
        if(reading)read(v);else write(v);
    }
    void flag(bool& v) {
        uint8_t byte=v;value(byte);
        if(byte>1)ok=false;
        v=byte!=0;
    }
    template<class T> bool vector(T& items,uint32_t limit) {
        uint32_t count=(uint32_t)items.size();value(count);
        if(!ok || count>limit){ok=false;return false;}
        if(reading)items.resize(count);
        return true;
    }
    void text(std::string& s,uint32_t limit=128) {
        uint32_t count=(uint32_t)s.size();value(count);
        if(!ok || count>limit){ok=false;return;}
        if(reading){s.resize(count);readBytes(s.data(),count);}
        else writeBytes(s.data(),count);
    }
};
