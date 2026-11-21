#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float pi=3.14159265358979323846f,levelLeptons=104.f;
int ticks(int original){return original*LOGIC_FPS/15;}
float facing(int direction){return ((direction-1)&7)*pi/4;}
float turn(float now,float goal,float step) {
    float difference=std::remainder(goal-now,2*pi);return std::remainder(now+std::clamp(difference,-step,step),2*pi);
}
float floorHeight(const World& w,float x,float y) {
    return w.map.inBounds((int)x,(int)y)?w.map.surfaceHeight((int)x,(int)y,0)*levelLeptons:0;
}
}

void World::resetNativeRockets() {
    nativeRocketDefinitions.clear();nativeLaunchers.clear();nativeRockets.clear();
    for(auto& e:ents)e.nativeAltitude=e.previousNativeAltitude=e.nativePitch=0;
}
int World::nativeLauncherDefinition(const Ent& actor) const {
    if(actor.isBuilding)return -1;
    for(int i=0;i<(int)nativeRocketDefinitions.size();++i) {
        const auto& d=nativeRocketDefinitions[i];
        if(actor.nativeIdentity.definition?actor.nativeIdentity.type==d.launcher:
            actor.utype==nativeTypes[d.launcher]->unit.type)return i;
    }
    return -1;
}
void World::initializeNativeLauncher(EID actor) {
    if(!valid(actor))return;const int definition=nativeLauncherDefinition(ents[actor]);if(definition<0)return;
    for(const auto& state:nativeLaunchers)if(state.actor==actor)return;
    NativeLauncherState state;state.actor=actor;state.definition=definition;state.nodes.resize(nativeRocketDefinitions[definition].capacity);
    nativeLaunchers.push_back(std::move(state));
}
int World::nativeDockedSpawns(EID actor) const {
    for(const auto& state:nativeLaunchers)if(state.actor==actor) {
        int count=0;for(const auto& node:state.nodes)if(node.missile<0 && node.readyAt<=tick)++count;return count;
    }
    return 0;
}

bool World::launchNativeRocket(EID actor,EID target) {
    if(!nativeRocketTargets(actor,target) || ents[actor].immobilized())return false;
    initializeNativeLauncher(actor);int index=-1;for(int i=0;i<(int)nativeLaunchers.size();++i)if(nativeLaunchers[i].actor==actor){index=i;break;}
    if(index<0)return false;auto& state=nativeLaunchers[index];const auto& d=nativeRocketDefinitions[state.definition];
    const auto& t=ents[target];float tx=t.x,ty=t.y;if(t.isBuilding){tx+=bldDef(t).w/2.f;ty+=bldDef(t).h/2.f;}
    const float distance=distf(ents[actor].x,ents[actor].y,tx,ty);
    if(distance<d.minimumRange || distance>d.range || t.airborne() || (t.player>=0 && !isEnemy(ents[actor].player,t.player)))return false;
    state.target=target;if(tick<state.nextLaunch)return false;
    for(const auto& node:state.nodes)if(valid(node.missile))return false; // one take-off at a time
    int slot=-1;for(int i=0;i<(int)state.nodes.size();++i)if(state.nodes[i].readyAt<=tick){slot=i;break;}
    if(slot<0)return false;
    const auto& owner=ents[actor];const float heading=facing(owner.dir),lateral=d.flh[1]*(slot&1?-1.f:1.f);
    const float x=owner.x+(d.flh[0]*std::cos(heading)-lateral*std::sin(heading))/256;
    const float y=owner.y+(d.flh[0]*std::sin(heading)+lateral*std::cos(heading))/256;
    const EID missile=spawnNativeActor(d.missile,owner.player,x,y,256,0,"rocket:"+std::to_string(actor)+":"+std::to_string(slot)+":"+std::to_string(tick),"","Guard");
    if(!valid(missile))return false;
    NativeRocketState rocket;rocket.actor=missile;rocket.definition=state.definition;rocket.source=actor;rocket.slot=slot;
    if(d.pause==0)rocket.phase=NativeRocketPhase::Tilt;
    rocket.altitude=actorSurfaceHeight(owner)*levelLeptons+d.flh[2];rocket.pitch=d.pitchInitial*pi/2;rocket.heading=heading;
    rocket.destinationX=tx;rocket.destinationY=ty;rocket.destinationZ=floorHeight(*this,tx,ty);rocket.elite=owner.vetRank>=2;
    nativeRockets.push_back(rocket);state.nodes[slot].missile=missile;state.nextLaunch=tick+ticks(d.launchDelay);
    auto& air=ents[missile];air.dir=owner.dir;air.nativeAltitude=air.previousNativeAltitude=rocket.altitude;air.nativePitch=rocket.pitch;
    if(owner.utype==UnitType::Boomer)ents[actor].subReveal=90;
    air.target=target;airOccSync(missile);return true;
}

