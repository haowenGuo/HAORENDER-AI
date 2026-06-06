#pragma once

#include "rigging/skeleton_types.h"

#include <QString>

namespace haorendergi {

class SkeletonExtractor {
public:
    SkeletonGraph loadFromFile(const QString& path, QString* error_message) const;
};

} // namespace haorendergi
