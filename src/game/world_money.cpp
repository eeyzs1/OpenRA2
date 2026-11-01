#include "game/world.h"
#include <algorithm>

Credits World::reservedProductionFunds(int player) const {
    if(player<0 || player>=numPlayers) return 0;
    const auto& p=players[player];Credits reserved=0;
    for(const auto& order:p.unitProd) if(order.active) reserved+=std::max(0,order.paid);
    for(const auto* order:{&p.bldProd,&p.defProd}) if(order->active) reserved+=std::max(0,order->paid);
    return reserved;
}

Credits World::changeMoney(int player,Credits delta) {
    if(player<0 || player>=numPlayers) return 0;
    auto& p=players[player];const Credits before=p.money;
    if(delta<=0) p.money=std::max<Credits>(0,p.money+delta);
    else p.money+=std::min(delta,std::max<Credits>(0,MAX_CREDIT_BALANCE-p.money-reservedProductionFunds(player)));
    return p.money-before;
}

Credits World::creditIncome(int player,Credits amount) {
    if(player<0 || player>=numPlayers || amount<=0) return 0;
    const Credits room=std::max<Credits>(0,Credits(g_gameRules.maxMoney)-players[player].money);
    return changeMoney(player,std::min(amount,room));
}

Credits World::transferMoney(int from,int to,Credits amount) {
    if(from<0 || to<0 || from>=numPlayers || to>=numPlayers || from==to || amount<=0) return 0;
    const Credits credited=changeMoney(to,std::min(amount,players[from].money));
    players[from].money-=credited;
    return credited;
}
