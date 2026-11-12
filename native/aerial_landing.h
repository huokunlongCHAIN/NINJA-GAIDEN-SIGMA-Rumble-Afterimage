#pragma once
#include <stdint.h>

// Established by the accepted down-slash action cue, consumed by an actual
// landing sound. Neither the jump button nor a predicted timer creates impact.
class AerialLanding {
    uint64_t began=0;bool pending=false;
public:
    void clear(){pending=false;}
    void begin(uint64_t now){began=now;pending=true;}
    bool consume(uint64_t now){
        bool valid=pending&&now>=began&&now-began<=2000;
        pending=false;return valid;
    }
};
