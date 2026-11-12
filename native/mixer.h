#pragma once
#include <stdint.h>

struct MotorOutput { uint16_t left=0, right=0; };

// Worker-thread-only, allocation-free envelope mixer. State effects are renewed
// by real events; no pulse can keep a motor running indefinitely.
class RumbleMixer {
    struct Pulse {
        uint64_t start=0; uint32_t duration=0; MotorOutput motors{};
        unsigned group=0, priority=0; bool fade=true;
    };
    Pulse pulses[128]{};
public:
    void clear() { for(auto& p:pulses) p.duration=0; }
    void cancel(unsigned group) { for(auto& p:pulses) if(p.group==group)p.duration=0; }
    void add(uint64_t start, unsigned left, unsigned right, unsigned duration,
             bool fade=true, unsigned group=0, unsigned priority=0) {
        if(!duration || (!left && !right)) return;
        if(duration>2000) duration=2000;
        if(left>65535) left=65535; if(right>65535) right=65535;
        Pulse* slot=nullptr;
        for(auto& p:pulses) {
            // sample() retires elapsed effects. A future start time must not
            // retire an effect that is still active at the current time.
            if(!p.duration) {slot=&p;break;}
            if(!slot || p.priority<slot->priority || (p.priority==slot->priority &&
               p.start+p.duration<slot->start+slot->duration)) slot=&p;
        }
        if(slot->duration && slot->priority>priority)return;
        *slot={start,uint32_t(duration),{uint16_t(left),uint16_t(right)},group,priority,fade};
    }
    MotorOutput sample(uint64_t now,unsigned filterGroup=0,bool excludeGroup=false) {
        MotorOutput output{};
        for(auto& p:pulses) {
            if(!p.duration || now<p.start) continue;
            uint64_t elapsed=now-p.start;
            if(elapsed>=p.duration) {p.duration=0;continue;}
            if(filterGroup && ((p.group==filterGroup)==excludeGroup))continue;
            unsigned left=p.fade?unsigned(uint64_t(p.motors.left)*(p.duration-elapsed)/p.duration):p.motors.left;
            unsigned right=p.fade?unsigned(uint64_t(p.motors.right)*(p.duration-elapsed)/p.duration):p.motors.right;
            if(left>output.left) output.left=uint16_t(left);
            if(right>output.right) output.right=uint16_t(right);
        }
        return output;
    }
};
