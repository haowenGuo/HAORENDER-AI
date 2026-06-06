#pragma once

#include "scene/scene_types.h"

#include <QString>

namespace haorendergi {

bool loadGltfAnimationScene(const QString& path, SceneModel* scene, QString* error_message);

} // namespace haorendergi
