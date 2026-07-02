#pragma once

#include "rigging/skeleton_types.h"

#include <QString>

namespace haorendergi {

class VrmNativeWriter {
public:
    bool writeFromSource(const QString& source_path,
                         const QString& output_path,
                         const SkeletonGraph& skeleton,
                         QString* status_message = nullptr) const;
};

} // namespace haorendergi
