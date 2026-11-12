#pragma once
#include <stdint.h>

// A level is established by the accepted ready event, never by how long Y was
// held. Uncharged releases can share the ET motion sound and must stay silent.
class ChargeState {
    uint64_t started=0,ending=0,renewed=0,firstReady=0,secondReady=0;
    unsigned level=0;
    bool active=false,pendingEnd=false;
public:
    void clear(){active=false;pendingEnd=false;level=0;}
    void begin(uint64_t now){started=renewed=firstReady=now;ending=secondReady=0;level=0;active=true;pendingEnd=false;}
    bool valid(uint64_t now)const {
        return active && now>=started && now>=renewed && now-renewed<=15000 &&
               (!pendingEnd || (now>=ending && now-ending<=120));
    }
    bool ready(unsigned value,uint64_t now){
        if(!valid(now)||pendingEnd||value<=level||value>2)return false;
        if(!level)firstReady=now;
        if(value==2)secondReady=now;
        level=value;return true;
    }
    void renew(uint64_t now){if(valid(now)&&!pendingEnd)renewed=now;}
    bool sustaining(uint64_t now)const{return valid(now)&&!pendingEnd;}
    unsigned currentLevel()const{return level;}
    uint64_t beganAt()const{return started;}
    uint64_t firstReadyAt()const{return firstReady;}
    uint64_t secondReadyAt()const{return secondReady;}
    bool releaseReady(uint64_t now)const{return valid(now)&&level>0;}
    void finish(uint64_t now){if(valid(now)){pendingEnd=true;ending=now;}}
    unsigned release(uint64_t now){unsigned result=valid(now)?level:0;clear();return result;}
};
