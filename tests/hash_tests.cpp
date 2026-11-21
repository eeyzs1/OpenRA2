#include "core/sha256.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    int failed=0;
    const auto check=[&](std::string_view input,const char* expected){Sha256 one;one.update(input);
        bool ok=digestHex(one.digest())==expected;
        for(size_t chunk:{1,7,64,103}){Sha256 stream;for(size_t offset=0;offset<input.size();offset+=chunk)
            stream.update(input.data()+offset,std::min(chunk,input.size()-offset));ok &= stream.digest()==one.digest();}
        std::printf("SHA256 %s bytes=%zu\n",ok?"PASS":"FAIL",input.size());failed+=!ok;};
    // NIST SHS vectors, plus independent Python hashlib padding-boundary answers.
    check("","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    check(std::string(1000000,'a'),"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const struct {size_t size;const char* expected;} vectors[]={
        {55,"80101b9d1281ccb3b9fb63729c2f80dab4ae7a56b55b954e9cc6451bf2825b52"},
        {56,"0814299f4494565303090505b77ee5863c6672a94e4999fbbff8ec396d58bf55"},
        {63,"4bc2da88441f15a6ed90d3dacd20cc601703a419c7568a7dc7bf59d88c9a21fb"},
        {64,"281202b0dcadec296df6e31b394a7604a0eaad74f1dd7bc127c944ba18c2fc83"},
        {65,"f0a83190bd50aa46bb2740275e374e918b0ea3d8330fb8679bd88f1af8d761e7"},
        {127,"26f60b9c6afc0d5a02c6abc9b17883b9b876979d9ef08fa9611c1dc4c6a15b3a"},
        {128,"9a3cb848c8219e3d94ef5075dfedcbade55bcc5b9e016a3a7e2dc9d893befd92"},
        {129,"9d10ceb3d0a8264cf9531f5462b258e898412d1268b89f0d9322bbeab2e1fcd6"}};
    for(auto v:vectors){std::string input(v.size,'\0');for(size_t i=0;i<input.size();++i)input[i]=(char)((i*37+17)%256);check(input,v.expected);}
    Sha256 continued;continued.update("a");const auto snapshot=continued.digest();continued.update("bc");
    const bool continuedOk=snapshot!=continued.digest() && digestHex(continued.digest())=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    std::printf("SHA256 %s snapshot then continue\n",continuedOk?"PASS":"FAIL");return failed+!continuedOk;
}
