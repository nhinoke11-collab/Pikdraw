#include "pikdraw/rendering/fixtures.hpp"
#include "pikdraw/desktop/viewport_widget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMainWindow>
#include <QSaveFile>
#include <QTimer>
#include <QDebug>
#include <cmath>
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
    const QCommandLineOption nodes_option(
        QStringLiteral("nodes"),
        QStringLiteral("Number of small vector paths in the viewport fixture (1–250000)."),
        QStringLiteral("count"),
        QStringLiteral("25000")
    );
    const QCommandLineOption spacing_option(
        QStringLiteral("spacing-mm"),
        QStringLiteral("Grid spacing in millimeters for the path fixture (4–10000)."),
        QStringLiteral("millimeters"),
        QStringLiteral("4.0")
    );
    const QCommandLineOption synthetic_pan_option(
        QStringLiteral("synthetic-pan-ms"),
        QStringLiteral("Run synthetic drag events for this duration; trace remains CPU-only."),
        QStringLiteral("duration-ms")
    );
    parser.addOption(trace_option);
    parser.addOption(nodes_option);
    parser.addOption(spacing_option);
    parser.addOption(synthetic_pan_option);
    parser.process(app);

    bool nodes_ok = false;
    const int node_count = parser.value(nodes_option).toInt(&nodes_ok);
    if (!nodes_ok || node_count < 1 || node_count > 250000) {
        qCritical() << "--nodes must be an integer from 1 to 250000";
        return 2;
    }

    bool spacing_ok = false;
    const double spacing_mm = parser.value(spacing_option).toDouble(&spacing_ok);
    if (!spacing_ok || !std::isfinite(spacing_mm) || spacing_mm < 4.0 || spacing_mm > 10000.0) {
        qCritical() << "--spacing-mm must be a finite number from 4 to 10000";
        return 2;
    }

    int synthetic_pan_ms = 0;
    if (parser.isSet(synthetic_pan_option)) {
        bool duration_ok = false;
        synthetic_pan_ms = parser.value(synthetic_pan_option).toInt(&duration_ok);
        if (!duration_ok || synthetic_pan_ms < 100 || synthetic_pan_ms > 60000) {
            qCritical() << "--synthetic-pan-ms must be an integer from 100 to 60000";
            return 2;
        }
    }

    QMainWindow window;
    window.setWindowTitle("PikDraw Spike A - Viewport Prototype (Qt + Skia Raster)");
    window.resize(1280, 800);

    auto* viewport = new pikdraw::desktop::PikDrawViewportWidget(&window);
    viewport->load_scene(pikdraw::rendering::FixtureGenerator::generate_many_small_paths(
        static_cast<size_t>(node_count), spacing_mm
    ));
    window.setCentralWidget(viewport);
    window.show();

    QTimer synthetic_pan_timer;
    QElapsedTimer synthetic_pan_clock;
    QPoint last_pointer_position(window.width() / 2, window.height() / 2);
    bool synthetic_pointer_pressed = false;
    if (synthetic_pan_ms > 0) {
        synthetic_pan_clock.start();
        QObject::connect(&synthetic_pan_timer, &QTimer::timeout, viewport, [&] {
            if (!synthetic_pointer_pressed) {
                QMouseEvent press(
                    QEvent::MouseButtonPress,
                    QPointF(last_pointer_position), QPointF(last_pointer_position),
                    QPointF(last_pointer_position), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier
                );
                QApplication::sendEvent(viewport, &press);
                synthetic_pointer_pressed = true;
            }

            if (synthetic_pan_clock.elapsed() >= synthetic_pan_ms) {
                QMouseEvent release(
                    QEvent::MouseButtonRelease,
                    QPointF(last_pointer_position), QPointF(last_pointer_position),
                    QPointF(last_pointer_position), Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier
                );
                QApplication::sendEvent(viewport, &release);
                synthetic_pan_timer.stop();
                app.quit();
                return;
            }

            const int tick = static_cast<int>(synthetic_pan_clock.elapsed() / 16);
            const QPoint delta = (tick % 2 == 0) ? QPoint(3, -2) : QPoint(-2, 3);
            last_pointer_position += delta;
            QMouseEvent move(
                QEvent::MouseMove,
                QPointF(last_pointer_position), QPointF(last_pointer_position),
                QPointF(last_pointer_position), Qt::NoButton, Qt::LeftButton,
                Qt::NoModifier
            );
            QApplication::sendEvent(viewport, &move);
        });
        synthetic_pan_timer.start(16);
    }

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
