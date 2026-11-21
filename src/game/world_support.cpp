#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>

namespace {
std::vector<Vec2i> landingCells(const World& world,int x,int y,int count) {
    std::vector<Vec2i> cells;
    for(int radius=0;radius<=3 && (int)cells.size()<count;++radius)
        for(int dy=-radius;dy<=radius && (int)cells.size()<count;++dy)
            for(int dx=-radius;dx<=radius && (int)cells.size()<count;++dx) {
                if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;
                const int tx=x+dx,ty=y+dy;
                if(world.map.passable(tx,ty) && world.map.at(tx,ty).terrain!=Terrain::Water
                    && !world.bldBlocked(tx,ty) && world.unitAtCell(tx,ty)==INVALID_EID)cells.push_back({tx,ty});
            }
    return cells;
}
const char* supportName(SupportType type) {
    return TR(type==SupportType::AmericanParadrop?S::AmericanParadrop:type==SupportType::Paradrop?S::Paradrop:
        type==SupportType::SpyPlane?S::SpyPlane:S::PsychicReveal);
}
}

const SupportRule& World::supportRule(SupportType type) const {
    static const SupportRule empty;
    if((unsigned)type>=SUPPORT_COUNT)return empty;
    if(nativeProductionRules.version>=4)return nativeProductionRules.support[(int)type];
    static const auto fusion=[] {
        std::array<SupportRule,SUPPORT_COUNT> rules;
        for(auto& rule:rules){rule.available=true;rule.rechargeTicks=PARADROP_TIME;}
        rules[0].infantry={"E1","E2","INIT"};rules[0].count={6,9,6};
        rules[1].infantry={"E1","E1","E1"};rules[1].count={8,8,8};return rules;
    }();
    return fusion[(int)type];
}

bool World::hasSupportSource(int player,SupportType type) const {
    if(player<0 || player>=numPlayers || (unsigned)type>=SUPPORT_COUNT)return false;
    if(!independentSupportEnabled) {
        if(type==SupportType::Paradrop)return hasParadropSource(player);
        if(type==SupportType::SpyPlane)return hasSpyPlaneSource(player);
        if(type==SupportType::PsychicReveal)return hasPsychicRevealSource(player);
        return false;
    }
    if(!players[player].active || players[player].defeated || !supportRule(type).available)return false;
    if(nativeProductionRules.available)return hasNativeSupportSource(player,1u<<(int)type);
    for(const auto& building:ents) {
        if(!building.alive || !building.isBuilding || building.player!=player || building.selling || building.constructAnim>0)continue;
        if(type==SupportType::Paradrop && building.btype==BldType::TechAirport)return true;
        if(type==SupportType::AmericanParadrop && building.btype==BldType::AirForceCmd && countryHasParadrop(players[player].country))return true;
        if(type==SupportType::SpyPlane && building.btype==BldType::Radar
            && (players[player].faction==Faction::Soviet || players[player].faction==Faction::China))return true;
        if(type==SupportType::PsychicReveal && building.btype==BldType::PsychicSensor && players[player].faction==Faction::Yuri)return true;
    }
    return false;
}

SupportType World::preferredParadrop(int player) const {
    if(independentSupportEnabled && hasSupportSource(player,SupportType::AmericanParadrop)
        && (supportReady(player,SupportType::AmericanParadrop) || !supportReady(player,SupportType::Paradrop)))return SupportType::AmericanParadrop;
    return hasSupportSource(player,SupportType::Paradrop)?SupportType::Paradrop:SupportType::COUNT;
}

int World::supportCharge(int player,SupportType type) const {
    if(player<0 || player>=numPlayers || (unsigned)type>=SUPPORT_COUNT)return 0;
    return independentSupportEnabled?players[player].support[(int)type].ticks:players[player].paradropCharge;
}

bool World::supportReady(int player,SupportType type) const {
    if(!hasSupportSource(player,type))return false;
    if(!nativeTemporalAbilityOnline(player,1u<<(int)type,true))return false;
    if(!independentSupportEnabled)return players[player].paradropReady;
    return players[player].support[(int)type].ready && !(supportRule(type).powered && players[player].lowPower());
}

void World::syncSupportSources() {
    if(!independentSupportEnabled)return;
    for(int player=0;player<numPlayers;++player)for(int i=0;i<SUPPORT_COUNT;++i)
        if(!hasSupportSource(player,(SupportType)i))players[player].support[i]={};
}

void World::updateSupportPowers() {
    syncSupportSources();
    for(int player=0;player<numPlayers;++player)for(int i=0;i<SUPPORT_COUNT;++i) {
        const auto type=(SupportType)i;auto& charge=players[player].support[i];const auto& rule=supportRule(type);
        if(!hasSupportSource(player,type) || !nativeTemporalAbilityOnline(player,1u<<i,true) || charge.ready || (rule.powered && players[player].lowPower()))continue;
        if(++charge.ticks>=rule.rechargeTicks) {
            charge.ticks=rule.rechargeTicks;charge.ready=true;
            eva(player,formatText(TR(S::EvaSWReadyFmt),supportName(type)));
            if(player==0)emitSound(Sfx::Ready,.7f);
        }
    }
}

