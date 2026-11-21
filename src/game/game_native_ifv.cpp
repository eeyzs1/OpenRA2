#include "game/game.h"
#include "gfx/sprites.h"

const Sprite& Game::nativeIFVSprite(const World::Ent& actor,bool turret,int frame) const {
    const auto& definition=*actor.nativeIdentity.definition;
    const auto directory=definition.rulesOnly?"assets/rules/profiles/"+world.rulesProfileId:"assets/maps/native/"+world.map.native.assetId;
    const int color=actor.player>=0 && actor.player<world.numPlayers?world.players[actor.player].colorId:0;
    return g_sprites.ifvPart(directory+"/ifv/"+definition.ifv.image,definition.ifv.turretCount,
        turret?world.nativeIFVTurret(actor):-1,turret?actor.turretDir:actor.dir,frame,color,HOUSE_COLORS[color]);
}
