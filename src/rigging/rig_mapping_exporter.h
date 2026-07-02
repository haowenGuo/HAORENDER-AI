#pragma once

#include "rigging/skeleton_types.h"

#include <QString>

namespace haorendergi {

class RigMappingExporter {
public:
    bool writeMapping(const QString& path,
                      const SkeletonGraph& source,
                      const SkeletonGraph& target,
                      const BoneMappingResult& result,
                      QString* error_message) const;
};

} // namespace haorendergi
