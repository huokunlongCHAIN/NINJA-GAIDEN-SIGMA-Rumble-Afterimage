#pragma once
#include "mixer.h"

struct WaveSegment { unsigned offset, duration, left, right; };
struct Waveform {
    unsigned length, count; bool stateBound;
    WaveSegment segments[4];
};
#include "table_waveforms.generated.h"

// W05 contacts and W18 grab/landing are event-aligned. A contact never schedules
// imaginary future hits. State-bound W14/W19 play at most one renewed cycle.
inline bool PlayWaveform(RumbleMixer& mixer, unsigned wave, uint64_t start,
                         unsigned group, unsigned priority, unsigned segment=0,
                         const Waveform* bank=kTableWaveforms) {
    if(wave>=20)return false;
    if(!wave) {mixer.cancel(group);return true;}
    const auto& effect=bank[wave];
    if((wave==5||wave==18)&&!segment)return false;
    if(segment>effect.count)return false;
    if(effect.stateBound)mixer.cancel(group);
    for(unsigned n=0;n<effect.count;++n) {
        if(segment && n!=segment-1)continue;
        const auto& pulse=effect.segments[n];
        // A selected segment starts on its actual event, not its example offset.
        mixer.add(start+(segment?0:pulse.offset),pulse.left,pulse.right,pulse.duration,
                  false,group,priority);
    }
    return true;
}
