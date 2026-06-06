#include "scene/vrm_expression_controller.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace haorendergi {
namespace {

bool sameExpressionName(const std::string& lhs, const std::string& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

} // namespace

const VrmExpressionData* findVrmExpression(const SceneModel& scene, const std::string& name) {
    const auto found = std::find_if(scene.vrm_expressions.begin(), scene.vrm_expressions.end(), [&](const VrmExpressionData& expression) {
        return sameExpressionName(expression.name, name);
    });
    return found != scene.vrm_expressions.end() ? &(*found) : nullptr;
}

bool setVrmExpressionWeight(SceneModel* scene, const std::string& name, float weight) {
    if (!scene || !findVrmExpression(*scene, name)) {
        return false;
    }

    const float clamped_weight = std::clamp(weight, 0.0f, 1.0f);
    auto found = std::find_if(scene->expression_weights.begin(), scene->expression_weights.end(), [&](const ExpressionWeightData& item) {
        return sameExpressionName(item.name, name);
    });
    if (found == scene->expression_weights.end()) {
        if (clamped_weight <= 1e-5f) {
            return true;
        }
        scene->expression_weights.push_back(ExpressionWeightData{ name, clamped_weight });
        return true;
    }

    if (clamped_weight <= 1e-5f) {
        scene->expression_weights.erase(found);
    } else {
        found->weight = clamped_weight;
    }
    return true;
}

void clearVrmExpressionWeights(SceneModel* scene) {
    if (!scene) {
        return;
    }
    scene->expression_weights.clear();
}

std::vector<float> baseMorphWeightsForMesh(const MeshData& mesh) {
    if (!mesh.morph_default_weights.empty()) {
        return mesh.morph_default_weights;
    }
    if (!mesh.morph_weights.empty()) {
        return mesh.morph_weights;
    }
    return std::vector<float>(mesh.morph_targets.size(), 0.0f);
}

void applyVrmExpressionWeightsToMesh(const SceneModel& scene,
                                     const MeshData& mesh,
                                     const std::vector<ExpressionWeightData>& expression_weights,
                                     std::vector<float>* morph_weights) {
    if (!morph_weights || expression_weights.empty() || scene.vrm_expressions.empty()) {
        return;
    }
    if (morph_weights->size() < mesh.morph_targets.size()) {
        morph_weights->resize(mesh.morph_targets.size(), 0.0f);
    }

    for (const ExpressionWeightData& item : expression_weights) {
        const float expression_weight = std::clamp(item.weight, 0.0f, 1.0f);
        if (expression_weight <= 1e-5f) {
            continue;
        }

        const VrmExpressionData* expression = findVrmExpression(scene, item.name);
        if (!expression) {
            continue;
        }

        for (const VrmMorphTargetBindData& bind : expression->morph_target_binds) {
            if (bind.node_index != mesh.node_index ||
                bind.morph_target_index < 0 ||
                bind.morph_target_index >= static_cast<int>(morph_weights->size())) {
                continue;
            }
            float& target_weight = (*morph_weights)[bind.morph_target_index];
            target_weight = std::clamp(target_weight + expression_weight * bind.weight, 0.0f, 1.0f);
        }
    }
}

} // namespace haorendergi
