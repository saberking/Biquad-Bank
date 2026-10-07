#ifndef PARAMETERS_HPP
#define PARAMETERS_HPP
#include "src/DistrhoDefines.h"
#include "Defines.hpp"

START_NAMESPACE_DISTRHO
enum Parameters {
#define X(i) kParamA##i, kParamB##i, kParamC##i, kParamD##i,
    BIQUAD_LIST
#undef X
    kParamDelay,
    kParamCount
};
END_NAMESPACE_DISTRHO
#endif // PARAMETERS_HPP
