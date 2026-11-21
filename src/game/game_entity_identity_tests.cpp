#include "game_regression.h"
#include "game/script.h"
#include "world_save_sections.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

int GameRegression::entityIdentityRegression() {
    int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("ENTITY_IDENTITY %s: %s\n",ok?"PASS":"FAIL",text);failures+=!ok;};
    const auto fresh=[](Game& g) {
        g.world.init(48,48,47,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)g.world.ents.size();++id)if(g.world.valid(id))g.world.kill(id,false);
        for(auto& cell:g.world.map.cells)cell=Cell{};
        for(auto& p:g.world.players){p.active=true;p.defeated=false;}
        g.world.cratesEnabled=false;g.localPlayer=0;g.phase=Phase::InGame;g.netGame=false;
        g.resetMatchInteraction();
    };
    {
        Game g;fresh(g);const EID old=g.world.spawnUnit(0,UnitType::Grizzly,5.5f,5.5f);
        const auto reference=g.world.entityRef(old);
        g.sel={old};g.groups[0]={old};g.chronoSourceSel={old};g.netGame=true;
        World::Cmd move;move.type=World::Cmd::Move;move.ids={old};move.x=30.5f;move.y=5.5f;g.issueCmd(move);
        const bool captured=g.pendingCmds.size()==1 && g.pendingCmds.front().identitiesBound
            && g.pendingCmds.front().generations==std::vector<uint64_t>{reference.generation};
        g.world.kill(old,false);const EID replacement=g.world.spawnUnit(0,UnitType::GI,5.5f,6.5f);
        const auto before=g.world.checksum();if(captured)g.world.applyCmd(0,g.pendingCmds.front());
        check(captured && replacement==old && !g.world.valid(reference) && g.world.checksum()==before
            && g.world.ents[replacement].state==UState::Idle,"a command captured by the real deferred ingress cannot move a replacement in the same slot");
        g.pruneInteractionReferences();
        check(g.sel.empty() && g.groups[0].empty() && g.chronoSourceSel.empty(),
            "selection, a dormant control group and a two-stage chrono source all discard the retired incarnation");
        g.netGame=false;
    }
    {
        Game g;fresh(g);const EID actor=g.world.spawnUnit(0,UnitType::Grizzly,5.5f,5.5f);
        const EID old=g.world.spawnUnit(1,UnitType::Conscript,10.5f,5.5f);
        World::Cmd attack;attack.type=World::Cmd::Attack;attack.ids={actor};attack.a=old;g.world.bindCommand(attack);
        const auto generation=g.world.entityRef(old).generation;
        g.world.kill(old,false);const EID replacement=g.world.spawnUnit(1,UnitType::Rhino,10.5f,6.5f);
        const auto before=g.world.checksum();g.world.applyCmd(0,attack);
        check(replacement==old && g.world.ents[replacement].generation!=generation && before==g.world.checksum()
            && g.world.ents[actor].target==INVALID_EID,"a delayed attack does not retarget the next enemy born in its target slot");
        World::Cmd current=attack;current.identitiesBound=false;g.world.bindCommand(current);g.world.applyCmd(0,current);
        check(g.world.ents[actor].target==replacement,"a newly submitted order can deliberately target that replacement");
    }
    {
        Game g;fresh(g);const EID first=g.world.spawnUnit(0,UnitType::Grizzly,5.5f,5.5f);
        const EID second=g.world.spawnUnit(0,UnitType::Grizzly,5.5f,8.5f);
        World::Cmd move;move.type=World::Cmd::Move;move.ids={first,second};move.x=30.5f;move.y=5.5f;g.world.bindCommand(move);
        g.world.kill(first,false);const EID replacement=g.world.spawnUnit(0,UnitType::GI,5.5f,6.5f);
        g.world.applyCmd(0,move);
        check(replacement==first && g.world.ents[replacement].state==UState::Idle && g.world.ents[second].state==UState::Moving,
            "a mixed selection executes for surviving identities while ignoring only stale members");
    }
    {
        Game g;fresh(g);const EID old=g.world.spawnBuilding(0,BldType::PowerPlant,5,5,true);
        g.selBuilding=old;g.captureInteractionReferences();
        World::Cmd sell;sell.type=World::Cmd::SellBuilding;sell.ids={old};g.world.bindCommand(sell);
        g.world.kill(old,false);const EID replacement=g.world.spawnBuilding(0,BldType::PowerPlant,12,12,true);
        const auto before=g.world.checksum();g.world.applyCmd(0,sell);g.pruneInteractionReferences();
        check(replacement==old && g.world.checksum()==before && !g.world.ents[replacement].selling && g.selBuilding==INVALID_EID,
            "stale building selection and sale commands cannot select or sell a newly constructed building");
    }
    {
        Game g;fresh(g);g.world.mcvRepacks=true;
        const EID yard=g.world.spawnBuilding(0,BldType::ConYard,12,12,true);
        g.selBuilding=yard;g.rememberSelection();
        World::Cmd move;move.type=World::Cmd::Move;move.ids={yard};move.x=30.5f;move.y=30.5f;g.issueCmd(move);
        check(g.selBuilding==INVALID_EID && g.sel.size()==1 && g.world.valid(g.sel.front())
            && !g.world.ents[g.sel.front()].isBuilding && g.world.ents[g.sel.front()].utype==UnitType::MCV
            && g.world.ents[g.sel.front()].state==UState::Moving,
            "right-click repacking carries source/replacement identities and transfers selection to the actual moving MCV");
    }
    for(bool reselectSource:{false,true}) {
        Game g;fresh(g);const EID source=g.world.spawnUnit(0,UnitType::MCV,12.5f,12.5f);
        g.sel={source};g.rememberSelection();const auto result=g.world.orderDeployment({source});
        g.world.emitDeployResult(0,result);bool safe=result.changes.size()==1;
        EID replacement=INVALID_EID;
        if(safe) {
            if(reselectSource) {
                // The completed conversion releases its source slot. Reuse
                // that source independently of the still-live replacement.
                replacement=g.world.spawnUnit(0,UnitType::GI,5.5f,5.5f);g.sel={replacement};g.rememberSelection();
                g.world.kill(result.changes.front().replacement,false);
            } else {
                g.world.kill(result.changes.front().replacement,false);replacement=g.world.spawnUnit(0,UnitType::GI,5.5f,5.5f);
            }
            g.consumePresentationEvents();
            safe=reselectSource?replacement==source && g.sel==std::vector<EID>{replacement} && g.selBuilding==INVALID_EID:
                replacement==result.changes.front().replacement && g.sel.empty() && g.selBuilding==INVALID_EID;
        }
        check(safe,reselectSource?"an old deployment receipt cannot steal a new explicit selection after source-slot reuse":
            "a delayed deployment receipt cannot select a replacement whose slot has already been reused");
    }
    {
        Game g;fresh(g);const EID retired=g.world.spawnUnit(0,UnitType::GI,5.5f,5.5f);
        g.world.kill(retired,false);const EID live=g.world.spawnUnit(0,UnitType::Grizzly,7.5f,5.5f);
        const auto handle=g.world.entityRef(live);FILE* f=tmpfile();std::vector<uint8_t> bytes;
        bool saved=f && g.world.saveGame(f);if(saved){const long length=ftell(f);rewind(f);bytes.resize((size_t)length);saved=fread(bytes.data(),1,bytes.size(),f)==bytes.size();}
        if(f)fclose(f);
        const auto load=[](const std::vector<uint8_t>& data,World& world){FILE* file=tmpfile();bool ok=file && fwrite(data.data(),1,data.size(),file)==data.size();
            if(ok){rewind(file);ok=world.loadGame(file);}if(file)fclose(file);return ok;};
        World restored;check(saved && load(bytes,restored) && restored.valid(handle) && restored.checksum()==g.world.checksum(),
            "current World saves preserve live and retired slot generations and the complete checksum");
        World changed=g.world;++changed.ents[live].generation;
        check(changed.checksum()!=g.world.checksum(),"a generation difference is visible to lockstep checksums");
        const auto section=locateWorldSaveSection(bytes,WorldSaveSection::EntityGenerations);
        bool rejected=saved && section.has_value();
        if(rejected){auto invalid=bytes;const uint64_t zero=0;std::memcpy(invalid.data()+section->offset+4+(size_t)live*8,&zero,8);
            World broken;rejected=!load(invalid,broken);invalid=bytes;invalid.pop_back();rejected &= !load(invalid,broken);
            invalid=bytes;invalid[section->footer]^=1;rejected &= !load(invalid,broken);}
        check(rejected,"zero live generations, truncated records and invalid identity-section lengths are rejected");
        bool legacy=saved && section.has_value();
        if(legacy){auto old=bytes;old.resize(section->offset);old[6]='4';old[7]='8';World migrated;legacy=load(old,migrated) && migrated.valid(live)
            && migrated.ents[live].generation==1 && migrated.ents[live].x==g.world.ents[live].x;}
        check(legacy,"World48 saves migrate to a fresh identity epoch without losing their simulation state");
    }
    {
        Game g;fresh(g);const EID exhausted=g.world.spawnUnit(0,UnitType::GI,5.5f,5.5f);
        g.world.ents[exhausted].generation=World::MAX_ENTITY_GENERATION;const auto handle=g.world.entityRef(exhausted);
        g.world.kill(exhausted,false);const EID successor=g.world.spawnUnit(0,UnitType::GI,6.5f,5.5f);
        check(successor!=exhausted && !g.world.valid(handle) && g.world.ents[exhausted].generation==World::MAX_ENTITY_GENERATION,
            "an exhausted generation retires the slot permanently instead of wrapping an external handle");
    }
    {
        Game receiver;fresh(receiver);NetLink sender;
        const EID actor=receiver.world.spawnUnit(0,UnitType::Grizzly,5.5f,5.5f);
        const EID target=receiver.world.spawnUnit(1,UnitType::Conscript,10.5f,5.5f);
        World::Cmd attack;attack.type=World::Cmd::Attack;attack.ids={actor};attack.a=target;receiver.world.bindCommand(attack);
        receiver.world.kill(target,false);receiver.world.spawnUnit(1,UnitType::Rhino,10.5f,6.5f);
        bool linked=false;
        for(int port=18340;port<18356 && !linked;++port) {
            receiver.net.close();sender.close();if(!receiver.net.host(port) || !sender.connectTo("127.0.0.1",port))continue;
            for(int i=0;i<200 && !linked;++i){receiver.net.poll();sender.poll();linked=receiver.net.connected() && sender.connected();
                if(!linked)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
        receiver.netGame=true;receiver.netPlayer=receiver.localPlayer=1;receiver.netSimTick=NET_DELAY;
        NetLink::Writer packet;packet.w((uint32_t)NET_DELAY);packet.w(uint16_t{1});packet.w((uint8_t)attack.type);packet.w(uint16_t{1});
        packet.w((int32_t)actor);packet.w(attack.generations.front());packet.w(attack.x);packet.w(attack.y);
        packet.w((int32_t)attack.a);packet.w((int32_t)attack.b);packet.w(attack.targetGeneration);packet.w(uint8_t{0});
        const auto before=receiver.world.checksum();bool transmitted=linked && sender.send(NetLink::MsgCmdFrame,packet.b);
        for(int i=0;i<200 && transmitted && !receiver.remoteCmds.contains(NET_DELAY);++i){sender.poll();receiver.netHandleMsgs();
            if(!receiver.remoteCmds.contains(NET_DELAY))std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        const auto entry=receiver.remoteCmds.find(NET_DELAY);bool decoded=entry!=receiver.remoteCmds.end() && entry->second.size()==1;
        if(decoded){const auto& received=entry->second.front();decoded=received.identitiesBound && received.generations==attack.generations
            && received.targetGeneration==attack.targetGeneration;receiver.world.applyCmd(0,received);}
        check(transmitted && decoded && !receiver.netPeerLeft && receiver.world.checksum()==before,
            "the real TCP command parser retains submission identities and rejects a stale target at execution on the receiving peer");
        receiver.world.freeList.clear();const EID future=(EID)receiver.world.ents.size();
        NetLink::Writer upcoming;upcoming.w((uint32_t)(NET_DELAY+1));upcoming.w(uint16_t{1});
        upcoming.w((uint8_t)World::Cmd::Move);upcoming.w(uint16_t{1});upcoming.w((int32_t)future);upcoming.w(uint64_t{1});
        upcoming.w(20.5f);upcoming.w(20.5f);upcoming.w(int32_t{0});upcoming.w(int32_t{0});upcoming.w(uint64_t{0});upcoming.w(uint8_t{0});
        bool accepted=transmitted && sender.send(NetLink::MsgCmdFrame,upcoming.b);
        for(int i=0;i<200 && accepted && !receiver.remoteCmds.contains(NET_DELAY+1);++i){sender.poll();receiver.netHandleMsgs();
            if(!receiver.remoteCmds.contains(NET_DELAY+1))std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        accepted &= !receiver.netPeerLeft && receiver.remoteCmds.contains(NET_DELAY+1);
        const EID born=receiver.world.spawnUnit(0,UnitType::GI,15.5f,15.5f);
        if(accepted)for(const auto& command:receiver.remoteCmds[NET_DELAY+1])receiver.world.applyCmd(0,command);
        check(accepted && born==future && receiver.world.ents[born].state==UState::Moving,
            "a valid ahead-of-time peer command can arrive before its actor is born locally and resolves that identity at execution");
        receiver.netGame=false;receiver.net.close();sender.close();
    }
    {
        Game host;fresh(host);NetLink peer;const EID actor=host.world.spawnUnit(0,UnitType::GI,5.5f,5.5f);
        bool linked=false;
        for(int port=18360;port<18376 && !linked;++port) {
            host.net.close();peer.close();if(!host.net.host(port) || !peer.connectTo("127.0.0.1",port))continue;
            for(int i=0;i<200 && !linked;++i){host.net.poll();peer.poll();linked=host.net.connected() && peer.connected();
                if(!linked)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
        host.netGame=true;World::Cmd command;command.type=World::Cmd::Move;command.ids.assign(4096,actor);command.x=20.5f;command.y=20.5f;
        host.issueCmd(command);command.type=World::Cmd::Stop;host.issueCmd(command);host.netAdvance();host.netAdvance();
        for(int i=0;i<200 && peer.inbox.size()<2;++i){host.net.poll();peer.poll();if(peer.inbox.size()<2)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        bool bounded=linked && !host.netDesync && host.pendingCmds.empty() && peer.inbox.size()==2;int sequence=0;
        for(const auto& message:peer.inbox) {
            NetLink::Reader reader(message.body);const auto tick=reader.r<uint32_t>();const auto count=reader.r<uint16_t>();
            const auto type=reader.r<uint8_t>();const auto actors=reader.r<uint16_t>();
            bounded &= message.type==NetLink::MsgCmdFrame && message.body.size()<=NetLink::MAX_BODY_BYTES
                && tick==NET_DELAY+sequence && count==1 && actors==4096 && type==(sequence?World::Cmd::Stop:World::Cmd::Move);
            ++sequence;
        }
        check(bounded,"two maximum-selection orders split into bounded FIFO TCP frames instead of silently losing an oversized packet");
        host.netGame=false;host.net.close();peer.close();
    }
    {
        Game g;fresh(g);g_script.init(&g.world,0,-1);g_script.onGameStart();g_script.onTrigger("stale");
        const auto infantry=[&](){for(EID id=0;id<(EID)g.world.ents.size();++id)if(g.world.valid(id)
            && !g.world.ents[id].isBuilding && g.world.ents[id].player==0 && g.world.ents[id].utype==UnitType::GI)return id;return INVALID_EID;};
        EID unit=infantry();
        check(g_script.onTriggerCond("safe") && g.world.valid(unit) && g.world.ents[unit].state==UState::Idle
            && g.world.ents[unit].target==INVALID_EID,
            "Lua spawn/query/death APIs retain identities and old scalar or array handles cannot move, damage, kill or retarget successors");
        const auto path=std::filesystem::temp_directory_path()/("openra2-entity-handles-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sav");
        const auto utf8=path.u8string();const std::string file(utf8.begin(),utf8.end());
        const bool saved=g.saveGameFile(file.c_str());const auto before=g.world.checksum();
        const bool loaded=saved && g.loadGameFile(file.c_str());g_script.onTrigger("stale");unit=infantry();
        check(loaded && g_script.onTriggerCond("safe") && before==g.world.checksum() && g.world.valid(unit),
            "actual application save/load preserves opaque Lua integers and does not revive stale script handles");
        g_script.onTrigger("current");
        check(g.world.valid(unit) && g.world.ents[unit].state==UState::Moving,
            "a live script handle restored from the same snapshot can still issue a real movement order");
        const EID damaged=g.world.spawnUnit(0,UnitType::MCV,43.5f,15.5f);g.world.ents[damaged].hp=333;
        World::Cmd conversion{};conversion.type=World::Cmd::Deploy;conversion.ids={damaged};g.world.applyCmd(0,conversion);
        check(g_script.onTriggerCond("conversion_health"),"the actual building-complete Lua callback observes inherited conversion damage");
        g_script.onTrigger("reentrant_prepare");std::vector<EID> mcvs;
        for(EID id=0;id<(EID)g.world.ents.size();++id)if(g.world.valid(id) && !g.world.ents[id].isBuilding
            && g.world.ents[id].utype==UnitType::MCV)mcvs.push_back(id);
        std::sort(mcvs.begin(),mcvs.end(),[&](EID a,EID b){return g.world.ents[a].x<g.world.ents[b].x;});
        World::Cmd deploy;deploy.type=World::Cmd::Deploy;deploy.ids=mcvs;g.world.bindCommand(deploy);g.world.applyCmd(0,deploy);
        EID newborn=INVALID_EID;for(EID id=0;id<(EID)g.world.ents.size();++id)if(g.world.valid(id) && !g.world.ents[id].isBuilding
            && g.world.ents[id].utype==UnitType::GI && g.world.ents[id].x==30.5f)newborn=id;
        check(mcvs.size()==2 && g.world.valid(newborn) && !g.world.ents[newborn].deployed,
            "a construction callback cannot replace a later batch member with a newborn that inherits the old deployment command");
        g_script.shutdown();std::error_code error;std::filesystem::remove(path,error);
    }
    {
        Game g;fresh(g);g.world.freeList.clear();g_script.init(&g.world,0,-1);g_script.onTrigger("legacy_live_prepare");
        const auto path=std::filesystem::temp_directory_path()/("openra2-legacy-handles-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sav");
        const auto utf8=path.u8string();const std::string file(utf8.begin(),utf8.end());
        bool legacy=g.saveGameFile(file.c_str());std::vector<uint8_t> bytes;
        {std::ifstream input(path,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(input),{});}
        const std::string magic="RA2WLD60";const auto start=std::search(bytes.begin(),bytes.end(),magic.begin(),magic.end());
        legacy &= start!=bytes.end();
        if(legacy) {
            const size_t offset=(size_t)(start-bytes.begin());
            const auto identities=locateWorldSaveSection(std::span<const uint8_t>(bytes).subspan(offset),WorldSaveSection::EntityGenerations);
            legacy=identities.has_value();
            if(legacy){bytes.resize(offset+identities->offset);bytes[offset+6]='4';bytes[offset+7]='8';
                {std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write((const char*)bytes.data(),bytes.size());}
                legacy=g.loadGameFile(file.c_str());}
        }
        g_script.onTrigger("legacy_live_kill");
        check(legacy && g_script.onTriggerCond("legacy_callback"),
            "an actual World48 application snapshot keeps legacy saved numeric-ID comparisons valid in death callbacks");
        g_script.onTrigger("legacy_live_successor");
        check(legacy && g_script.onTriggerCond("legacy_protected"),
            "a migrated first-generation raw handle becomes permanently invalid when that slot is reused");
        g_script.shutdown();std::error_code error;std::filesystem::remove(path,error);
    }
    std::printf("ENTITY_IDENTITY failures=%d\n",failures);return failures;
}
