// Test-only bridge: no production Python API or viewer default is changed.
#include <Gui/Flag.h>
#include <Gui/View3DInventorViewer.h>
#include <Inventor/SoRenderManager.h>
#include <QPointer>
#include <memory>

static QPointer<Gui::View3DInventorViewer> viewer;
static std::unique_ptr<Gui::GLFlagWindow> item;

extern "C" int flag_test_clear()
{
    if (viewer && item) {
        viewer->removeGraphicsItem(item.get());
        item.reset();
        viewer->getSoRenderManager()->scheduleRedraw();
    }
    return 1;
}

extern "C" int flag_test_create(void* widget)
{
    flag_test_clear();
    viewer = dynamic_cast<Gui::View3DInventorViewer*>(static_cast<QWidget*>(widget));
    if (!viewer) return 0;
    item = std::make_unique<Gui::GLFlagWindow>(viewer);
    for (int i = 0; i < 2; ++i) {
        auto* flag = new Gui::Flag;
        flag->setText(i ? "Flag B" : "Flag A");
        flag->setFixedWidth(140);
        flag->setOrigin(SbVec3f(i ? 10 : 0, 0, 0));
        auto palette = flag->palette();
        palette.setColor(QPalette::Window, QColor(240, 210, 60));
        palette.setColor(QPalette::Text, Qt::black);
        flag->setPalette(palette);
        item->addFlag(flag, i ? Gui::FlagLayout::BottomRight : Gui::FlagLayout::TopLeft);
    }
    viewer->addGraphicsItem(item.get());
    viewer->getSoRenderManager()->scheduleRedraw();
    return 1;
}

extern "C" int flag_test_update()
{
    if (!viewer || !item) return 0;
    auto* flag = item->getFlag(0);
    flag->move(flag->pos() + QPoint(40, 60));
    flag->setOrigin(SbVec3f(0, 10, 10));
    // No explicit redraw: the production widget must invalidate its connectors.
    return 1;
}

extern "C" int flag_test_hide()
{
    if (!viewer || !item) return 0;
    item->getFlag(0)->hide();
    return 1;
}
