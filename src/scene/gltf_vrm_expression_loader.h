#pragma once

#include "scene/scene_types.h"

#include <QString>

namespace haorendergi {

bool loadGltfVrmExpressions(const QString& path,
                            SceneModel* scene,
                            QString* error_message = nullptr);

} // namespace haorendergi
