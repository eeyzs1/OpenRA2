#include "game/game.h"
#include <algorithm>

namespace {
bool country(int value){return value>(int)Country::None && value<=(int)Country::COUNT;}
bool mapSize(int value){for(int size:{48,64,96,128,160,200,256})if(size==value)return true;return false;}
const char* reasonKey(NetLink::RejectReason reason){switch(reason){
    case NetLink::RejectReason::Version:return "version";case NetLink::RejectReason::Rules:return "rules";
    case NetLink::RejectReason::InvalidRules:return "invalid-rules";case NetLink::RejectReason::Profile:return "selection";default:return "protocol";}}
}

void Game::netResetHandshake(){lobbyHelloSent=false;lobbyError=NetLink::RejectReason::None;lobbyPeerRules={};lobbyAgreedRules={};lobbyPeerChoice=RulesChoice::Fusion;netSeed=0;peerCountry=peerColor=-1;}

void Game::netRejectLobby(NetLink::RejectReason reason,bool notify){
    if(netGame){netPeerLeft=true;net.close();return;}
    lobbyError=reason;lobbyState=3;
    TraceLog(LOG_WARNING,"NET lobby rejected: %s",reasonKey(reason));
    if(notify && net.connected()) {
        NetLink::Writer body;body.w((uint8_t)reason);net.send(NetLink::MsgReject,body.b);
        // Keep polling until the peer reads the reason and closes, or the
        // player returns. Closing now could discard a buffered rejection.
    } else net.close();
}

const char* Game::netLobbyStatus() const {
    if(lobbyState==3){switch(lobbyError){
        case NetLink::RejectReason::Rules:return TR(S::NetRulesMismatch);
        case NetLink::RejectReason::Profile:return TR(S::NetProfileMismatch);
        case NetLink::RejectReason::Version:return TR(S::NetVersionMismatch);
        case NetLink::RejectReason::InvalidRules:return TR(S::NetInvalidRules);
        case NetLink::RejectReason::Protocol:return TR(S::NetHandshakeFailed);
        default:return TR(S::ConnectFail);}}
    if(lobbyState==5)return TR(S::NetCheckingRules);
    if(lobbyRole==0)return lobbyState==2?TR(S::PeerJoined):TR(S::WaitPeer);
    return lobbyState==2 || lobbyHelloSent?TR(S::WaitHostStart):TR(S::WaitPeer);
}

bool Game::netSendHello(){
    const auto digest=selectedRulesFingerprint();if(!digest){netRejectLobby(NetLink::RejectReason::InvalidRules);return false;}
    const auto choices=selectableCountries();if(std::find(choices.begin(),choices.end(),cfgCountry)==choices.end()){netRejectLobby(NetLink::RejectReason::Protocol);return false;}
    NetLink::Writer w;w.w(NetLink::SIMULATION_VERSION);w.w((uint8_t)cfgRules);w.w((uint8_t)cfgCountry);w.w((uint8_t)cfgColor);w.w(*digest);
    if(!net.send(NetLink::MsgHello,w.b)){lobbyState=3;net.close();return false;}
    lobbyHelloSent=true;return true;
}

bool Game::netStartHandshake(uint64_t seed){
    if(netGame || lobbyRole!=0 || lobbyState!=2)return false;
    if(lobbyPeerChoice!=cfgRules){netRejectLobby(NetLink::RejectReason::Profile);return false;}
    const auto digest=selectedRulesFingerprint();if(!digest){netRejectLobby(NetLink::RejectReason::InvalidRules);return false;}
    if(*digest!=lobbyPeerRules){netRejectLobby(NetLink::RejectReason::Rules);return false;}
    const auto choices=selectableCountries();
    if(!seed || !mapSize(cfgMapSize) || cfgMapType<0 || cfgMapType>6 || cfgMoney<0 || cfgMoney>1000000
        || std::find(choices.begin(),choices.end(),cfgCountry)==choices.end() || std::find(choices.begin(),choices.end(),peerCountry)==choices.end()){
        netRejectLobby(NetLink::RejectReason::Protocol);return false;}
    NetLink::Writer w;w.w(NetLink::SIMULATION_VERSION);w.w((uint8_t)cfgRules);w.w(seed);w.w((uint16_t)cfgMapSize);w.w((uint8_t)cfgMapType);
    w.w((int32_t)cfgMoney);w.w((uint8_t)cfgCrates);w.w((uint8_t)cfgCountry);w.w((uint8_t)cfgColor);w.w(*digest);
    if(!net.send(NetLink::MsgWelcome,w.b)){lobbyState=3;net.close();return false;}
    netSeed=seed;lobbyAgreedRules=*digest;lobbyState=5;return true;
}

