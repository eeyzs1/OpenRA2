#include "game_regression.h"
#include "game/lang.h"
#include "game/hud_text.h"
#include "game/production_work.h"
#include "gfx/sprites.h"
#include <cstdio>
#include <filesystem>

int GameRegression::hudRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;
    auditHud=true;shotAtDisplaySize=true;SetTargetFPS(0);
    sim.active=true;sim.pos={-100,-100};sim.pressedL=sim.pressedR=sim.downL=sim.downR=false;
    const UnitType tanks[]={UnitType::Grizzly,UnitType::Rhino,UnitType::Type99,UnitType::LasherTank};
    EID factory=INVALID_EID,plant=INVALID_EID;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("HUD %s %s\n",ok?"PASS":"FAIL",name); };
    uint32_t cameoHash=0;bool cameoStable=true;
    for(int player:{0,2}) {
        const auto& cameo=g_sprites.iconUnit(UnitType::Type99,player);
        Image image=LoadImageFromTexture(cameo.tex);Color* pixels=LoadImageColors(image);
        uint32_t hash=2166136261u;
        for(int i=0;pixels && i<image.width*image.height;++i)
            for(unsigned char value:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a}) hash=(hash^value)*16777619u;
        cameoStable &= pixels && image.width==108 && image.height==84 && (!cameoHash || hash==cameoHash);
        cameoHash=hash;
        if(pixels) UnloadImageColors(pixels);UnloadImage(image);
    }
    check(cameoStable,"Type99 cameo loads at the contract size without applying house remap");
    std::printf("HUD_CAMEO type99=%08x remap_stable=%d\n",cameoHash,cameoStable);
    auto reset=[&](int faction) {
        world.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;
        int index=0;
        for(BldType type:{BldType::ConYard,BldType::PowerPlant,BldType::OreRefinery,BldType::Barracks,
                BldType::WarFactory,BldType::Radar,BldType::BattleLab,BldType::IndustrialPlant,BldType::AirForceCmd,BldType::NavalYard}) {
            EID id=world.spawnBuilding(0,type,8+(index%5)*8,8+(index/5)*9,true);
            if(type==BldType::WarFactory) factory=id;
            if(type==BldType::IndustrialPlant) plant=id;
            ++index;
        }
        world.spawnBuilding(0,BldType::NuclearReactor,50,50,true);
        world.players[0].money=10000;world.map.reveal(0,24,24,40);
        sel={world.spawnUnit(0,tanks[faction],22.5f,22.5f)};selBuilding=INVALID_EID;
        camZoom=1;camX=-(SCREEN_W-sidebarW)/2.f;camY=80;uiTab=3;uiScroll=0;
        moveFeedbackTimer=0;msgTimer=0;combatCues.clear();bakeTerrain();minimapTimer=0;
        sim.pos={-100,-100};render();
    };
    auto hover=[&](UnitType type) {
        for(const auto& item:hudItems) if(item.unit && item.type==(int)type) {
            sim.pos={item.bounds.x+item.bounds.width/2,item.bounds.y+item.bounds.height-3};return true;
        }
        return false;
    };
    auto capture=[&](const std::string& stem) {
        const auto hash=world.checksum();bool valid=true;
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();
            const auto& box=hudTip.bounds;
            valid &= !hudTip.lines.empty() && box.x>=0 && box.y>=0 && box.x+box.width<=SCREEN_W
                && box.y+box.height<=SCREEN_H-BOTTOM_BAR_H;
            for(size_t line=0;line<hudTip.lines.size();++line)
                valid &= MeasureTextEx(font,hudTip.lines[line].c_str(),(float)hudTip.lineSizes[line],0).x<=box.width-18+1;
            for(const auto& item:hudItems) valid &= item.labelSize>=14
                && MeasureTextEx(font,item.label.c_str(),(float)item.labelSize,0).x<=item.bounds.width-6+1;
            shotFile=(std::filesystem::path(outputDir)/(stem+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
        }
        valid &= hash==world.checksum();return valid;
    };
    for(int language=0;language<2;++language) {
        g_lang=language;
        for(int faction=0;faction<4;++faction) {
            reset(faction);bool found=hover(tanks[faction]);render();
            int quote=world.unitProductionCost(0,tanks[faction]);
            bool priced=hudTip.sub==TextFormat(TR(S::TipPriceFmt),quote) && quote<unitDef(tanks[faction]).cost;
            check(found && priced && capture("f"+std::to_string(faction)+"_l"+std::to_string(language)+"_vehicles"),
                "four-faction vehicle quote, readable labels, bounded tooltip and render isolation");
            uiTab=0;sim.pos={-100,-100};render();
            bool building=!hudItems.empty();
            if(building) { auto r=hudItems.front().bounds;sim.pos={r.x+10,r.y+r.height-3}; }
            check(building && capture("f"+std::to_string(faction)+"_l"+std::to_string(language)+"_buildings"),
                "four-faction building labels and tooltip fit both framebuffer sizes");
        }
        for(int state=0;state<8;++state) {
            reset(0);bool started=world.startUnitProd(0,UnitType::Grizzly);
            auto& prod=world.players[0].unitProd[unitDef(UnitType::Grizzly).prodCat()];
            const int buildTicks=unitDef(UnitType::Grizzly).buildTime;
            prod.progress=buildTicks/2;prod.workRemainder=512;
            prod.paid=(int)((int64_t)prod.totalCost*(prod.progress*PRODUCTION_WORK_SCALE+prod.workRemainder)
                /(int64_t(buildTicks)*PRODUCTION_WORK_SCALE));
            if(state==1) world.holdUnitProd(0,UnitType::Grizzly,true);
            if(state==2) world.players[0].money=0;
            if(state==3) { world.players[0].powerMade=0;world.players[0].powerUsed=1; }
            if(state==4) { prod.ready=true;prod.progress=buildTicks;prod.workRemainder=0;prod.paid=prod.totalCost; }
            if(state==5) world.kill(factory,false);
            if(state==6) { prod.totalCost=0;prod.paid=0; }
            if(state==7) world.kill(plant,false);
            render();hover(UnitType::Grizzly);render();
            bool status=hudTip.sub.find(TextFormat(TR(S::TipPaidFmt),prod.paid,prod.totalCost))!=std::string::npos;
            if(state==1) status &= hudTip.reason==TR(S::TipHeld);
            if(state==2) status &= hudTip.reason==TR(S::TipNoMoney);
            if(state==3) status &= hudTip.reason==TR(S::TipLowPower);
            if(state==4) status &= hudTip.reason==TR(S::ExitBlocked);
            if(state==5) status &= hudTip.reason==TextFormat(TR(S::TipRequireFmt),bldName(BldType::WarFactory));
            if(state==0 || state==6 || state==7) status &= hudTip.reason.empty();
            if(state==7) status &= hudTip.sub.find(TextFormat(TR(S::TipPriceFmt),unitDef(UnitType::Grizzly).cost))!=std::string::npos
                && prod.totalCost<unitDef(UnitType::Grizzly).cost;
            float progress=-1;
            for(const auto& item:hudItems) if(item.unit && item.type==(int)UnitType::Grizzly) progress=item.progress;
            status &= state==4?progress==1.f:progress>0.5f && progress<0.51f;
            check(started && status && capture("state"+std::to_string(state)+"_l"+std::to_string(language)),
                "paid work, held, funds, power, blocked exit, lost factory and free-order progress remain truthful");
        }
        sim.pos={20,(float)SCREEN_H-10};
        check(capture("bottom_l"+std::to_string(language)),"bottom-bar tooltip stays above the toolbar");
    }
    sim.active=false;auditHud=false;shotAtDisplaySize=false;g_lang=0;
    return failures;
}
