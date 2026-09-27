#include <Gui/GLPainter.h>
#include <QImage>
#include <QOpenGLContext>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoSeparator.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try {
        SoDB::init();
        SoWgpuRenderAction::initClass();
        auto* root = new SoAnnotation;
        root->ref();
        auto* scene = new SoSeparator;
        root->addChild(scene);
        Gui::GLPainter painter;
        require(!painter.end(), "inactive end succeeded");
        require(!painter.begin(scene, 0, 300), "empty viewport accepted");
        require(!painter.begin(static_cast<QPaintDevice*>(nullptr), scene), "null device accepted");
        QImage device(400, 300, QImage::Format_RGB32);
        require(painter.begin(&device, scene), "retained begin failed without GL widget");
        require(painter.isActive() && !QOpenGLContext::currentContext(), "GL context required");
        require(!painter.begin(scene, 400, 300), "nested begin accepted");
        painter.setColor(0, 0, 1, 1);
        painter.setLineWidth(5);
        painter.setLineStipple(2, 0xAAAA);
        painter.drawLine(40, 60, 200, 60);
        painter.resetLineStipple();
        painter.setColor(0, 1, 0, 1);
        painter.setPointSize(11);
        painter.drawPoint(260, 100);
        painter.setColor(1, 0, 0, 0.5f);
        painter.setLineWidth(3);
        painter.drawRect(50, 140, 200, 210);
        require(painter.end() && !painter.isActive(), "end retained session");
        require(scene->getNumChildren() == 3, "primitive insertion order/count changed");
        painter.drawPoint(0, 0);
        require(scene->getNumChildren() == 3, "drawing outside session modified scene");
        SoWgpuRenderAction action;
        action.setViewportRegion(SbViewportRegion(400, 300));
        action.apply(root);
        require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "recording failed");
        std::string log = action.getRecordingLog().getString();
        require(log.find("transp=0.5000") != std::string::npos, "per-primitive alpha lost");
        require(log.find("pointSize=11.0000") != std::string::npos, "point size lost");
        require(log.find("linePattern=43690") != std::string::npos, "line stipple lost");
        require(log.find("depthTest=0 depthWrite=0") != std::string::npos, "depthless overlay lost");
        if (argc > 1 && std::string(argv[1]) == "--gpu") {
            std::unique_ptr<SoWgpuRenderTarget> target(
                SoWgpuRenderTarget::createOffscreen(SbVec2i32(400, 300)));
            require(target && target->getStatus() == SoWgpuRenderTarget::TARGET_READY,
                    "GPU target unavailable");
            target->setDepthReadbackEnabled(FALSE);
            action.setRenderTarget(target.get());
            for (auto mode : {SoWgpuRenderAction::BLEND, SoWgpuRenderAction::SORTED_OBJECT_BLEND}) {
                action.setTransparencyType(mode);
                action.apply(root);
                require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "GPU submit failed");
                std::vector<uint8_t> pixels;
                target->readbackRGBA(pixels);
                require(pixels.size() == 400*300*4, "GPU readback missing");
                int blue = 0, green = 0, translucentRed = 0;
                for (size_t i = 0; i < pixels.size(); i += 4) {
                    const int r = pixels[i], g = pixels[i+1], b = pixels[i+2];
                    blue += b > 180 && r < 60 && g < 60;
                    green += g > 180 && r < 60 && b < 60;
                    translucentRed += r > 80 && r < 220 && g < 60 && b < 60;
                }
                require(blue > 100 && blue < 700, "stippled line missing or became solid on GPU");
                require(green > 40, "sized point missing on GPU");
                require(translucentRed > 100, "transparent rectangle missing on GPU");
                require(!QOpenGLContext::currentContext(), "retained painter acquired Qt GL context");
            }
            scene->removeAllChildren();
            action.apply(root);
            require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "empty overlay submit failed");
            std::vector<uint8_t> cleared;
            target->readbackRGBA(cleared);
            require(cleared.size() == 400*300*4, "cleared frame readback missing");
            for (size_t i = 0; i < cleared.size(); i += 4) {
                require(cleared[i] < 10 && cleared[i+1] < 10 && cleared[i+2] < 10,
                        "removed primitives remained on GPU");
            }
            action.setRenderTarget(nullptr);
        }
        root->unref();
        std::cout << "PASS GLPainter retained session, styles, alpha, lifecycle and no Qt GL context\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
