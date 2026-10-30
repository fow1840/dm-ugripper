#pragma once
#include "record_runtime/runtime_types.h"
#include <optional>
#include <string>
#include <string_view>
namespace DA::Ugripper {
inline int GripperGeneration(std::string_view sn) {
    if (sn.size()!=16 || sn.substr(0,3)!="DAG") return 0;
    for (char c:sn) if (!((c>='A'&&c<='Z')||(c>='0'&&c<='9'))) return 0;
    return (sn[4]=='1'||sn[4]=='2')?25:sn[4]=='3'?29:0;
}
inline std::optional<HealthFault> GripperGenerationFault(
    bool leftConnected, std::string_view left, bool rightConnected, std::string_view right) {
    if (!leftConnected || !rightConnected) return std::nullopt;
    const int l=GripperGeneration(left),r=GripperGeneration(right);
    if (!((l==25&&r==29)||(l==29&&r==25))) return std::nullopt;
    return HealthFault{RuntimeLedState::Error2,
        l==25?HardwareFaultSide::Left:HardwareFaultSide::Right,
        "gripper_version_mismatch", "Mixed V2.5/V2.9 grippers; replace old side: left="+
        std::string(left)+" right="+std::string(right)};
}
}
