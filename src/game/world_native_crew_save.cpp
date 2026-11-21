#include "game/world.h"
#include "core/binary_state_io.h"
#include <set>
#include <limits>

namespace {
constexpr uint32_t header=0x57455243,footer=0x444e4543,version=1,fixedBytes=33,maxIdentities=1048576;
template<class W,class F>bool identities(W& world,F visit) {
    uint32_t count=0;
    const auto passenger=[&](auto&& self,auto& p,int depth)->bool {
        if(depth>3 || ++count>maxIdentities)return false;
        visit(p.combatActor);
        for(auto& child:p.cargo)if(!self(self,child,depth+1))return false;
        return true;
    };
    for(auto& actor:world.ents) {
        if(++count>maxIdentities)return false;
        visit(actor.combatActor);
        for(auto& p:actor.cargo)if(!passenger(passenger,p,0))return false;
        for(auto& p:actor.garrison)if(!passenger(passenger,p,0))return false;
    }
    return true;
}
}

bool World::validateNativeCrewState() const {
    if(nativeCrewRules.enabled && (!nextCombatActor || nextCombatActor>(uint64_t)std::numeric_limits<int64_t>::max()))return false;
    bool ok=true;std::set<uint64_t> seen;
    const auto identity=[&](uint64_t value) {
        if(value && (!nativeCrewRules.enabled || value>=nextCombatActor || !seen.insert(value).second))ok=false;
    };
    if(!identities(*this,identity))return false;
    if(nativeCrewRules.enabled)for(const auto& actor:ents)if(actor.alive && !actor.combatActor)ok=false;
    for(const auto& projectile:projs)if(projectile.sourceActor && (!nativeCrewRules.enabled || projectile.sourceActor>=nextCombatActor))ok=false;
    if(!nativeCrewRules.enabled && !nativeCrewBursts.empty())return false;
    if(nativeCrewBursts.size()>maxIdentities)return false;
    for(const auto& [identity,burst]:nativeCrewBursts) {
        if(!identity || identity>=nextCombatActor || !valid(burst.carrier) || !valid(burst.target)
            || !nativeCrewFires(ents[burst.carrier.id]) || !findCrewPassenger(burst.carrier.id,identity)
            || burst.remaining<1 || burst.remaining>63 || burst.delay<0 || burst.delay>1800
            || burst.player<0 || burst.player>=numPlayers || burst.player!=ents[burst.carrier.id].player)return false;
    }
    return ok;
}

bool World::writeNativeCrewState(FILE* file) const {
    if(!file || !validateNativeCrewState())return false;
    BinaryStateStream s{file};const uint8_t enabled=nativeCrewRules.enabled;
    uint32_t count=0;if(enabled && !identities(*this,[&](uint64_t){++count;}))return false;
    const uint32_t shots=enabled?(uint32_t)projs.size():0,bursts=(uint32_t)nativeCrewBursts.size(),bytes=fixedBytes+8*(count+shots)+45*bursts;
    const uint64_t next=enabled?nextCombatActor:0;
    s.write(header);s.write(version);s.write(enabled);s.write(next);s.write(count);s.write(shots);
    if(enabled) {
        identities(*this,[&](uint64_t value){s.write(value);});
        for(const auto& projectile:projs)s.write(projectile.sourceActor);
    }
    s.write(bursts);
    for(const auto& [identity,b]:nativeCrewBursts) {
        s.write(identity);s.write(b.carrier.id);s.write(b.carrier.generation);s.write(b.target.id);s.write(b.target.generation);
        s.write(b.remaining);s.write(b.delay);s.write(b.player);const uint8_t air=b.air;s.write(air);
    }
    s.write(footer);s.write(bytes);return s.ok;
}

bool World::readNativeCrewState(FILE* file) {
    if(!file)return false;
    BinaryStateStream s{file};uint32_t mark=0,v=0,count=0,shots=0,end=0,bytes=0;uint8_t enabled=0;uint64_t next=0;
    s.read(mark);s.read(v);s.read(enabled);s.read(next);s.read(count);s.read(shots);
    if(!s.ok || mark!=header || v!=version || enabled>1 || (enabled!=0)!=nativeCrewRules.enabled
        || count>maxIdentities || shots>8192 || (!enabled && (count || shots || next)))return false;
    if(enabled) {
        uint32_t expected=0;if(!identities(*this,[&](uint64_t){++expected;}) || count!=expected || shots!=projs.size())return false;
        nextCombatActor=next;identities(*this,[&](uint64_t& value){s.read(value);});
        for(auto& projectile:projs)s.read(projectile.sourceActor);
    }
    uint32_t bursts=0;s.read(bursts);if(!s.ok || bursts>count || (!enabled && bursts))return false;
    nativeCrewBursts.clear();uint64_t previous=0;
    for(uint32_t i=0;i<bursts;++i) {
        uint64_t identity=0;NativeCrewBurst b;uint8_t air=0;
        s.read(identity);s.read(b.carrier.id);s.read(b.carrier.generation);s.read(b.target.id);s.read(b.target.generation);
        s.read(b.remaining);s.read(b.delay);s.read(b.player);s.read(air);
        if(!s.ok || identity<=previous || air>1)return false;previous=identity;b.air=air!=0;
        nativeCrewBursts.emplace(identity,b);
    }
    s.read(end);s.read(bytes);
    return s.ok && end==footer && bytes==fixedBytes+8*(count+shots)+45*bursts && validateNativeCrewState();
}

uint32_t World::nativeCrewChecksum() const {
    uint32_t hash=2166136261u;
    const auto number=[&](uint64_t value,int bytes){for(int i=0;i<bytes;++i){hash^=(uint8_t)(value>>(i*8));hash*=16777619u;}};
    number(header,4);number(version,4);number(nextCombatActor,8);
    identities(*this,[&](uint64_t value){number(value,8);});
    for(const auto& projectile:projs)number(projectile.sourceActor,8);
    number(nativeCrewBursts.size(),4);
    for(const auto& [identity,b]:nativeCrewBursts) {
        number(identity,8);number((uint32_t)b.carrier.id,4);number(b.carrier.generation,8);
        number((uint32_t)b.target.id,4);number(b.target.generation,8);
        number(b.remaining,4);number(b.delay,4);number((uint32_t)b.player,4);number(b.air,1);
    }
    return hash;
}
