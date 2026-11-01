#include "game/game.h"

std::string Game::superweaponFeedbackText(const SuperweaponOrderResult& result) const {
    std::string text;
    if(result.accepted) {
        if(result.type==(int)SWType::ChronoShift) text=TextFormat(TR(S::MsgSWTransferredFmt),result.transferred);
        else text=TR(S::MsgSWExecuted);
    } else switch(result.reason) {
        case SuperweaponRejectReason::Unavailable:text=TR(S::MsgSWUnavailable);break;
        case SuperweaponRejectReason::NotReady:text=TR(S::MsgSWNotReady);break;
        case SuperweaponRejectReason::NoSource:text=TR(S::MsgSWNoSource);break;
        case SuperweaponRejectReason::Blocked:text=TR(S::MsgSWBlocked);break;
        case SuperweaponRejectReason::InvalidTarget:text=TR(S::MsgSWTargetInvalid);break;
        default:text=TR(S::MsgSWInvalid);break;
    }
    if(result.type>=0 && result.type<(int)SWType::COUNT) text=std::string(swName((SWType)result.type))+(g_lang?": ":"：")+text;
    return text;
}
