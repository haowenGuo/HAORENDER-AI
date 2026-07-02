#pragma once

#include "rigging/skeleton_types.h"

namespace haorendergi {

class AiBoneMapper {
public:
    BoneMappingResult mapSkeletons(const SkeletonGraph& source, const SkeletonGraph& target) const;
};

} // namespace haorendergi
