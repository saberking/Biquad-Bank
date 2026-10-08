#ifndef PARAMETERS_HPP
#define PARAMETERS_HPP
#include "src/DistrhoDefines.h"
#include "Defines.hpp"

START_NAMESPACE_DISTRHO
enum Parameters {
#define X(i) kParamInPan##i, kParamInPOff##i, kParamFeed##i, kParamFreq##i,kParamOutPan##i, kParamOutPOff##i, kParamLvl##i, kParamPhs##i,
    BIQUAD_LIST
#undef X
    kParamDelay,
    kParamCount
};
END_NAMESPACE_DISTRHO
#endif // PARAMETERS_HPP