bool Game::netHandleHandshake(const NetLink::Msg& message){
    const auto type=message.type;
    if(type!=NetLink::MsgHello && type!=NetLink::MsgWelcome && type!=NetLink::MsgReady && type!=NetLink::MsgStart && type!=NetLink::MsgReject)return false;
    using Reason=NetLink::RejectReason;NetLink::Reader r(message.body);
    const auto reject=[&](Reason reason){netRejectLobby(reason);return true;};
    if(type==NetLink::MsgReject){const auto value=r.r<uint8_t>();
        if(netGame || (lobbyState!=1 && lobbyState!=2 && lobbyState!=5) || !r.done() || value<(uint8_t)Reason::Version || value>(uint8_t)Reason::Profile)return reject(Reason::Protocol);
        netRejectLobby((Reason)value,false);return true;}
    if(netGame || (type==NetLink::MsgHello && (lobbyRole!=0 || lobbyState!=1))
        || (type==NetLink::MsgWelcome && (lobbyRole!=1 || lobbyState!=1 || !lobbyHelloSent))
        || (type==NetLink::MsgReady && (lobbyRole!=0 || lobbyState!=5))
        || (type==NetLink::MsgStart && (lobbyRole!=1 || lobbyState!=2 || !lobbyHelloSent)))return reject(Reason::Protocol);
    const uint32_t version=r.r<uint32_t>();if(!r.ok)return reject(Reason::Protocol);
    if(version!=NetLink::SIMULATION_VERSION)return reject(Reason::Version);
    const auto value=r.r<uint8_t>();if(!r.ok || value>=(uint8_t)RulesChoice::COUNT)return reject(Reason::Protocol);
    const auto choice=(RulesChoice)value;
    if(type==NetLink::MsgHello){const int peer=r.r<uint8_t>(),color=r.r<uint8_t>();const auto incoming=r.r<Sha256Digest>();
        if(!r.done() || !country(peer) || color>=MAX_PLAYERS)return reject(Reason::Protocol);
        if(choice!=cfgRules)return reject(Reason::Profile);
        const auto own=selectedRulesFingerprint();if(!own)return reject(Reason::InvalidRules);
        if(*own!=incoming)return reject(Reason::Rules);
        const auto choices=selectableCountries();if(std::find(choices.begin(),choices.end(),peer)==choices.end())return reject(Reason::Protocol);
        peerCountry=peer;peerColor=color;lobbyPeerChoice=choice;lobbyPeerRules=incoming;lobbyState=2;return true;}
    const uint64_t seed=r.r<uint64_t>();
    if(type==NetLink::MsgWelcome){const int size=r.r<uint16_t>(),terrain=r.r<uint8_t>();const int32_t money=r.r<int32_t>();
        const int crates=r.r<uint8_t>(),peer=r.r<uint8_t>(),color=r.r<uint8_t>();const auto incoming=r.r<Sha256Digest>();
        if(!r.done() || !seed || !mapSize(size) || terrain>6 || money<0 || money>1000000 || crates>1 || !country(peer) || color>=MAX_PLAYERS)return reject(Reason::Protocol);
        if(choice!=cfgRules)return reject(Reason::Profile);
        const auto own=selectedRulesFingerprint();if(!own)return reject(Reason::InvalidRules);
        if(*own!=incoming)return reject(Reason::Rules);
        const auto choices=selectableCountries();if(std::find(choices.begin(),choices.end(),peer)==choices.end()
            || std::find(choices.begin(),choices.end(),cfgCountry)==choices.end())return reject(Reason::Protocol);
        NetLink::Writer ready;ready.w(NetLink::SIMULATION_VERSION);ready.w((uint8_t)cfgRules);ready.w(seed);ready.w(incoming);
        if(!net.send(NetLink::MsgReady,ready.b)){lobbyState=3;net.close();return true;}
        netSeed=seed;cfgMapSize=size;cfgMapType=terrain;cfgMoney=money;cfgCrates=crates!=0;
        peerCountry=peer;peerColor=color;lobbyPeerChoice=choice;lobbyAgreedRules=incoming;lobbyState=2;return true;}
    const auto incoming=r.r<Sha256Digest>();
    if(!r.done() || !seed || seed!=netSeed || !country(peerCountry))return reject(Reason::Protocol);
    if(choice!=cfgRules || lobbyPeerChoice!=cfgRules)return reject(Reason::Profile);
    const auto own=selectedRulesFingerprint();if(!own)return reject(Reason::InvalidRules);
    if(incoming!=lobbyAgreedRules || incoming!=*own)return reject(Reason::Rules);
    if(type==NetLink::MsgReady){NetLink::Writer start;start.w(NetLink::SIMULATION_VERSION);start.w((uint8_t)cfgRules);start.w(seed);start.w(incoming);
        if(!net.send(NetLink::MsgStart,start.b)){lobbyState=3;net.close();return true;}netPlayer=0;
    } else netPlayer=1;
    netBeginGame();return true;
}
