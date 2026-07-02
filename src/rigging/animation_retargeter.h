#pragma once

#include "rigging/ai_bone_mapper.h"
#include "scene/scene_types.h"

#include <QString>

namespace haorendergi {

bool retargetAnimationToTarget(const SceneModel& source_animation,
                               const SceneModel& target_bind_scene,
                               const BoneMappingResult& mapping_result,
                               SceneModel* output_scene,
                               QString* error_message);

} // namespace haorendergi