bool World::nativeRocketTargets(EID actor,EID target) const {
    if(!valid(actor) || !valid(target))return false;
    const auto& owner=ents[actor];const int index=nativeLauncherDefinition(owner);if(index<0)return false;
    if(owner.nativeIdentity.definition && owner.nativeIdentity.definition->guns.direct()) {
        const auto weapon=effWeapon(owner,&ents[target]);
        return weapon.nativeType==owner.nativeIdentity.type && weapon.nativeSlot>=0
            && weapon.nativeSlot%2==nativeRocketDefinitions[index].weaponSlot;
    }
    return true;
}

void World::updateNativeLaunchers() {
    std::erase_if(nativeRockets,[](const auto& r){return r.actor<0;});
    std::erase_if(nativeLaunchers,[](const auto& l){return l.actor<0;});
    for(EID actor=0;actor<(EID)ents.size();++actor)if(valid(actor) && nativeLauncherDefinition(ents[actor])>=0)initializeNativeLauncher(actor);
    const int count=(int)nativeLaunchers.size();
    for(int i=0;i<count;++i) {
        const EID actor=nativeLaunchers[i].actor;if(!valid(actor))continue;
        const auto& e=ents[actor];
        if(e.state!=UState::Attacking && e.state!=UState::Chasing){nativeLaunchers[i].target=-1;continue;}
        if(nativeRocketTargets(actor,e.target))launchNativeRocket(actor,e.target);else nativeLaunchers[i].target=-1;
    }
}

