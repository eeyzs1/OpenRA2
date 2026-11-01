#include "game/game.h"
#include <cstdio>
#include <filesystem>

int Game::magneticLandingRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;
    for(int scenario=0;scenario<11;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Yuri,Faction::Allies},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        for(auto& player:w.players) { player.defeated=false;player.money=0; }
        w.cratesEnabled=false;
        const EID actor=w.spawnUnit(0,UnitType::Magnetron,20.5f,30.5f);
        if(scenario==8 || scenario==9) for(int x=21;x<=28;++x) w.map.at(x,30).terrain=Terrain::Water;
        const EID victim=w.spawnUnit(1,scenario==8 || scenario==9?UnitType::Aegis:UnitType::MCV,26.5f,30.5f);
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={actor};attack.a=victim;w.applyCmd(0,attack);
        for(int i=0;i<180 && w.ents[victim].magneticHeight==0;++i) w.update();
        bool lifted=w.ents[victim].magneticHeight>0 && w.ents[victim].occCell<0 && w.ents[victim].airBucket>=0;
        const auto gun=w.effWeapon(w.ents[actor]);
        bool targetable=w.findNearestEnemy(0,20.5f,30.5f,10,false,&gun,UnitType::Magnetron,actor)==victim;
        for(int i=0;i<20;++i) { w.update();lifted &= w.valid(victim) && w.ents[victim].occCell<0; }
        if(scenario==0) {
            world=w;world.map.bldOccRef=&world.bldOcc;phase=Phase::InGame;localPlayer=1;sim.active=false;
            interpAlpha=1;camZoom=1;sel={victim};
            camX=(world.ents[victim].x-world.ents[victim].y)*32-400;
            camY=(world.ents[victim].x+world.ents[victim].y)*16-300;
            bakeTerrain();minimapTimer=0;shotAtDisplaySize=true;
            const auto hash=world.checksum();const int liftHeight=world.ents[victim].magneticHeight;
            const auto raised=unitScreenRect(world.ents[victim]);
            world.ents[victim].magneticHeight=0;const auto ground=unitScreenRect(world.ents[victim]);
            world.ents[victim].magneticHeight=liftHeight;
            bool visual=ground.y-raised.y==liftHeight*3 && pickUnit((int)(raised.x+raised.width/2),(int)(raised.y+raised.height/2))==victim;
            for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                SetWindowSize(size.x,size.y);render();
                const auto path=(std::filesystem::path(outputDir)/("lifted_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
                shotFile=path;render();Image frame=LoadImage(path.c_str());
                visual &= frame.width==size.x && frame.height==size.y;UnloadImage(frame);
            }
            visual &= world.checksum()==hash;failures+=!visual;
            std::printf("MAGNETIC_VISUAL %s height=%d rect_shift=%.1f pick=%d simulation_unchanged=%d\n",visual?"PASS":"FAIL",liftHeight,ground.y-raised.y,
                pickUnit((int)(raised.x+raised.width/2),(int)(raised.y+raised.height/2))==victim,world.checksum()==hash);
            shotAtDisplaySize=false;
        }
        const int x=(int)w.ents[victim].x,y=(int)w.ents[victim].y;
        if(scenario==1 || scenario==10) w.spawnBuilding(0,BldType::PowerPlant,x,y,true);
        if(scenario==2) w.map.at(x,y).terrain=Terrain::Water;
        if(scenario==3) w.spawnUnit(0,UnitType::MCV,x+0.5f,y+0.5f);
        if(scenario==4) w.map.at(x,y).overlay=Overlay::Rock1;
        if(scenario==9) w.map.at(x,y).terrain=Terrain::Clear;
        EID parasite=INVALID_EID;
        if(scenario==10) {
            parasite=w.spawnUnit(0,UnitType::TerrorDrone,w.ents[victim].x,w.ents[victim].y);
            w.ents[parasite].parasiting=true;w.ents[parasite].parasiteHost=victim;w.ents[victim].parasite=parasite;w.unitOccSync(parasite);
        }
        const int hp=w.ents[victim].hp,height=w.ents[victim].magneticHeight;
        if(scenario==5) w.kill(actor,false);
        else if(scenario==6) w.ents[actor].chrono=40;
        else if(scenario==7) w.ents[victim].player=0;
        else { World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={actor};w.applyCmd(0,stop); }
        FILE* file=tmpfile();World replay;bool equal=file && w.saveGame(file);
        if(equal) { rewind(file);equal=replay.loadGame(file); }
        if(file) fclose(file);
        bool restored=equal && replay.ents[victim].occCell<0 && replay.ents[victim].magneticHeight==height;
        bool beamRestored=false;
        if(equal) for(const auto& effect:replay.effects)
            beamRestored |= effect.kind==3 && effect.aux==1 && effect.aux2==height;
        restored &= beamRestored;
        w.update();if(equal) { replay.update();equal &= w.checksum()==replay.checksum(); }
        const bool crash=(scenario>=1 && scenario<=4) || scenario==9 || scenario==10;
        bool landed=crash?!w.valid(victim):w.valid(victim) && w.ents[victim].magneticHeight==0
            && w.ents[victim].magneticBy==INVALID_EID && w.ents[victim].occCell>=0 && w.ents[victim].airBucket<0
            && w.ents[victim].hp==hp-height*12;
        if(scenario==10) landed &= !w.valid(parasite);
        for(int i=0;i<120 && equal;++i) { w.update();replay.update();equal &= w.checksum()==replay.checksum(); }
        const bool ok=lifted && targetable && restored && landed && equal;
        failures+=!ok;
        std::printf("MAGNETIC_LANDING %s scenario=%d lifted=%d targetable=%d restored=%d landed=%d replay=%d\n",
            ok?"PASS":"FAIL",scenario,lifted,targetable,restored,landed,equal);
        if(scenario==0) for(int invalid=0;invalid<3;++invalid) {
            World corrupt=w;corrupt.map.bldOccRef=&corrupt.bldOcc;
            if(invalid<2) corrupt.ents[victim].magneticHeight=invalid?13:-1;
            else { Effect effect;effect.kind=3;effect.aux=1;effect.aux2=13;effect.maxAge=12;corrupt.effects.push_back(effect); }
            FILE* bad=tmpfile();bool written=bad && corrupt.saveGame(bad),accepted=false;World rejected;
            if(written) { rewind(bad);accepted=rejected.loadGame(bad); }
            if(bad) fclose(bad);
            const bool rejects=written && !accepted;failures+=!rejects;
            std::printf("MAGNETIC_SAVE_VALIDATION %s field=%d\n",rejects?"PASS":"FAIL",invalid);
        }
    }
    return failures;
}
