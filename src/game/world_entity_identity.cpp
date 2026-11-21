#include "game/world.h"
#include <limits>

World::EntityMutationScope::EntityMutationScope(World& owner):world(owner) {++world.entityMutationDepth;}
World::EntityMutationScope::~EntityMutationScope() {
    if(--world.entityMutationDepth==0 && !world.updateInProgress && world.areaDamageDepth==0) {
        world.freeList.insert(world.freeList.end(),world.retiredDuringUpdate.begin(),world.retiredDuringUpdate.end());
        world.retiredDuringUpdate.clear();
    }
}

bool World::commandTargetsEntity(const Cmd& c) {
    switch(c.type) {
    case Cmd::Attack:case Cmd::Capture:case Cmd::Repair:case Cmd::Board:
    case Cmd::Garrison:case Cmd::Service:case Cmd::ReturnHarvester:return true;
    default:return false;
    }
}

void World::bindCommand(Cmd& c) const {
    if(c.identitiesBound)return;
    c.generations.clear();c.generations.reserve(c.ids.size());
    for(EID id:c.ids)c.generations.push_back(entityRef(id).generation);
    c.targetGeneration=commandTargetsEntity(c)?entityRef(c.a).generation:0;
    c.identitiesBound=true;
}

bool World::writeEntityGenerations(FILE* file) const {
    if(!file || ents.size()>(std::numeric_limits<uint32_t>::max()-4)/8)return false;
    const uint32_t count=(uint32_t)ents.size(),bytes=4+count*8;
    if(fwrite(&count,4,1,file)!=1)return false;
    for(const auto& entity:ents) {
        if((entity.alive && !entity.generation) || entity.generation>MAX_ENTITY_GENERATION || fwrite(&entity.generation,8,1,file)!=1)return false;
    }
    return fwrite(&bytes,4,1,file)==1;
}

bool World::readEntityGenerations(FILE* file) {
    uint32_t count=0,bytes=0;
    if(!file || fread(&count,4,1,file)!=1 || count!=ents.size()
        || count>(std::numeric_limits<uint32_t>::max()-4)/8)return false;
    for(auto& entity:ents) {
        if(fread(&entity.generation,8,1,file)!=1 || (entity.alive && !entity.generation) || entity.generation>MAX_ENTITY_GENERATION)return false;
    }
    return fread(&bytes,4,1,file)==1 && bytes==4+count*8;
}
