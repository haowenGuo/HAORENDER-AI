#pragma once

#include "scene/scene_types.h"

#include <QString>

#include <vector>

namespace haorendergi {

bool loadGltfVrmMaterialOverrides(const QString& path,
                                  std::vector<MaterialData>* materials,
                                  QString* error_message = nullptr);

} // namespace haorendergi
