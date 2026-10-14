#include <Gui/Flag.h>
#include <QApplication>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <type_traits>
#include <iostream>

static_assert(!std::is_base_of_v<QOpenGLWidget, Gui::Flag>);

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    Gui::Flag flag;
    QPalette palette = flag.palette();
    palette.setColor(QPalette::Window, QColor(230, 230, 230));
    palette.setColor(QPalette::Text, Qt::black);
    flag.setPalette(palette);
    flag.setText("Distance = 12.34");
    flag.setOrigin(SbVec3f(1, 2, 3));
    flag.resize(flag.sizeHint());
    flag.show();
    app.processEvents();
    QImage pixels(flag.size(), QImage::Format_ARGB32);
    pixels.fill(Qt::transparent);
    flag.render(&pixels);
    int dark = 0;
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
            const auto c = pixels.pixelColor(x, y);
            dark += c.red() < 80 && c.green() < 80 && c.blue() < 80 && c.alpha() > 200;
        }
    }
    if (dark < 10 || QOpenGLContext::currentContext() ||
        flag.getOrigin() != SbVec3f(1, 2, 3)) {
        std::cerr << "FAIL Flag raster label or no-GL contract\n";
        return 1;
    }
    std::cout << "PASS Flag text and origin on QWidget without a Qt GL context\n";
    return 0;
}
