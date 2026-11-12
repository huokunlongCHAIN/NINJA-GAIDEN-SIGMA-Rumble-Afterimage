#pragma once
#include <stdint.h>

struct ContactCapture {uintptr_t source=0,target=0,aux=0;unsigned mode=0,flags=0;};

// In this verified entry point source is the recipient; target is the
// instigating character. The names preserve the original diagnostic format.
inline bool MatchContact(const ContactCapture& contact,uintptr_t player,int mode,unsigned role){
    bool present=contact.source && contact.target;
    if(mode>=0 && (!present || contact.mode!=unsigned(mode)))return false;
    if(role==1 && (!present || contact.target!=player || contact.source==player))return false;
    if(role==2 && (!present || contact.source!=player || contact.target==player))return false;
    return true;
}
