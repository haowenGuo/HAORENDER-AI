#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace haorendergi {

struct NativeExportFormat {
    QString id;
    QString description;
    QString extension;
};

class NativeModelConverter {
public:
    QVector<NativeExportFormat> availableFormats() const;
    QStringList supportedTargetExtensions() const;

    bool canExportExtension(const QString& extension) const;
    bool convert(const QString& source_path,
                 const QString& output_path,
                 const QString& target_extension,
                 QString* status_message = nullptr) const;

private:
    QString resolveFormatId(const QString& target_extension,
                            QString* available_formats_text = nullptr) const;
};

} // namespace haorendergi
