#include "game/game.h"

int Game::deploymentFeedbackRegression() {
    int failures=0;
    const int savedLanguage=g_lang;
    for(int language=0;language<2;++language) for(int scenario=0;scenario<18;++scenario) {
        g_lang=language;
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<world.ents.size();++i) if(world.ents[i].alive) world.kill((EID)i,false);
        for(auto& cell:world.map.cells) cell=Cell{};
        for(auto& player:world.players) { player.defeated=false;player.money=0; }
        world.cratesEnabled=false;world.mcvRepacks=true;world.presentationEvents.clear();
        localPlayer=0;netGame=false;pendingCmds.clear();sel.clear();selBuilding=INVALID_EID;msg="sentinel";
        const auto type=scenario==0 || scenario==4 || scenario==7 || (scenario>=11 && scenario<=14) || scenario==17?UnitType::MCV:scenario==1 || scenario==2?UnitType::SiegeChopper:
            scenario==5?UnitType::SlaveMiner:scenario==6?UnitType::V3Launcher:scenario==8?UnitType::Rhino:UnitType::GI;
        EID actor=world.spawnUnit(0,type,20.5f,20.5f);sel={actor};
        if(scenario==1) world.spawnBuilding(0,BldType::PowerPlant,20,20,true);
        if(scenario==0 || scenario==2) world.ents[actor].chrono=40;
        EID second=INVALID_EID;
        if(scenario==4) { second=world.spawnUnit(0,UnitType::MCV,40.5f,40.5f);sel.push_back(second); }
        if(scenario==9) sel.push_back(actor);
        if(scenario==10) { second=world.spawnUnit(0,UnitType::Rhino,24.5f,20.5f);sel.push_back(second); }
        if(scenario==15 || scenario==16) world.orderRadDeploy({actor});
        if(scenario==16) { second=world.spawnUnit(0,UnitType::GI,24.5f,20.5f);sel.push_back(second); }
        if(scenario==17) { second=world.spawnUnit(0,UnitType::MCV,20.5f+bldDef(BldType::ConYard).w-1,20.5f);sel.push_back(second); }
        if(scenario>=11 && scenario<=13) {
            world.kill(actor,false);actor=world.spawnBuilding(0,BldType::ConYard,20,20,true);
            sel.clear();selBuilding=actor;
            if(scenario==11) world.mcvRepacks=false;
            if(scenario==12) world.ents[actor].permaControlled=true;
        }
        const auto originalSelection=sel;
        World replay=world;replay.map.bldOccRef=&replay.bldOcc;
        World::Cmd expected{};expected.type=World::Cmd::Deploy;expected.ids=sel.empty()?std::vector<EID>{selBuilding}:sel;
        if(scenario==7 || scenario==14) netGame=true;
        cmdDeploySel();
        bool queued=(scenario!=7 && scenario!=14) || (pendingCmds.size()==1 && msg=="sentinel" && sel.size()==1 && world.valid(actor) && !world.ents[actor].isBuilding);
        if(scenario==14) { second=world.spawnUnit(0,UnitType::Engineer,45.5f,45.5f);replay.spawnUnit(0,UnitType::Engineer,45.5f,45.5f);sel={second}; }
        if(scenario==7 || scenario==14) { netGame=false;const auto commands=pendingCmds;pendingCmds.clear();for(const auto& command:commands) world.applyCmd(0,command); }
        replay.applyCmd(0,expected);
        bool equal=world.checksum()==replay.checksum();
        DeployOrderResult result;bool event=false;
        for(const auto& feedback:world.presentationEvents) if(feedback.kind==PresentationKind::DeployOrder) {
            result=feedback.deploy;event=feedback.player==0 && feedback.tick==world.tick && feedback.entity>=0 && feedback.positional;
        }
        const auto checksum=world.checksum();
        consumePresentationEvents();
        equal &= world.checksum()==checksum;
        bool outcome=true;
        if(scenario==0 || scenario==1 || scenario==2) outcome=world.valid(actor) && !world.ents[actor].deployed && !world.ents[actor].isBuilding
            && sel.size()==1 && msg!="sentinel" && msg!=TR(S::MsgDeployed) && msg!=TR(S::MsgDeployToggled);
        else if(scenario==4) {
            int yards=0;for(const auto& e:world.ents) yards+=e.alive && e.isBuilding && e.btype==BldType::ConYard;
            outcome=yards==2;
        } else if(scenario==7) outcome=world.valid(selBuilding) && world.ents[selBuilding].isBuilding && sel.empty();
        else if(scenario==8) outcome=result.rejected==1 && result.reason==DeployRejectReason::Unsupported && msg==TR(S::MsgDeployUnsupported);
        else if(scenario==10) outcome=world.ents[actor].deployed && !world.ents[second].deployed && result.accepted==1 && result.rejected==1
            && moveFeedbackPartial && msg==TextFormat(TR(S::MsgDeployPartial),1,1,TR(S::MsgDeployUnsupported));
        else if(scenario==11 || scenario==12) outcome=world.valid(selBuilding) && world.ents[selBuilding].isBuilding && result.rejected==1
            && msg==TR(scenario==11?S::MsgRepackingDisabled:S::MsgDeployControlled);
        else if(scenario==13) outcome=selBuilding==INVALID_EID && sel.size()==1 && !world.ents[sel[0]].isBuilding
            && world.ents[sel[0]].utype==UnitType::MCV && result.packed==1 && msg==TextFormat(TR(S::MsgPackDone),1);
        else if(scenario==14) outcome=sel==std::vector<EID>{second} && selBuilding==INVALID_EID && result.expanded==1;
        else if(scenario==15) outcome=!world.ents[actor].deployed && result.undeployed==1 && msg==TextFormat(TR(S::MsgUndeployDone),1);
        else if(scenario==16) outcome=!world.ents[actor].deployed && world.ents[second].deployed && result.accepted==2
            && result.deployed==1 && result.undeployed==1 && msg==TextFormat(TR(S::MsgDeployMixed),2);
        else if(scenario==17) outcome=result.expanded==1 && result.rejected==1 && result.reason==DeployRejectReason::Blocked
            && world.valid(second) && !world.ents[second].isBuilding && sel==std::vector<EID>{second} && moveFeedbackPartial;
        else outcome=world.ents[actor].deployed && msg!="sentinel" && msg!=TR(S::MsgDeployToggled);
        if(scenario==9) outcome &= result.accepted==1 && result.rejected==0;
        if(scenario==0 || scenario==2) outcome &= msg==TR(S::MsgImmobilized) && sel==originalSelection;
        if(scenario==1) outcome &= msg==TR(S::MsgDeployBlocked) && sel==originalSelection;
        for(int tick=0;tick<30;++tick) { world.update();replay.update();equal &= world.checksum()==replay.checksum(); }
        bool ok=queued && outcome && event && equal;failures+=!ok;
        std::printf("DEPLOYMENT_FEEDBACK %s scenario=%d language=%d queued=%d outcome=%d event=%d replay=%d\n",ok?"PASS":"FAIL",scenario,language,queued,outcome,event,equal);
    }
    netGame=false;pendingCmds.clear();
    g_lang=savedLanguage;
    return failures;
}
