#include <Gui/Inventor/So3DAnnotation.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoTranslation.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void box(SoSeparator* parent, const SbColor& color, float x, float z, float size)
{
    auto* part = new SoSeparator;
    auto* material = new SoMaterial;
    material->diffuseColor = color;
    part->addChild(material);
    auto* translation = new SoTranslation;
    translation->translation.setValue(x, 0, z);
    part->addChild(translation);
    auto* cube = new SoCube;
    cube->width = size;
    cube->height = size;
    cube->depth = 0.1f;
    part->addChild(cube);
    parent->addChild(part);
}

static SoCallbackAction::Response priority(void* data, SoCallbackAction* callback, const SoNode* node)
{
    auto* action = static_cast<CoinRenderAction*>(callback);
    if (callback->getCurPathCode() == SoAction::OFF_PATH) return SoCallbackAction::CONTINUE;
    if (action->deferAnnotation(node == data ? 10 : 0)) return SoCallbackAction::PRUNE;
    return SoCallbackAction::CONTINUE;
}

int main(int argc, char** argv)
{
    try {
        SoDB::init();
        CoinRenderAction::initClass();
        Gui::So3DAnnotation::initClass();
        auto* root = new SoSeparator;
        root->ref();
        auto* camera = new SoOrthographicCamera;
        camera->position.setValue(0, 0, 10);
        camera->nearDistance = 1;
        camera->farDistance = 30;
        camera->height = 8;
        root->addChild(camera);
        auto* light = new SoLightModel;
        light->model = SoLightModel::BASE_COLOR;
        root->addChild(light);
        auto* nearAnnotation = new Gui::So3DAnnotation;
        auto* nested = new Gui::So3DAnnotation;
        box(nested, SbColor(1, 0, 0), 0, 1, 2);
        nearAnnotation->addChild(nested);
        root->addChild(nearAnnotation);
        // Main geometry deliberately follows an annotation in graph order.
        box(root, SbColor(0, 0, 1), 0, 3, 6);
        auto* farAnnotation = new Gui::So3DAnnotation;
        box(farAnnotation, SbColor(0, 1, 0), 0, 0, 4);
        root->addChild(farAnnotation);
        auto* foreground = new SoSeparator;
        auto* begin = new SoCallback;
        begin->setCallback([](void*, SoAction* a) {
            static_cast<CoinRenderAction*>(a)->beginForegroundPass();
        });
        auto* end = new SoCallback;
        end->setCallback([](void*, SoAction* a) {
            static_cast<CoinRenderAction*>(a)->endForegroundPass();
        });
        foreground->addChild(begin);
        box(foreground, SbColor(1, 1, 0), -1.5f, 4, 1);
        auto* ordinary = new SoAnnotation;
        box(ordinary, SbColor(0, 1, 1), -2.5f, -5, 0.5f);
        foreground->addChild(ordinary);
        foreground->addChild(end);
        root->addChild(foreground);
        CoinRenderAction action(SbViewportRegion(128, 128));
        require(!action.deferAnnotation(), "deferred outside traversal");
        action.apply(root);
        require(action.getLastStatus() == CoinRenderAction::SUCCESS, "recording failed");
        const std::string log = action.getRecordingLog().getString();
        size_t clears = 0, cursor = 0;
        while ((cursor = log.find("layer=1 clearDepthBefore=1", cursor)) != std::string::npos) {
            ++clears; ++cursor;
        }
        require(clears == 1, "3D annotations must share exactly one depth clear");
        require(log.find("layer=1 clearDepthBefore=0") != std::string::npos,
                "second annotation not in shared pass");
        require(log.find("layer=2 clearDepthBefore=0") != std::string::npos,
                "foreground did not follow 3D annotation pass");
        const std::string red = "diff=[1.0000,0.0000,0.0000,1.0000]";
        const std::string green = "diff=[0.0000,1.0000,0.0000,1.0000]";
        require(log.find(red) < log.find(green), "equal priority changed traversal order");
        CoinRenderAction prioritized(SbViewportRegion(128, 128));
        prioritized.addPreCallback(Gui::So3DAnnotation::getClassTypeId(), priority, nearAnnotation);
        prioritized.apply(root);
        require(prioritized.getLastStatus() == CoinRenderAction::SUCCESS, "priority recording failed");
        const std::string ordered = prioritized.getRecordingLog().getString();
        require(ordered.find(green) < ordered.find(red), "priority did not sort delayed paths");
        require(log.find("layer=3 clearDepthBefore=1") != std::string::npos,
                "ordinary foreground annotation lost its own layer");
        require(!Gui::So3DAnnotation::render, "BGFX changed global GL replay state");
        if (argc > 1 && std::string(argv[1]) == "--gpu") {
            std::unique_ptr<CoinRenderTarget> target(
                CoinRenderTarget::createOffscreen(SbVec2i32(128, 128)));
            require(target && target->getStatus() == CoinRenderTarget::TARGET_READY, "GPU unavailable");
            target->setDepthReadbackEnabled(FALSE);
            action.setRenderTarget(target.get());
            action.apply(root);
            require(action.getLastStatus() == CoinRenderAction::SUCCESS, "GPU submit failed");
            std::vector<uint8_t> pixels;
            target->readbackRGBA(pixels);
            require(pixels.size() == 128*128*4, "readback missing");
            auto pixel = [&](int x, int y, int r, int g, int b) {
                size_t i = (y*128+x)*4;
                if (std::abs(int(pixels[i])-r) >= 20 || std::abs(int(pixels[i+1])-g) >= 20 ||
                    std::abs(int(pixels[i+2])-b) >= 20)
                    std::cerr << "pixel " << x << "," << y << " got "
                              << int(pixels[i]) << "," << int(pixels[i+1]) << "," << int(pixels[i+2])
                              << " expected " << r << "," << g << "," << b << "\\n";
                require(std::abs(int(pixels[i])-r) < 20 && std::abs(int(pixels[i+1])-g) < 20 &&
                        std::abs(int(pixels[i+2])-b) < 20, "annotation depth/layer pixel mismatch");
            };
            pixel(64, 64, 255, 0, 0); // near red survives later far green
            pixel(86, 64, 0, 255, 0); // annotations cover nearer main scene
            pixel(105, 64, 0, 0, 255);
            pixel(24, 64, 0, 255, 255);
            pixel(40, 64, 255, 255, 0); // ordinary foreground covers 3D set
            auto* farPart = static_cast<SoSeparator*>(farAnnotation->getChild(0));
            static_cast<SoMaterial*>(farPart->getChild(0))->transparency = 0.5f;
            action.apply(root);
            require(action.getLastStatus() == CoinRenderAction::SUCCESS, "transparent annotation submit failed");
            target->readbackRGBA(pixels);
            pixel(64, 64, 255, 0, 0);
            pixel(86, 64, 0, 128, 128);
            action.setRenderTarget(nullptr);
        }
        auto* path = new SoPath(root);
        path->ref();
        path->append(root->findChild(farAnnotation));
        CoinRenderAction pathAction(SbViewportRegion(128, 128));
        pathAction.apply(path);
        path->unref();
        require(pathAction.getLastStatus() == CoinRenderAction::SUCCESS, "annotation path replay failed");
        const std::string pathLog = pathAction.getRecordingLog().getString();
        require(pathLog.find("diff=[0.0000,1.0000,0.0000,") != std::string::npos && pathLog.find(red) == std::string::npos,
                "off-path annotation rendered during path replay");
        root->removeChild(nearAnnotation);
        root->removeChild(farAnnotation);
        action.apply(root);
        require(std::string(action.getRecordingLog().getString()).find("layer=3") == std::string::npos,
                "annotation queue leaked into next frame");
        root->unref();
        std::cout << "PASS shared 3D annotation depth, foreground order and frame lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
