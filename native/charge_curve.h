#pragma once
#include "mixer.h"
#include "charge_state.h"

struct ChargeCurveConfig {
    bool enabled=true;
    unsigned firstRampMs=1350,secondRampMs=1940,gapMs=60,utGapMs=60;
    MotorOutput start{3277,7864},et{9830,18350},ut{16384,27525};
};

inline MotorOutput ChargeRamp(MotorOutput from,MotorOutput to,uint64_t elapsed,unsigned duration){
    if(!duration || elapsed>=duration)return to;
    auto blend=[&](unsigned a,unsigned b){return uint16_t(int64_t(a)+(int64_t(b)-a)*int64_t(elapsed)/duration);};
    return {blend(from.left,to.left),blend(from.right,to.right)};
}

// The accepted game events establish the two charge phases. A held button only
// renews an already established state; pressing Y alone never creates rumble.
inline MotorOutput SampleChargeCurve(const ChargeState& state,const ChargeCurveConfig& config,
                                      uint64_t now,bool held){
    if(!config.enabled||!held||!state.sustaining(now))return {};
    if(!state.currentLevel())return ChargeRamp(config.start,config.et,now-state.beganAt(),config.firstRampMs);
    uint64_t elapsed=now-state.firstReadyAt();
    if(elapsed<config.gapMs)return {};
    if(state.currentLevel()==2){
        if(now-state.secondReadyAt()<config.utGapMs)return {};
        return config.ut;
    }
    return ChargeRamp(config.et,config.ut,elapsed-config.gapMs,config.secondRampMs);
}