SuperweaponRejectReason World::supportRejection(int player,SupportType type,float x,float y) const {
    if((unsigned)type>=SUPPORT_COUNT)return SuperweaponRejectReason::InvalidType;
    if(!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=map.w || y>=map.h)return SuperweaponRejectReason::InvalidTarget;
    if(!hasSupportSource(player,type) || !nativeTemporalAbilityOnline(player,1u<<(int)type,true) || (independentSupportEnabled && supportRule(type).powered && players[player].lowPower()))
        return SuperweaponRejectReason::Unavailable;
    if(!supportReady(player,type))return SuperweaponRejectReason::NotReady;
    if(type==SupportType::Paradrop || type==SupportType::AmericanParadrop) {
        if(supportDeliveryEnabled) {
            const int cx=(int)x,cy=(int)y;
            if(map.at(cx,cy).terrain==Terrain::Water && !map.hasDeck(cx,cy))return SuperweaponRejectReason::Blocked;
            bool landing=false;int layer=0;
            for(int r=0;r<=3 && !landing;++r)for(int dy=-r;dy<=r && !landing;++dy)for(int dx=-r;dx<=r && !landing;++dx)
                if(std::max(std::abs(dx),std::abs(dy))==r)landing=supportLandingCell(cx+dx,cy+dy,layer);
            if(!landing)return SuperweaponRejectReason::Blocked;
        } else if(map.at((int)x,(int)y).terrain==Terrain::Water || landingCells(*this,(int)x,(int)y,1).empty())return SuperweaponRejectReason::Blocked;
    }
    return SuperweaponRejectReason::None;
}

bool World::orderSupport(int player,SupportType type,float x,float y) {
    if(supportRejection(player,type,x,y)!=SuperweaponRejectReason::None)return false;
    if(!independentSupportEnabled) {
        if(type==SupportType::Paradrop)orderParadrop(player,x,y);
        else if(type==SupportType::SpyPlane)orderSpyPlane(player,x,y);
        else orderPsychicReveal(player,x,y);
        return !players[player].paradropReady;
    }
    if(supportDeliveryEnabled && type!=SupportType::PsychicReveal) {
        if(!launchSupportMission(player,type,x,y))return false;
        players[player].support[(int)type]={};return true;
    }
    if(type==SupportType::Paradrop || type==SupportType::AmericanParadrop) {
        const auto& rule=supportRule(type);const auto faction=players[player].faction;
        const int side=faction==Faction::Soviet?1:faction==Faction::Yuri?2:0;
        int nativeType=-1;UnitType unit=UnitType::GI;
        if(nativeProductionRules.available) {
            const auto found=std::find_if(nativeTypes.begin(),nativeTypes.end(),[&](const auto& d){return !d->isBuilding && d->id==rule.infantry[side];});
            if(found==nativeTypes.end())return false;nativeType=(int)(found-nativeTypes.begin());unit=(*found)->unit.type;
        } else if(type==SupportType::Paradrop)
            unit=faction==Faction::Soviet?UnitType::Conscript:faction==Faction::Yuri?UnitType::Initiate:faction==Faction::China?UnitType::PLA:UnitType::GI;
        int dropped=0;
        for(const auto cell:landingCells(*this,(int)x,(int)y,rule.count[side])) {
            // Spawn callbacks may occupy a later landing cell in this volley.
            if(!map.passable(cell.x,cell.y) || map.at(cell.x,cell.y).terrain==Terrain::Water
                || bldBlocked(cell.x,cell.y) || unitAtCell(cell.x,cell.y)!=INVALID_EID)continue;
            if(!valid(spawnUnit(player,unit,cell.x+.5f,cell.y+.5f,nativeType)))continue;
            ++dropped;Effect effect;effect.kind=9;effect.x=cell.x+.5f;effect.y=cell.y+.5f;effect.maxAge=18;effects.push_back(effect);
        }
        if(!dropped)return false;
        map.reveal(player,(int)x,(int)y,6);eva(player,TR(S::EvaParadropDrop));emitSoundAt(Sfx::Place,x,y);
    } else {
        if(type==SupportType::SpyPlane) {
            for(int dx=-24;dx<=24;++dx)if(map.inBounds((int)x+dx,(int)y))map.reveal(player,(int)x+dx,(int)y,2);
        } else map.reveal(player,(int)x,(int)y,10);
        emitSoundAt(type==SupportType::SpyPlane?Sfx::Missile:Sfx::Tesla,x,y);
        Effect effect;effect.kind=8;effect.x=x;effect.y=y;effect.maxAge=type==SupportType::SpyPlane?30:35;effects.push_back(effect);
    }
    players[player].support[(int)type]={};return true;
}
