#include "game/world.h"
#include "core/binary_state_io.h"
#include <set>

namespace {
constexpr uint32_t header=0x504d4554,footer=0x444e4554,legacyVersion=1;
constexpr uint32_t fixedBytes=17,nodeBytes=40; // excludes the final byte-count word
}

bool World::validateNativeTemporalState() const {
    if(!nativeTemporalEnabled && !nativeTemporalBeams.empty())return false;
    if(nativeTemporalBeams.size()>1048576 || (!nativeCrewRules.enabled && nativeTemporalBeams.size()>ents.size()))return false;
    std::set<int64_t> visited;
    for(EID target=0;target<(EID)ents.size();++target) {
        const auto& actor=ents[target];const int64_t head=actor.nativeTemporalHead;
        if(head==INVALID_EID)continue;
        if(!nativeTemporalEnabled || !actor.alive || !actor.nativeIdentity.definition || !actor.nativeIdentity.definition->warpable)return false;
        int64_t cursor=head,next=INVALID_EID;
        while(cursor!=INVALID_EID) {
            auto found=nativeTemporalBeams.find(cursor);
            if(found==nativeTemporalBeams.end() || !visited.insert(cursor).second)return false;
            const auto& beam=found->second;
            if(!valid(beam.owner) || !valid(beam.target) || beam.target!=entityRef(target)
                || beam.owner.id==target || ents[beam.owner.id].nativeTemporalHead!=INVALID_EID || beam.next!=next)return false;
            if(beam.passenger) {
                if(!nativeCrewRules.enabled || beam.passenger>=(uint64_t)INT64_MAX || cursor!=nativeCrewTemporalKey(beam.passenger)
                    || !nativeCrewFires(ents[beam.owner.id]) || !findCrewPassenger(beam.owner.id,beam.passenger) || nativeTemporalDamage(cursor)<=0)return false;
            } else if(cursor!=beam.owner.id || (nativeCrewRules.enabled && nativeTemporalDamage(cursor)<=0))return false;
            if(cursor==head) {
                const int strength=actor.isBuilding?bldDef(actor).hp:unitDef(actor).hp;
                if(beam.remaining<=0 || beam.remaining>(int64_t)strength*10)return false;
            } else if(beam.remaining!=0)return false;
            next=cursor;cursor=beam.prev;
        }
    }
    return visited.size()==nativeTemporalBeams.size();
}

bool World::writeNativeTemporalState(FILE* file) const {
    if(!file || !validateNativeTemporalState())return false;
    BinaryStateStream s{file};const uint8_t enabled=nativeTemporalEnabled;
    const uint32_t v=nativeCrewRules.enabled?2:legacyVersion,recordBytes=v==2?56:nodeBytes;
    const uint32_t count=(uint32_t)nativeTemporalBeams.size(),bytes=fixedBytes+count*recordBytes;
    s.write(header);s.write(v);s.write(enabled);s.write(count);
    for(const auto& [source,beam]:nativeTemporalBeams) {
        s.write(beam.owner.id);s.write(beam.owner.generation);if(v==2)s.write(beam.passenger);
        s.write(beam.target.id);s.write(beam.target.generation);
        if(v==2){s.write(beam.next);s.write(beam.prev);}else{s.write((EID)beam.next);s.write((EID)beam.prev);}
        s.write(beam.remaining);
    }
    s.write(footer);s.write(bytes);return s.ok;
}

bool World::readNativeTemporalState(FILE* file) {
    if(!file)return false;
    BinaryStateStream s{file};uint32_t mark=0,v=0,count=0,end=0,bytes=0;uint8_t enabled=0;
    s.read(mark);s.read(v);s.read(enabled);s.read(count);
    if(!s.ok || mark!=header || v!=(nativeCrewRules.enabled?2:legacyVersion) || enabled>1 || (enabled!=0)!=nativeTemporalEnabled
        || (count>1048576 || (!nativeCrewRules.enabled && count>ents.size())) || (!enabled && count))return false;
    nativeTemporalBeams.clear();for(auto& actor:ents)actor.nativeTemporalHead=INVALID_EID;
    int64_t previous=INT64_MIN;
    for(uint32_t i=0;i<count;++i) {
        NativeTemporalBeam beam;s.read(beam.owner.id);s.read(beam.owner.generation);if(v==2)s.read(beam.passenger);
        s.read(beam.target.id);s.read(beam.target.generation);
        if(v==2){s.read(beam.next);s.read(beam.prev);}else{EID next=INVALID_EID,prev=INVALID_EID;s.read(next);s.read(prev);beam.next=next;beam.prev=prev;}
        s.read(beam.remaining);
        if(!s.ok || beam.passenger>=(uint64_t)INT64_MAX || !valid(beam.owner) || !valid(beam.target))return false;
        const int64_t key=beam.passenger?nativeCrewTemporalKey(beam.passenger):beam.owner.id;
        if(key<=previous)return false;previous=key;
        if(beam.next==INVALID_EID) {
            auto& actor=ents[beam.target.id];
            if(actor.nativeTemporalHead!=INVALID_EID)return false;
            actor.nativeTemporalHead=key;
        }
        if(!nativeTemporalBeams.emplace(key,beam).second)return false;
    }
    s.read(end);s.read(bytes);
    return s.ok && end==footer && bytes==fixedBytes+count*(v==2?56:nodeBytes) && (v==2 || validateNativeTemporalState());
}

uint32_t World::nativeTemporalChecksum() const {
    uint32_t hash=2166136261u;
    const auto number=[&](uint64_t value,int bytes) {for(int i=0;i<bytes;++i){hash^=(uint8_t)(value>>(i*8));hash*=16777619u;}};
    const uint32_t v=nativeCrewRules.enabled?2:legacyVersion;
    number(header,4);number(v,4);number(nativeTemporalEnabled,1);number(nativeTemporalBeams.size(),4);
    for(const auto& [source,beam]:nativeTemporalBeams) {
        number((uint32_t)beam.owner.id,4);number(beam.owner.generation,8);if(v==2)number(beam.passenger,8);
        number((uint32_t)beam.target.id,4);number(beam.target.generation,8);
        number(v==2?(uint64_t)beam.next:(uint32_t)beam.next,v==2?8:4);
        number(v==2?(uint64_t)beam.prev:(uint32_t)beam.prev,v==2?8:4);number((uint64_t)beam.remaining,8);
    }
    return hash;
}
