#pragma once
#include <algorithm>
#include <cstdint>

// Fixed work units: speed changes affect future work, never completed work.
constexpr int PRODUCTION_WORK_SCALE = 1024;
template<class Item,class Funds>
bool advanceProduction(Item& p, Funds& money, int buildTime, int workRate) {
    const int64_t total = int64_t(std::max(1, buildTime)) * PRODUCTION_WORK_SCALE;
    const int64_t old = int64_t(p.progress) * PRODUCTION_WORK_SCALE + p.workRemainder;
    const int64_t next = std::min(total, old + std::max(0, workRate));
    const int due = next == total ? p.totalCost : int(int64_t(p.totalCost) * next / total);
    const int debit = std::max(0, due - p.paid);
    if (money < debit) return false;
    money -= debit;
    p.paid += debit;
    p.progress = int(next / PRODUCTION_WORK_SCALE);
    p.workRemainder = int(next % PRODUCTION_WORK_SCALE);
    return next == total && p.paid == p.totalCost;
}

// The source production clock has 54 discrete installments. This transient
// view adapts the ordinary queue and authored-mission factory jobs without
// changing either saved representation or their scheduling/price policies.
struct NativeProductionWork {
    int& work;
    int& progress;
    int& paid;
};
template<class Funds>
bool advanceNativeProductionWork(NativeProductionWork p,Funds& money,int totalCost,int interval) {
    if(p.progress>=54)return true;
    if(!interval || ++p.work<interval)return false;
    p.work=0;
    const int installment=(totalCost-p.paid)/(54-p.progress);
    if(money<installment)return false;
    money-=installment;p.paid+=installment;++p.progress;
    return p.progress==54;
}
