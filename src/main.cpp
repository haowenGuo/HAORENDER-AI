#include "app/main_window.h"

#include <QApplication>
#include <QFont>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <QSurfaceFormat>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

void applyStartupGeometry(QWidget& window) {
    QScreen* screen = window.screen() ? window.screen() : QApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(40, 40, 1360, 900);
    constexpr int preferred_width = 1360;
    constexpr int preferred_height = 900;
    constexpr int screen_margin = 48;
    const int usable_width = std::max(360, available.width() - screen_margin);
    const int usable_height = std::max(320, available.height() - screen_margin);
    const int width = std::min(preferred_width, usable_width);
    const int height = std::min(preferred_height, usable_height);
    const int x = available.x() + std::max(0, (available.width() - width) / 2);
    const int y = available.y() + std::max(0, (available.height() - height) / 2);

    window.showNormal();
    window.resize(width, height);
    window.move(x, y);
    window.raise();
    window.activateWindow();
}

} // namespace

int main(int argc, char** argv) {
    QStringList raw_arguments;
    raw_arguments.reserve(std::max(argc - 1, 0));
    for (int i = 1; i < argc; ++i) {
        raw_arguments.push_back(QString::fromLocal8Bit(argv[i]));
    }

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QSurfaceFormat format;
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));

    QString startup_model_path;
    QString startup_style_preset_path;
    QString startup_source_animation_path;
    bool open_rig_ai = false;
    haorendergi::RenderPipeline initial_pipeline = haorendergi::RenderPipeline::Raster;
    for (int i = 0; i < raw_arguments.size(); ++i) {
        const QString argument = raw_arguments[i].trimmed();
        if (argument == QStringLiteral("--raytrace")) {
            initial_pipeline = haorendergi::RenderPipeline::RayTrace;
        } else if (argument == QStringLiteral("--dxr")) {
            initial_pipeline = haorendergi::RenderPipeline::DxrRayTrace;
        } else if (argument == QStringLiteral("--rig-ai")) {
            open_rig_ai = true;
        } else if (argument == QStringLiteral("--style-preset") && i + 1 < raw_arguments.size()) {
            startup_style_preset_path = raw_arguments[++i];
        } else if (argument.startsWith(QStringLiteral("--style-preset="))) {
            startup_style_preset_path = argument.mid(QStringLiteral("--style-preset=").size());
        } else if (argument == QStringLiteral("--source-animation") && i + 1 < raw_arguments.size()) {
            startup_source_animation_path = raw_arguments[++i];
        } else if (argument.startsWith(QStringLiteral("--source-animation="))) {
            startup_source_animation_path = argument.mid(QStringLiteral("--source-animation=").size());
        } else {
            startup_model_path = argument;
        }
    }

    haorendergi::MainWindow window(startup_model_path, initial_pipeline, startup_style_preset_path, startup_source_animation_path);
    window.show();
    QTimer::singleShot(0, &window, [&window]() {
        applyStartupGeometry(window);
    });
    if (open_rig_ai) {
        QTimer::singleShot(500, &window, [&window]() { window.openRigAiWindow(); });
    }
    return app.exec();
}
