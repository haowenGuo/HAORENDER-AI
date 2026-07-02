#pragma once

#include "scene/scene_types.h"

#include <string>
#include <vector>

namespace haorendergi {

const VrmExpressionData* findVrmExpression(const SceneModel& scene, const std::string& name);
bool setVrmExpressionWeight(SceneModel* scene, const std::string& name, float weight);
void clearVrmExpressionWeights(SceneModel* scene);
std::vector<float> baseMorphWeightsForMesh(const MeshData& mesh);
void applyVrmExpressionWeightsToMesh(const SceneModel& scene,
                                     const MeshData& mesh,
                                     const std::vector<ExpressionWeightData>& expression_weights,
                                     std::vector<float>* morph_weights);

} // namespace haorendergi
