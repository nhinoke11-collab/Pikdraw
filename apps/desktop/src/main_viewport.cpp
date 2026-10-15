#include "pikdraw/rendering/fixtures.hpp"
#include "pikdraw/desktop/viewport_widget.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMainWindow>
#include <QSaveFile>
#include <QDebug>
#include <sstream>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption trace_option(
        QStringLiteral("trace-out"),
        QStringLiteral("Write the bounded viewport CPU trace as JSON when the window closes."),
        QStringLiteral("file")
    );
    parser.addOption(trace_option);
    parser.process(app);

    QMainWindow window;
    window.setWindowTitle("PikDraw Spike A - Viewport Prototype (Qt + Skia Raster)");
    window.resize(1280, 800);

    auto* viewport = new pikdraw::desktop::PikDrawViewportWidget(&window);
    viewport->load_scene(pikdraw::rendering::FixtureGenerator::generate_many_small_paths(25000));
    window.setCentralWidget(viewport);
    window.show();

    const int app_exit_code = app.exec();
    if (parser.isSet(trace_option)) {
        std::ostringstream json;
        viewport->tracer().export_json(json);
        const QByteArray data = QByteArray::fromStdString(json.str());
        QSaveFile trace_file(parser.value(trace_option));
        if (!trace_file.open(QIODevice::WriteOnly) ||
            trace_file.write(data) != data.size() ||
            !trace_file.commit()) {
            qCritical() << "Failed to save viewport trace:" << trace_file.errorString();
            return 2;
        }
    }
    return app_exit_code;
}
