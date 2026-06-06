#include "rigging/rig_mapping_exporter.h"

#include <QFile>
#include <QJsonDocument>

namespace haorendergi {

bool RigMappingExporter::writeMapping(const QString& path,
                                      const SkeletonGraph& source,
                                      const SkeletonGraph& target,
                                      const BoneMappingResult& result,
                                      QString* error_message) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to open mapping file for writing: %1").arg(path);
        }
        return false;
    }

    const QJsonDocument document(boneMappingResultToJson(source, target, result));
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to write mapping file: %1").arg(path);
        }
        return false;
    }
    if (error_message) {
        error_message->clear();
    }
    return true;
}

} // namespace haorendergi
