#include "core/sha256.h"
#include <algorithm>
#include <bit>
#include <cstring>

void Sha256::block(const uint8_t* data) {
    static constexpr uint32_t k[]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64]{};
    for(int i=0;i<16;++i)for(int j=0;j<4;++j)w[i]=(w[i]<<8)|data[i*4+j];
    for(int i=16;i<64;++i){const auto a=w[i-15],b=w[i-2];
        w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10));}
    auto [a,b,c,d,e,f,g,h]=state;
    for(int i=0;i<64;++i){
        const uint32_t t1=h+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        const uint32_t t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    const uint32_t v[]={a,b,c,d,e,f,g,h};for(int i=0;i<8;++i)state[i]+=v[i];
}

void Sha256::update(const void* data,size_t size) {
    auto* p=static_cast<const uint8_t*>(data);bytes+=(uint64_t)size;
    while(size){const size_t n=std::min(size,buffer.size()-used);std::memcpy(buffer.data()+used,p,n);
        used+=n;p+=n;size-=n;if(used==buffer.size()){block(buffer.data());used=0;}}
}

Sha256Digest Sha256::digest() const {
    Sha256 copy=*this;const uint64_t bits=bytes*8;uint8_t pad[128]{};pad[0]=0x80;
    const size_t count=used<56?56-used:120-used;copy.update(pad,count);
    uint8_t length[8]{};for(int i=0;i<8;++i)length[i]=(uint8_t)(bits>>(56-i*8));copy.update(length,8);
    Sha256Digest out{};for(int i=0;i<8;++i)for(int j=0;j<4;++j)out[i*4+j]=(uint8_t)(copy.state[i]>>(24-j*8));return out;
}

std::string digestHex(const Sha256Digest& value) {
    static constexpr char hex[]="0123456789abcdef";std::string text;text.reserve(64);
    for(uint8_t b:value){text+=hex[b>>4];text+=hex[b&15];}return text;
}