void World::updateNativeRocket(EID actor) {
    auto found=std::find_if(nativeRockets.begin(),nativeRockets.end(),[&](const auto& r){return r.actor==actor;});
    if(found==nativeRockets.end())return;auto& r=*found;auto& air=ents[actor];const auto& d=nativeRocketDefinitions[r.definition];
    constexpr float dt=15.f/LOGIC_FPS;air.previousNativeAltitude=air.nativeAltitude;
    if(air.immobilized())return;
    if(r.phase!=NativeRocketPhase::Flight) {
        if(!valid(r.source)){kill(actor,false);return;}
        const auto& owner=ents[r.source];const float yaw=facing(owner.dir),side=d.flh[1]*(r.slot&1?-1.f:1.f);
        air.x=owner.x+(d.flh[0]*std::cos(yaw)-side*std::sin(yaw))/256;
        air.y=owner.y+(d.flh[0]*std::sin(yaw)+side*std::cos(yaw))/256;
        r.heading=yaw;r.altitude=actorSurfaceHeight(owner)*levelLeptons+d.flh[2];air.dir=owner.dir;
        if(r.phase==NativeRocketPhase::Pause) {
            if(++r.phaseTicks>=ticks(d.pause)){r.phase=NativeRocketPhase::Tilt;r.phaseTicks=0;}
        } else {
            ++r.phaseTicks;const float fraction=d.tilt?std::min(1.f,(float)r.phaseTicks/ticks(d.tilt)):1;
            r.pitch=(d.pitchInitial+(d.pitchFinal-d.pitchInitial)*fraction)*pi/2;
            if(r.phaseTicks>=ticks(d.tilt)) {
                r.phase=NativeRocketPhase::Flight;r.phaseTicks=0;
                for(auto& launcher:nativeLaunchers)if(launcher.actor==r.source && r.slot>=0) {
                    auto& node=launcher.nodes[r.slot];node.missile=-1;node.readyAt=tick+ticks(d.regenerate);
                    launcher.nextLaunch=std::max(launcher.nextLaunch,tick+(uint64_t)ticks(d.launchDelay));
                }
                r.slot=-1;emitSoundAt(Sfx::Missile,air.x,air.y,actor);
            }
        }
    } else {
        const auto impactAt=[&](float x,float y) {
            WeaponDef impact;impact.damage=r.elite?d.eliteDamage:d.damage;impact.cellSpread=r.elite?d.elite.spread:d.normal.spread;
            impact.percentAtMax=r.elite?d.elite.percentAtMax:d.normal.percentAtMax;impact.nativeVerses=(r.elite?d.elite.verses:d.normal.verses).data();
            impact.antiAir=false;const int owner=air.player;const EID source=valid(r.source) && ents[r.source].player==owner?r.source:INVALID_EID;
            kill(actor,false);applyCellSpreadDamage(impact,owner,x,y,source);explodeAt(x,y,1);
        };
        const auto contactsTerrain=[&]() {
            if(!r.diving || r.pitch>0)return false;
            const float ground=floorHeight(*this,air.x,air.y);
            // Resolve float roundoff at the terrain plane while preserving
            // the final movement of accurate shots toward their aim point.
            const float tolerance=4*std::numeric_limits<float>::epsilon()*std::max(1.f,std::abs(ground));
            return r.altitude<=ground+tolerance;
        };
        if(contactsTerrain()){impactAt(air.x,air.y);return;}
        // Speed is the original type percentage on the 0..255 lepton scale.
        const float maximum=(float)std::min(255,(int)(d.maxSpeed*256/100));
        r.speed=std::min(maximum,r.speed+d.acceleration*dt);
        const float dx=(r.destinationX-air.x)*256,dy=(r.destinationY-air.y)*256;
        const float horizontal=std::sqrt(dx*dx+dy*dy),dz=r.destinationZ-r.altitude;
        const float cruise=r.destinationZ+d.altitude;
        // LazyCurve uses an arch rather than a level cruise segment. Begin
        // turning down early on short shots so the missile cannot climb past
        // its captured ground destination. This is a guided-arc approximation;
        // original executable pitch/height quantization is not yet calibrated.
        if(r.altitude>=cruise || (d.lazyCurve && horizontal<=std::max(256.f,(r.altitude-r.destinationZ)*1.5f)))r.cruised=true;
        if(d.lazyCurve && r.cruised)r.diving=true;
        if(r.cruised && horizontal<=std::max(256.f,(r.altitude-r.destinationZ)*1.5f))r.diving=true;
        const float desiredPitch=!r.cruised?d.pitchFinal*pi/2:r.diving?std::atan2(dz,horizontal):0;
        r.pitch=turn(r.pitch,desiredPitch,d.turnRate*pi/2*dt);
        r.heading=turn(r.heading,std::atan2(dy,dx),d.yawRate*2*pi/256*dt);
        const float step=r.speed*dt;
        if(std::sqrt(horizontal*horizontal+dz*dz)<=std::max(1.f,step)) {
            impactAt(r.destinationX,r.destinationY);return;
        }
        air.x+=std::cos(r.heading)*std::cos(r.pitch)*step/256;
        air.y+=std::sin(r.heading)*std::cos(r.pitch)*step/256;r.altitude+=std::sin(r.pitch)*step;
        if(contactsTerrain()){impactAt(air.x,air.y);return;}
        air.dir=dirFromVec(std::cos(r.heading),std::sin(r.heading));++r.phaseTicks;
    }
    air.nativeAltitude=r.altitude;air.nativePitch=r.pitch;air.target=INVALID_EID;
}

void World::retireNativeRocketActor(EID actor) {
    std::vector<EID> attached;
    for(auto& r:nativeRockets) {
        if(r.actor==actor) {
            for(auto& launcher:nativeLaunchers)if(launcher.actor==r.source && r.slot>=0) {
                auto& node=launcher.nodes[r.slot];if(node.missile==actor){node.missile=-1;node.readyAt=tick+ticks(nativeRocketDefinitions[r.definition].regenerate);}
            }
            r.actor=-1;
        }
        if(r.source==actor){r.source=-1;if(r.actor>=0 && r.phase!=NativeRocketPhase::Flight)attached.push_back(r.actor);}
    }
    for(auto& launcher:nativeLaunchers) {
        if(launcher.actor==actor){launcher.actor=-1;launcher.target=-1;for(auto& node:launcher.nodes)node.missile=-1;}
        if(launcher.target==actor)launcher.target=-1;
    }
    for(EID missile:attached)if(valid(missile))kill(missile,false);
    std::erase_if(nativeRockets,[](const auto& r){return r.actor<0;});
    std::erase_if(nativeLaunchers,[](const auto& l){return l.actor<0;});
}
