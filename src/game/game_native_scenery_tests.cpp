#include "game_regression.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::nativeSceneryChecks(const char* outputDir) {
    int failures=0;
    auto check=[&](bool ok,const char* name){std::printf("NATIVE_SCENERY %s: %s\n",ok?"PASS":"FAIL",name);failures+=!ok;};
    World original=world;original.map.bldOccRef=&original.bldOcc;
    EID tree=INVALID_EID,drill=INVALID_EID,wall=INVALID_EID,neighbor=INVALID_EID;
    Vec2i treeEntrance{-1,-1};
    for(EID id=0;id<(EID)world.ents.size();++id) {
        const auto& e=world.ents[id];const auto& d=*e.nativeIdentity.definition;
        if(d.id=="TIBTRE02")drill=id;
        if(d.kind==1 && d.id.starts_with("TREE") && tree==INVALID_EID && world.map.at((int)e.x,(int)e.y).passable()) {
            int blockers=0;
            for(const auto& other:world.ents)if(other.alive && other.isBuilding && other.nativeIdentity.definition->blocksMovement) {
                const auto& footprint=bldDef(other);
                blockers+=e.x>=other.x && e.y>=other.y && e.x<other.x+footprint.w && e.y<other.y+footprint.h;
            }
            if(blockers==1)for(auto delta:{Vec2i{1,0},Vec2i{0,1},Vec2i{-1,0},Vec2i{0,-1}}) {
                const int x=(int)e.x+delta.x,y=(int)e.y+delta.y;
                if(world.passableFor(x,y,0) && !world.bldBlocked(x,y) && world.map.climbOk(x,y,(int)e.x,(int)e.y,0)) {
                    tree=id;treeEntrance={x,y};break;
                }
            }
        }
        if(d.wall && wall==INVALID_EID) {
            const int x=(int)e.x,y=(int)e.y;
            for(auto delta:{Vec2i{1,0},Vec2i{0,1}}) {
                const auto other=world.bldAt(x+delta.x,y+delta.y);
                if(world.valid(other) && world.ents[other].nativeIdentity.definition->wall) {wall=id;neighbor=other;break;}
            }
        }
    }
    check(world.valid(tree) && world.valid(drill) && world.valid(wall) && world.valid(neighbor),
          "the real source map supplies a tree, immune ore drill and connected sandbags");
    if(!world.valid(tree) || !world.valid(drill) || !world.valid(wall) || !world.valid(neighbor))return failures;
    {
        World mechanics=world;mechanics.map.bldOccRef=&mechanics.bldOcc;
        const auto placed=mechanics.ents[tree];const int x=(int)placed.x,y=(int)placed.y;
        check(placed.hp==200 && placed.player==-1 && mechanics.bldBlocked(x,y),
              "the source tree has TreeStrength=200 and blocks its actual terrain cell");
        std::vector<Vec2i> path;
        const bool enteredBefore=mechanics.map.findPath(treeEntrance.x,treeEntrance.y,x,y,path)
            && !path.empty() && path.back()==Vec2i{x,y};
        const int immuneHp=mechanics.ents[drill].hp;mechanics.damage(drill,100000,0);
        check(mechanics.valid(drill) && mechanics.ents[drill].hp==immuneHp,"the source Immune flag prevents combat damage to the ore drill");
        WeaponDef impact=unitDef(UnitType::Grizzly).weapon;impact.warhead=WeaponDef::Warhead::HE;
        impact.damage=200;impact.cellSpread=0;impact.percentAtMax=1;
        mechanics.applyCellSpreadDamage(impact,0,x+.5f,y+.5f,INVALID_EID);
        path.clear();const bool enteredAfter=mechanics.map.findPath(treeEntrance.x,treeEntrance.y,x,y,path)
            && !path.empty() && path.back()==Vec2i{x,y};
        check(!enteredBefore && !mechanics.valid(tree) && mechanics.passableFor(x,y,0) && !mechanics.bldBlocked(x,y) && enteredAfter,
              "real cell-spread damage destroys the tree and opens the actual A* destination");
        check(std::none_of(mechanics.players.begin(),mechanics.players.end(),[](const Player& p){return p.defeated;}),
              "native mission houses retain their state instead of receiving skirmish elimination on scenery destruction");
        const auto before=mechanics.ents[neighbor];const int nx=(int)before.x,ny=(int)before.y;
        const auto frame=mechanics.map.native.tiles[mechanics.cellIdx(nx,ny)].overlayData;
        const auto victim=mechanics.ents[wall];const int wx=(int)victim.x,wy=(int)victim.y;
        mechanics.damage(wall,10000,0);
        check(!mechanics.valid(wall) && mechanics.map.native.tiles[mechanics.cellIdx(wx,wy)].overlay==255
            && !mechanics.map.at(wx,wy).blocked && mechanics.map.native.tiles[mechanics.cellIdx(nx,ny)].overlayData!=frame,
              "sandbag destruction clears the native blocker and reconnects the surviving neighbor");
        FILE* file=tmpfile();World restored;bool resume=file && mechanics.saveGame(file);
        if(resume){rewind(file);resume=restored.loadGame(file);}if(file)fclose(file);
        for(int tick=0;tick<120 && resume;++tick){mechanics.update();restored.update();resume=mechanics.checksum()==restored.checksum();}
        check(resume,"destroyed native terrain, changed wall frames and immunity resume identically for 120 ticks");
    }
    {
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();
        const auto other=world.ents[neighbor];const int key=world.cellIdx((int)other.x,(int)other.y);
        const int observed=nativeActorFrame(other);
        std::fill(world.map.fog[localPlayer].begin(),world.map.fog[localPlayer].end(),FOG_SEEN);
        world.damage(wall,10000,0);syncFogKnowledge();
        const int current=nativeActorFrame(world.ents[neighbor]);
        check(current!=observed && fogKnowledge.buildings.contains(key) && fogKnowledge.buildings.at(key).nativeFrame==observed,
              "a hidden wall reconnection preserves the viewer's last observed original frame");
        const auto save=(std::filesystem::path(outputDir)/"native-scenery-hidden.sav").string();
        const auto state=world.checksum();
        check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && world.checksum()==state
            && fogKnowledge.buildings.contains(key) && fogKnowledge.buildings.at(key).nativeFrame==observed,
              "application v10 restores frozen native scenery frames independently of the live map");
        const auto& spec=*world.ents[neighbor].nativeIdentity.definition;
        const auto& sprite=nativeActorSprite(world.ents[neighbor],observed);
        Image actual=sprite.tex.id?LoadImageFromTexture(sprite.tex):Image{};
        const auto filename=contentPathFmt("assets/maps/native/%s/actors/%s_%d.png",world.map.native.assetId.c_str(),spec.art.c_str(),observed);
        Image source=LoadImage(filename);bool pixels=actual.data && source.data && actual.width==source.width && actual.height==source.height;int visible=0;
        if(pixels)for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x) {
            const auto expected=GetImageColor(source,x,y);if(expected.a!=255)continue;
            const auto received=GetImageColor(actual,x,y);pixels &= ColorToInt(expected)==ColorToInt(received);++visible;
        }
        if(actual.data)UnloadImage(actual);if(source.data)UnloadImage(source);
        check(pixels && visible>20,"the GPU frozen-wall sprite uses the observed source frame rather than the hidden current frame");
    }
    world=std::move(original);world.map.bldOccRef=&world.bldOcc;fogKnowledge={};
    return failures;
}
