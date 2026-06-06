#pragma once

#include "scene/scene_types.h"

#include <QString>

namespace haorendergi {

class ModelLoader {
public:
    SceneModel loadFromFile(const QString& path, QString* error_message) const;
    SceneModel loadAnimationFromFile(const QString& path, QString* error_message) const;
};

} // namespace haorendergi
