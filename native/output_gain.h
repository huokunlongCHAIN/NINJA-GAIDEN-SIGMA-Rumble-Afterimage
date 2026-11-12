#pragma once
#include "mixer.h"

inline MotorOutput ScaleGameOutput(MotorOutput value,unsigned percent){
    auto scale=[&](unsigned motor){
        uint64_t scaled=(uint64_t(motor)*percent+50)/100;
        return uint16_t(scaled>65535?65535:scaled);
    };
    return {scale(value.left),scale(value.right)};
}

inline MotorOutput MixGameAndCharge(MotorOutput game,MotorOutput charge,unsigned gamePercent,unsigned chargePercent){
    game=ScaleGameOutput(game,gamePercent);charge=ScaleGameOutput(charge,chargePercent);
    return {game.left>charge.left?game.left:charge.left,game.right>charge.right?game.right:charge.right};
}
