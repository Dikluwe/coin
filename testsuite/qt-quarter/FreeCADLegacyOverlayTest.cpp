#include <Gui/GLPainter.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoSeparator.h>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static std::string record(SoNode* scene, int width = 400, int height = 300)
{
    auto* layer = new SoAnnotation;
    layer->ref();
    layer->addChild(scene);
    CoinRenderAction action;
    action.setViewportRegion(SbViewportRegion(width, height));
    action.apply(layer);
    require(action.getLastStatus() == CoinRenderAction::SUCCESS, "overlay extraction failed");
    std::string result = action.getRecordingLog().getString();
    layer->unref();
    return result;
}

int main()
{
    try {
        SoDB::init();
        CoinRenderAction::initClass();
        Gui::Rubberband rubber;
        require(!rubber.overlayScene(400, 300), "inactive rubberband visible");
        rubber.setWorking(true);
        rubber.setCoords(250, 200, 50, 40); // Reverse drag must remain valid.
        rubber.setColor(1, 0, 0, 0.4f);
        SoNode* first = rubber.overlayScene(400, 300);
        require(first && first == rubber.overlayScene(400, 300), "idle scene rebuilt");
        const auto filled = record(first);
        require(filled.find("transp=0.6000") != std::string::npos, "outline alpha lost");
        require(filled.find("linePattern=43690") != std::string::npos, "outline pattern lost");
        require(filled.find("depthTest=0 depthWrite=0") != std::string::npos, "overlay depth changed");
        require(filled.find("top=TRIANGLES") != std::string::npos, "missing rectangle fill");
        Gui::Rubberband copied = rubber;
        require(copied.overlayScene(400, 300) != first, "copy shares mutable cached scene");
        require(record(copied.overlayScene(400, 300)) == filled, "copy changed geometry");
        copied = rubber;
        require(record(copied.overlayScene(400, 300)) == filled, "assignment changed geometry");
        require(filled.find("draws count: 2") != std::string::npos, "missing rectangle outline");
        rubber.setLineStipple(false);
        require(record(rubber.overlayScene(400, 300)) != filled, "stipple invalidation missing");
        require(rubber.overlayScene(800, 600), "resize lost geometry");
        require(!rubber.overlayScene(0, 0), "invalid viewport emitted geometry");
        rubber.setWorking(false);
        require(!rubber.overlayScene(800, 600), "rubberband remained after release");

        Gui::Polyline poly;
        poly.setWorking(true);
        poly.addNode(QPoint(30, 40));
        require(!poly.overlayScene(400, 300), "single-node line should be empty");
        poly.addNode(QPoint(200, 40));
        poly.addNode(QPoint(200, 220));
        poly.setColor(0, 1, 0, 1);
        const auto closed = record(poly.overlayScene(400, 300));
        poly.setClosed(false);
        const auto open = record(poly.overlayScene(400, 300));
        require(open != closed, "closing segment unchanged");
        poly.setClosed(true);
        poly.setCloseStippled(true);
        require(record(poly.overlayScene(400, 300)) != closed, "stippled closure missing");
        poly.setLineWidth(5);
        require(record(poly.overlayScene(400, 300)).find("lineWidth=5.0000") != std::string::npos,
                "line width lost geometry");
        poly.popNode();
        require(poly.overlayScene(400, 300), "pop removed too much");
        poly.clear();
        require(!poly.overlayScene(400, 300), "clear retained old line");
        std::cout << "PASS legacy overlay extraction, styles, cache, resize and lifecycle\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
