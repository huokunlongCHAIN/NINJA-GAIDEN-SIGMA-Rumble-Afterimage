#pragma once
#include <stdint.h>

// Audio-script hints classify an already confirmed collision; they never
// generate a hit or output on their own. Only the sampled dragon-sword actions
// are labelled. Other weapons/actions fall back to ordinary contact feedback.
class AttackContext {
    unsigned kind=0;
    uint64_t at=0;
public:
    void clear(){kind=0;at=0;}
    void set(unsigned value,uint64_t now){kind=value;at=now;}
    bool heavy(uint64_t now,unsigned maxAge)const {
        return kind==2 && now>=at && now-at<=maxAge;
    }
};
