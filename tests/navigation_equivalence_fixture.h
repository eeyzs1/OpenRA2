#pragma once
#include "game/map.h"
#include <array>

namespace NavigationEquivalence {
// Frozen geometry generator. Answers are recorded from the pre-optimization
// layered implementation, rather than calculated from the new fast path.
inline uint64_t sample(int scenario) {
    Map map;map.w=map.h=7;map.cells.resize(49);map.gateCells.resize(49);
    std::vector<int> occupancy(49);map.bldOccRef=&occupancy;
    Rng random(0x739ab65dull+(uint64_t)scenario*977);
    if(scenario>=32)map.bridgeCells.resize(49);
    for(size_t i=0;i<map.cells.size();++i) {
        auto& cell=map.cells[i];cell.terrain=(Terrain)random.range(0,5);cell.overlay=(Overlay)random.range(0,5);
        cell.height=(uint8_t)random.range(0,5);cell.present=random.range(0,15)!=0;cell.blocked=random.range(0,15)==0;
        occupancy[i]=random.range(0,7)==0?1:0;
        if(random.range(0,7)==0)map.gateCells[i]={(int)i,random.range(0,2),random.range(0,1)!=0};
        if(scenario>=32 && random.range(0,3)==0)map.bridgeCells[i]={(int)i,(uint8_t)(cell.height+4),(uint8_t)random.range(0,1)};
    }
    uint64_t hash=14695981039346656037ull;
    for(int ay=-1;ay<=map.h;++ay)for(int ax=-1;ax<=map.w;++ax)
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(int domain=0;domain<3;++domain)
            for(int layer=0;layer<2;++layer)for(bool corners:{false,true})for(bool under:{false,true})for(uint8_t access:{0,1,3,255}) {
                const int result=map.stepLayer(ax,ay,layer,ax+dx,ay+dy,domain,corners,under,access);
                hash=(hash^(uint8_t)(result+1))*1099511628211ull;
            }
    return hash;
}
inline constexpr std::array<uint64_t,64> baseline={
    0x796e2001ae03ef8dull,0xf565ed7094d282f5ull,0x63bb9288896a5badull,0x7a2093f0924cbf1dull,
    0x5dae765431f71f15ull,0x5416e9c4e657705dull,0x99cdad10ae8ccfddull,0x0042261f77ae5595ull,
    0x6df28f11c097a54dull,0x27d86f5daef3d505ull,0xe7f991c74f1c5c2dull,0x7b077d1795628005ull,
    0x0e654b8e825c506dull,0x55f0683d53176275ull,0x392bcd59833b22b5ull,0x811adf68598bd96dull,
    0xc985535d0233341dull,0x9742863be4d8a8adull,0x751c647977a6bc65ull,0x8b8255cc90b03ffdull,
    0x83d96bdd0e07b7fdull,0x0cc77f87fb9c272dull,0xde86c64b62ca640dull,0x800b6c770b9e4dedull,
    0xc5367b47c63f2405ull,0x40a048edbdd1956dull,0x68ad7f7cae8c753dull,0xcc7f999942367b2dull,
    0xf0a706890e0afa35ull,0xcf9e0cee9f2d6a75ull,0xa677d2af90076cedull,0xf356f1df401630b5ull,
    0xe4749dde355920adull,0x48ab4a08f64e1941ull,0x3db26c433685e919ull,0x4c7f7ea81a74d441ull,
    0x18c38c610f7b5921ull,0x255dc6ff477f91e1ull,0x9646b4f19f6dc971ull,0x7fd1ca4af6b24ffdull,
    0xb2932f0dd12ee1a5ull,0x1e125e377e2ee97dull,0x8cfc1e3599ad5a5dull,0x03dc3d37307ce815ull,
    0x61dd568c50a75745ull,0x9a61914f3db2b911ull,0x2703c67882fee2e1ull,0xdf03c24cbf839449ull,
    0xdaee8b456ddcc97dull,0x7e24bcb92048d705ull,0x7723a9dd6fbbd5d9ull,0xec1bb09c9d1cb6f9ull,
    0x406f326c3d60c489ull,0xe654e01866565a79ull,0xbb94bc5c80e1deedull,0x857eb56edbfa7715ull,
    0x06c83defae040d05ull,0xc3f7785be6b2cdd1ull,0xcadab6afddf6c599ull,0xd717b860b5a7ade5ull,
    0x42295ae07b440549ull,0xcc3713faee16648dull,0x5787901334fc0119ull,0xa435d5f9b3bcb995ull,
};
}
