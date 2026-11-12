#pragma once
#include "mixer.h"
#include "contact_filter.h"

// Real animation-interpreter ranges, not controller-button timers.
struct SustainedAction {
    unsigned first=0,last=0,kind=0,maxMs=0;
    MotorOutput motors{};
    unsigned hitMs=0,hitCooldown=0;
    MotorOutput hit{};
    char label[80]{};
    uint64_t lastHit=0;
};
inline int FindSustainedAction(const SustainedAction* actions,unsigned count,unsigned pc) {
    if(!pc)return -1;
    for(unsigned i=0;i<count;++i)if(pc>=actions[i].first&&pc<actions[i].last)return int(i);
    return -1;
}
inline bool CanSustainedHit(const SustainedAction& action,const ContactCapture& contact,
                            uintptr_t player,bool damagePath,uint64_t now){
    return action.kind==2 && damagePath && MatchContact(contact,player,0,1) && action.hitMs &&
           (action.hit.left||action.hit.right) && (!action.lastHit||now-action.lastHit>=action.hitCooldown);
}
class ActionSustainState {
    int current=-1,blocked=-1;
    uint64_t began=0;
public:
    int active()const{return current;}
    bool suppressed(int i)const{return i>=0&&i==blocked;}
    void reset(){current=blocked=-1;began=0;}
    void stop(const SustainedAction* actions,unsigned count,unsigned pc){
        blocked=FindSustainedAction(actions,count,pc);current=-1;began=0;
    }
    MotorOutput sample(const SustainedAction* actions,unsigned count,unsigned pc,uint64_t now){
        int next=FindSustainedAction(actions,count,pc);
        if(next!=blocked)blocked=-1;
        if(next==blocked)next=-1;
        if(next!=current){current=next;began=now;}
        if(current<0)return {};
        const auto& action=actions[current];
        if(now-began>=action.maxMs){blocked=current;current=-1;return {};}
        return action.motors;
    }
};
