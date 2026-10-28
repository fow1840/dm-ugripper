#pragma once
#include <cstdint>
namespace DA::Ugripper {
inline bool InspectionFeedbackPhaseOn(unsigned phase) { return phase<4 && phase%2==0; }
inline bool InspectionFeedbackAdvance(unsigned phase,uint64_t elapsed,bool acknowledged) {
    return elapsed >= (InspectionFeedbackPhaseOn(phase)?350u:250u) && (acknowledged || elapsed>=1500);
}
}
