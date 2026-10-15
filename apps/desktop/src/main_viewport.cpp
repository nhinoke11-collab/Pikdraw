#include "pikdraw/rendering/fixtures.hpp"
#include "pikdraw/desktop/viewport_widget.hpp"
#include <QApplication>
#include <QMainWindow>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle("PikDraw Spike A - Viewport Prototype (Qt + Skia Raster)");
    window.resize(1280, 800);

    auto* viewport = new pikdraw::desktop::PikDrawViewportWidget(&window);
    viewport->load_scene(pikdraw::rendering::FixtureGenerator::generate_many_small_paths(25000));
    window.setCentralWidget(viewport);
    window.show();

    return app.exec();
}
