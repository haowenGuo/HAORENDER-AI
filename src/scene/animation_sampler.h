#pragma once

#include "scene/scene_types.h"

namespace haorendergi {

bool sampleSceneAnimation(const SceneModel& bind_scene,
                          int animation_index,
                          double time_seconds,
                          SceneModel* output_scene);

} // namespace haorendergi
