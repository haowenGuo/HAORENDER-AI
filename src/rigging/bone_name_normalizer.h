#pragma once

#include "rigging/skeleton_types.h"

#include <QString>

namespace haorendergi {

QString normalizeBoneName(const QString& name);
BoneSemantic classifyBoneName(const QString& name);

} // namespace haorendergi
