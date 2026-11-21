#include "legacy_v7_fixture.h"
#include "game/campaign_save.h"
#include "game/script_snapshot.h"
#include <string_view>
#include <algorithm>
#include <cstring>

static std::vector<uint8_t> previousFixture(const std::vector<uint8_t>& input,FILE* fog,size_t cells,int version) {
    auto current=input;
    if(current.size()<4)return {};
    bool removedCrew=false;
    uint32_t crewBytes=0;memcpy(&crewBytes,current.data()+current.size()-4,4);
    if(crewBytes>=33 && crewBytes+4<=current.size() && !memcmp(current.data()+current.size()-4-crewBytes,"CREW",4)) {
        const auto offset=current.size()-4-crewBytes;
        if(crewBytes!=33 || current[offset+8]!=0 || memcmp(current.data()+offset+29,"CEND",4))return {};
        for(size_t i=9;i<29;++i)if(current[offset+i])return {};
        current.resize(offset);removedCrew=true;if(current.size()<4)return {};
    }
    bool removedTemporal=false;
    uint32_t temporalBytes=0;memcpy(&temporalBytes,current.data()+current.size()-4,4);
    if(temporalBytes>=17 && temporalBytes+4<=current.size()
        && !memcmp(current.data()+current.size()-4-temporalBytes,"TEMP",4)) {
        const auto offset=current.size()-4-temporalBytes;uint32_t count=0;memcpy(&count,current.data()+offset+9,4);
        // A synthetic historical fixture may remove only the closed, empty
        // contract. Never discard Source35 semantics or active collaboration.
        if(temporalBytes!=17 || current[offset+8]!=0 || count || memcmp(current.data()+offset+13,"TEND",4))return {};
        current.resize(offset);removedTemporal=true;if(current.size()<4)return {};
    }
    uint32_t orderBytes=0;memcpy(&orderBytes,current.data()+current.size()-4,4);
    if(orderBytes>=13 && orderBytes+4<=current.size()
        && !memcmp(current.data()+current.size()-4-orderBytes,"CELO",4)) {
        // Synthetic legacy fixtures never discard active Scene33 object order.
        if(orderBytes!=13 || current[current.size()-4-orderBytes+8]!=0)return {};
        current.resize(current.size()-4-orderBytes);
    }
    uint32_t deliveryBytes=0;memcpy(&deliveryBytes,current.data()+current.size()-4,4);
    if(deliveryBytes>=17 && deliveryBytes+4<=current.size()
        && !memcmp(current.data()+current.size()-4-deliveryBytes,"DLVR",4)) {
        // These historical synthetic fixtures contain no active support mission.
        // Never silently discard a physical aircraft or falling passenger.
        if(deliveryBytes!=17)return {};
        current.resize(current.size()-4-deliveryBytes);
    }
    uint32_t supportBytes=0;memcpy(&supportBytes,current.data()+current.size()-4,4);
    if(supportBytes>=14 && supportBytes<=14+8*20 && supportBytes+4<=current.size()
        && !memcmp(current.data()+current.size()-4-supportBytes,"PPUS",4))
        current.resize(current.size()-4-supportBytes); // v56 independent support and veteran behavior flags
    if(current.size()<12)return {};uint32_t crateBytes=0;memcpy(&crateBytes,current.data()+current.size()-4,4);
    if(crateBytes<8 || crateBytes>current.size()-4)return {};current.resize(current.size()-4-crateBytes); // v55 crate lifetimes
    if(current.size()<12)return {};uint32_t navigationBytes=0;memcpy(&navigationBytes,current.data()+current.size()-4,4);
    if(navigationBytes<8 || navigationBytes>current.size()-4)return {};current.resize(current.size()-4-navigationBytes); // v54 air orders
    if(current.size()<13)return {};uint32_t jumpjetBytes=0;memcpy(&jumpjetBytes,current.data()+current.size()-4,4);
    if(jumpjetBytes<9 || jumpjetBytes>current.size()-4)return {};current.resize(current.size()-4-jumpjetBytes); // v53 Jumpjet state
    if(current.size()<13)return {};uint32_t flightBytes=0;memcpy(&flightBytes,current.data()+current.size()-4,4);
    if(flightBytes<9 || flightBytes>current.size()-4)return {};current.resize(current.size()-4-flightBytes); // v52 horizontal flight
    if(current.size()<9)return {};uint32_t aswBytes=0;memcpy(&aswBytes,current.data()+current.size()-4,4);
    if(aswBytes!=5)return {};current.resize(current.size()-9); // v51 ASW behavior flag
    if(current.size()<8)return {};uint32_t aircraftBytes=0;memcpy(&aircraftBytes,current.data()+current.size()-4,4);
    if(aircraftBytes<5 || aircraftBytes>current.size()-4)return {};current.resize(current.size()-4-aircraftBytes); // v50 aircraft slots
    if(current.size()<8)return {};uint32_t identityBytes=0;memcpy(&identityBytes,current.data()+current.size()-4,4);
    if(identityBytes<4 || identityBytes>current.size()-4)return {};current.resize(current.size()-4-identityBytes); // v49 slot generations
    if(current.size()<8)return {};uint32_t movementBytes=0;memcpy(&movementBytes,current.data()+current.size()-4,4);
    if(movementBytes<4 || movementBytes>current.size()-4)return {};current.resize(current.size()-4-movementBytes); // v48 movement extension
    if(current.size()<8)return {};uint32_t attackBytes=0;memcpy(&attackBytes,current.data()+current.size()-4,4);
    if(attackBytes<4 || attackBytes>current.size()-4)return {};current.resize(current.size()-4-attackBytes); // v47 order extension
    if(current.empty() || current.back()!=0)return {};current.pop_back(); // v46 inactive condition extension
    if(version<24){if(current.empty() || current.back()!=0)return {};current.pop_back();} // v45 inactive native-crate extension
    if(version<23){if(current.empty() || current.back()!=0)return {};current.pop_back();} // v44 inactive native-AI extension
    if(version<22){if(current.empty() || current.back()!=0)return {};current.pop_back();} // v43 production extension
    if(current.size()<8 || (memcmp(current.data(),"RA2GAM10",8) && memcmp(current.data(),"RA2GAM11",8)) || !fog || fseek(fog,0,SEEK_END))return {};
    const long length=ftell(fog);if(length<0 || (size_t)length<12+cells*10 || fseek(fog,0,SEEK_SET))return {};
    std::vector<uint8_t> fogBytes((size_t)length);
    if(fread(fogBytes.data(),1,fogBytes.size(),fog)!=fogBytes.size())return {};
    auto found=std::search(current.begin()+8,current.end(),fogBytes.begin(),fogBytes.end());
    if(found==current.end())return {};
    if(!memcmp(current.data(),"RA2GAM11",8)){
        const size_t offset=(size_t)(found-current.begin());uint32_t bytes=0;
        if(offset<12)return {};memcpy(&bytes,current.data()+offset-4,4);
        if(bytes<5 || bytes>1024 || bytes>offset-12)return {};
        current.erase(current.begin()+offset-4-bytes,current.begin()+offset);memcpy(current.data(),"RA2GAM10",8);
        found=std::search(current.begin()+8,current.end(),fogBytes.begin(),fogBytes.end());if(found==current.end())return {};
    }
    const size_t fogOffset=(size_t)(found-current.begin()),worldOffset=fogOffset+fogBytes.size();
    if(worldOffset+8<=current.size() && !memcmp(current.data()+worldOffset,"RA2WLD60",8) && (!removedCrew || !removedTemporal))return {};
    if(worldOffset+8<=current.size() && !memcmp(current.data()+worldOffset,"RA2WLD59",8) && !removedTemporal)return {};
    constexpr size_t worldHeader=43,oldCell=8,newCell=9,oldFogCell=9,newFogCell=10;
    if(worldOffset+worldHeader+cells*newCell+1>current.size()
        || (memcmp(current.data()+worldOffset,"RA2WLD55",8) && memcmp(current.data()+worldOffset,"RA2WLD56",8) && memcmp(current.data()+worldOffset,"RA2WLD57",8) && memcmp(current.data()+worldOffset,"RA2WLD58",8) && memcmp(current.data()+worldOffset,"RA2WLD59",8) && memcmp(current.data()+worldOffset,"RA2WLD60",8)) || current.back()!=0 || current[current.size()-2]!=0
        || current[worldOffset+worldHeader+cells*newCell]!=0)return {};
    std::vector<uint8_t> result(current.begin(),current.begin()+fogOffset+12);
    memcpy(result.data(),version==7?"RA2GAME7":version==8?"RA2GAME8":version==9?"RA2GAME9":"RA2GAM10",8);
    for(size_t i=0;i<cells;++i) {
        const auto start=current.begin()+fogOffset+12+i*newFogCell;
        result.insert(result.end(),start,start+(version==7?oldFogCell:newFogCell));
    }
    const size_t buildings=fogOffset+12+cells*newFogCell;
    uint32_t buildingCount=0;if(buildings+4>worldOffset)return {};memcpy(&buildingCount,current.data()+buildings,4);
    if(buildings+4+(size_t)buildingCount*36!=worldOffset)return {};
    result.insert(result.end(),current.begin()+buildings,current.begin()+buildings+4);
    for(uint32_t i=0;i<buildingCount;++i)result.insert(result.end(),current.begin()+buildings+4+i*36,current.begin()+buildings+4+i*36+(version>=10?36:version==9?32:28));
    const size_t oldWorldOffset=result.size();
    if(version>=11){result.insert(result.end(),current.begin()+worldOffset,current.end());
        if(version==24)memcpy(result.data()+oldWorldOffset,"RA2WLD45",8);
        else if(version==23)memcpy(result.data()+oldWorldOffset,"RA2WLD44",8);
        else if(version==22)memcpy(result.data()+oldWorldOffset,"RA2WLD43",8);
        else if(version==21)memcpy(result.data()+oldWorldOffset,"RA2WLD42",8);
        else if(version==20)memcpy(result.data()+oldWorldOffset,"RA2WLD41",8);
        else if(version==19)memcpy(result.data()+oldWorldOffset,"RA2WLD40",8);
        else if(version==18)memcpy(result.data()+oldWorldOffset,"RA2WLD39",8);
        else if(version==17)memcpy(result.data()+oldWorldOffset,"RA2WLD38",8);
        else if(version==16)memcpy(result.data()+oldWorldOffset,"RA2WLD37",8);
        else if(version==15)memcpy(result.data()+oldWorldOffset,"RA2WLD36",8);
        else{memcpy(result.data()+oldWorldOffset,"RA2WRLD",7);result[oldWorldOffset+7]=version==11?'W':version==12?'X':version==13?'Y':'Z';}return result;}
    if(version>=9) {result.insert(result.end(),current.begin()+worldOffset,current.end()-1);memcpy(result.data()+oldWorldOffset,"RA2WRLDV",8);return result;}
    if(version==8) {
        result.insert(result.end(),current.begin()+worldOffset,current.end()-2);memcpy(result.data()+oldWorldOffset,"RA2WRLDU",8);return result;
    }
    result.insert(result.end(),current.begin()+worldOffset,current.begin()+worldOffset+worldHeader);
    memcpy(result.data()+oldWorldOffset,"RA2WRLDT",8);
    for(size_t i=0;i<cells;++i) {
        const auto start=current.begin()+worldOffset+worldHeader+i*newCell;
        result.insert(result.end(),start,start+oldCell);
    }
    result.insert(result.end(),current.begin()+worldOffset+worldHeader+cells*newCell+1,current.end()-2);
    return result;
}
std::vector<uint8_t> makeLegacyV7Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,7);}
std::vector<uint8_t> makeLegacyV8Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,8);}
std::vector<uint8_t> makeLegacyV9Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,9);}
std::vector<uint8_t> makeLegacyV10Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,10);}
std::vector<uint8_t> makeLegacyWorld32Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,11);}
std::vector<uint8_t> makeLegacyWorld33Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,12);}
std::vector<uint8_t> makeLegacyWorld34Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,13);}
std::vector<uint8_t> makeLegacyWorld35Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,14);}
std::vector<uint8_t> makeLegacyWorld36Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,15);}
std::vector<uint8_t> makeLegacyWorld37Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,16);}
std::vector<uint8_t> makeLegacyWorld38Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,17);}
std::vector<uint8_t> makeLegacyWorld39Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,18);}
std::vector<uint8_t> makeLegacyWorld40Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,19);}
std::vector<uint8_t> makeLegacyWorld41Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,20);}
std::vector<uint8_t> makeLegacyWorld42Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,21);}
std::vector<uint8_t> makeLegacyWorld43Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,22);}
std::vector<uint8_t> makeLegacyWorld44Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,23);}

std::vector<uint8_t> makeLegacyWorld45Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {return previousFixture(current,fog,cells,24);}

std::vector<uint8_t> makeLegacyV6Fixture(const std::vector<uint8_t>& current,FILE* fog,size_t cells) {
    auto result=makeLegacyV7Fixture(current,fog,cells);
    const std::string_view tag="RA2CAMP1";
    const auto found=std::search(result.begin(),result.end(),tag.begin(),tag.end());
    if(found==result.end())return {};
    const size_t start=(size_t)(found-result.begin());
    FILE* file=std::tmpfile();if(!file)return {};
    CampaignSnapshot campaign;ScriptSnapshot script;
    const bool valid=std::fwrite(result.data(),1,result.size(),file)==result.size()
        && std::fseek(file,(long)start,SEEK_SET)==0
        && readCampaignSnapshot(file,nullptr,campaign) && script.read(file)
        && campaign==CampaignSnapshot{} && script==ScriptSnapshot{};
    const long end=std::ftell(file);std::fclose(file);
    // A fixture must never drop live campaign or script state.
    if(!valid || end<(long)start || (size_t)end>result.size())return {};
    result.erase(result.begin()+start,result.begin()+end);
    std::memcpy(result.data(),"RA2GAME6",8);return result;
}
