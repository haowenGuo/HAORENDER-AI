#pragma once

#include "app/render_viewport.h"
#include "rigging/skeleton_types.h"
#include "scene/model_loader.h"
#include "scene/scene_types.h"

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace haorendergi {

class AssetConverterWindow final : public QDialog {
public:
    explicit AssetConverterWindow(QWidget* parent = nullptr, bool embedded = false);

    void loadSourcePath(const QString& path);
    void setChineseUi(bool enabled);

private:
    enum class TargetFormat {
        Vrm = 0,
        Pmx = 1,
        Glb = 2,
        Gltf = 3,
        Fbx = 4,
        Obj = 5,
        Dae = 6,
        Stl = 7,
        Ply = 8
    };

    void buildUi();
    void chooseSource();
    void chooseOutputDirectory();
    void generateConversionPlan();
    void executeConversion();
    void saveReport();
    void refreshLanguage();
    void refreshSourceSummary();
    void refreshTargetUi();
    void refreshEmptyState();
    void refreshTargetComboText();
    void writePlanAndReport(const QString& output_directory,
                            const QString& output_model_path,
                            const QString& status,
                            const QString& backend_note) const;
    void writeBackendTaskPackage(const QString& output_directory,
                                 const QString& output_model_path,
                                 const QString& status,
                                 const QString& backend_note) const;
    bool runNativeGlbConversion(const QString& output_model_path,
                                QString* backend_note);
    bool runNativeSimpleModelConversion(const QString& output_model_path,
                                        QString* backend_note);
    bool runNativeVrmConversion(const QString& output_model_path,
                                QString* backend_note);
    bool runBlenderGlbConversion(const QString& output_model_path,
                                 QString* backend_note);

    TargetFormat targetFormat() const;
    QString targetFormatName() const;
    QString targetExtension() const;
    QString defaultOutputDirectory() const;
    QString buildInventoryText() const;
    QString buildPlanText() const;
    QString buildReportText(const QString& status = QString(),
                            const QString& output_model_path = QString(),
                            const QString& backend_note = QString()) const;
    QString buildBackendReadme(const QString& output_model_path,
                               const QString& status,
                               const QString& backend_note) const;
    QString buildBlenderBridgeScript() const;
    QString buildRunConversionScript(const QString& output_model_path) const;
    QJsonObject buildPlanJson(const QString& status = QString(),
                              const QString& output_model_path = QString(),
                              const QString& backend_note = QString()) const;
    QString findBlenderExecutable() const;

    bool embedded_ = false;
    bool chinese_ui_ = false;
    QString source_path_;
    QString output_directory_;
    QString last_output_model_path_;
    QString last_status_;
    QString last_backend_note_;
    SceneModel source_scene_;
    SceneModel converted_scene_;
    SkeletonGraph source_skeleton_;
    ModelLoader model_loader_;

    QLabel* title_label_ = nullptr;
    QLabel* subtitle_label_ = nullptr;
    QLabel* before_label_ = nullptr;
    QLabel* after_label_ = nullptr;
    QLineEdit* source_path_edit_ = nullptr;
    QLineEdit* output_dir_edit_ = nullptr;
    QComboBox* target_combo_ = nullptr;
    QPushButton* import_button_ = nullptr;
    QPushButton* output_button_ = nullptr;
    QPushButton* plan_button_ = nullptr;
    QPushButton* execute_button_ = nullptr;
    QPushButton* report_button_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTextBrowser* inventory_view_ = nullptr;
    QTextBrowser* plan_view_ = nullptr;
    QTextBrowser* report_view_ = nullptr;
    RenderViewport* before_viewport_ = nullptr;
    RenderViewport* after_viewport_ = nullptr;
};

} // namespace haorendergi
