#pragma once

#include <QHash>
#include <QString>

namespace haorendergi {

bool loadGltfVrmHumanoidBones(const QString& path,
                              QHash<QString, QString>* node_name_to_human_bone,
                              QString* error_message = nullptr);

} // namespace haorendergi
