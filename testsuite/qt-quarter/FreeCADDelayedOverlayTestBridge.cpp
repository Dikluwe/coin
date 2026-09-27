// Runs inside FreeCAD so its preference and selection infrastructure is real.
// No production Python API, Qt GL context or onscreen capture is needed.
#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/ViewProvider.h>
#include <Gui/ViewParams.h>
#include <Gui/Selection/SelectionView.h>
#include <Gui/Selection/Selection.h>
#include <QTimer>
#include <QApplication>
#include <QKeyEvent>
#include <Gui/Inventor/SoDrawingGrid.h>
#include <Gui/Inventor/So3DAnnotation.h>
#include <Gui/Selection/SoFCUnifiedSelection.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoVertexProperty.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoTranslation.h>
#include <QImage>
#include <QFile>
#include <QOpenGLContext>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

static std::string error;
static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static SoSeparator* box(const SbColor& color, float x, float z, float size)
{
    auto* group = new SoSeparator;
    auto* material = new SoMaterial;
    material->diffuseColor = color;
    group->addChild(material);
    auto* translation = new SoTranslation;
    translation->translation.setValue(x, 0, z);
    group->addChild(translation);
    auto* cube = new SoCube;
    cube->width = size;
    cube->height = size;
    cube->depth = 0.1f;
    group->addChild(cube);
    return group;
}

extern "C" const char* delayed_overlay_error() { return error.c_str(); }

extern "C" int delayed_overlay_test(const char* artifacts)
{
    try {
        error.clear();
        const auto* initialQtContext = QOpenGLContext::currentContext();
        SoWgpuRenderAction::initClass();
        auto* root = new SoSeparator;
        root->ref();
        auto* camera = new SoOrthographicCamera;
        camera->position.setValue(0, 0, 10);
        camera->height = 8;
        camera->nearDistance = 1;
        camera->farDistance = 30;
        root->addChild(camera);
        auto* lighting = new SoLightModel;
        lighting->model = SoLightModel::BASE_COLOR;
        root->addChild(lighting);
        auto* grid = new Gui::Inventor::SoDrawingGrid;
        root->addChild(grid);
        root->addChild(box(SbColor(0, 0, 1), 0, 3, 6));

        auto* source = new Gui::SoFCSelectionRoot;
        auto* selected = box(SbColor(1, 0, 0), 0, -2, 2);
        selected->ref();
        source->addChild(selected);
        source->addChild(box(SbColor(0, 1, 0), -2.5f, -2, 1));
        auto* path = new SoPath(source);
        path->ref();
        path->append(0);
        auto* annotation = new Gui::SoFCPathAnnotation;
        annotation->setPath(path);
        path->unref();
        root->addChild(annotation);
        auto* annotation3D = new Gui::So3DAnnotation;
        annotation3D->addChild(box(SbColor(0, 1, 0), 0, -5, 1));
        root->addChild(annotation3D);

        SoWgpuRenderAction action(SbViewportRegion(128, 128));
        action.setBackgroundColor(SbColor4f(1, 1, 1, 1));
        action.setTransparencyType(SoWgpuRenderAction::SORTED_OBJECT_BLEND);
        action.apply(root);
        require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "recording failed");
        std::string log = action.getRecordingLog().getString();
        require(log.find("layer=1 clearDepthBefore=0") != std::string::npos,
                "normal delayed queue must not clear depth");
        require(log.find("layer=2 clearDepthBefore=1") != std::string::npos,
                "3D queue must follow normal delayed overlays");
        require(log.find("depthTest=0 depthWrite=0") != std::string::npos,
                "grid/path annotations must disable depth");
        auto* gridLines = static_cast<SoLineSet*>(grid->getChild(1));
        require(gridLines->numVertices.getNum() == 80, "square grid geometry missing");

        std::unique_ptr<SoWgpuRenderTarget> target(
            SoWgpuRenderTarget::createOffscreen(SbVec2i32(128, 128)));
        require(target && target->getStatus() == SoWgpuRenderTarget::TARGET_READY, "GPU unavailable");
        target->setDepthReadbackEnabled(FALSE);
        action.setRenderTarget(target.get());
        std::vector<uint8_t> pixels;
        auto submit = [&](const char* name) {
            action.apply(root);
            require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "GPU submit failed");
            target->readbackRGBA(pixels);
            const auto size = target->getSize();
            require(pixels.size() == size[0]*size[1]*4, "readback missing");
            QImage image(pixels.data(), size[0], size[1], QImage::Format_RGBA8888);
            require(image.save(QString::fromUtf8(artifacts) + "/" + name + ".png"), "PNG save failed");
        };
        auto pixel = [&](int x, int y, int r, int g, int b) {
            size_t i = (y*target->getSize()[0]+x)*4;
            if (std::abs(int(pixels[i])-r) >= 20 || std::abs(int(pixels[i+1])-g) >= 20 ||
                std::abs(int(pixels[i+2])-b) >= 20)
                throw std::runtime_error("pixel " + std::to_string(x) + "," + std::to_string(y) +
                    " got " + std::to_string(pixels[i]) + "," + std::to_string(pixels[i+1]) +
                    "," + std::to_string(pixels[i+2]) + " expected " + std::to_string(r) +
                    "," + std::to_string(g) + "," + std::to_string(b));
            require(std::abs(int(pixels[i])-r) < 20 && std::abs(int(pixels[i+1])-g) < 20 &&
                    std::abs(int(pixels[i+2])-b) < 20, "delayed overlay pixel mismatch");
        };
        auto patchContainsBlue = [&](int x, int y) {
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    size_t i = ((y+dy)*target->getSize()[0]+x+dx)*4;
                    if (pixels[i] < 20 && pixels[i+1] < 20 && pixels[i+2] > 235) return;
                }
            throw std::runtime_error("blue scene absent in grid intersection patch");
        };
        submit("combined");
        pixel(64, 64, 0, 255, 0); // 3D annotations come after grid/path overlays
        pixel(76, 63, 255, 0, 0); // selected path covers nearer blue scene
        patchContainsBlue(24, 62); // unselected sibling must not be rendered

        root->removeChild(annotation3D);
        submit("path");
        pixel(64, 64, 255, 0, 0);
        std::vector<uint8_t> before = pixels;
        // An auditing path truncates on removal; the unaudited path can restore it.
        source->removeChild(selected);
        source->insertChild(selected, 1);
        submit("restored-path");
        require(pixels == before, "restored path changed selected geometry");
        require(annotation->getPath()->getLength() == 2, "truncated path not restored");

        source->selectionStyle = Gui::SoFCSelectionRoot::Box;
        submit("bbox");
        int greenOutline = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            greenOutline += pixels[i] < 20 && pixels[i+1] > 235 && pixels[i+2] < 20;
        require(greenOutline > 100, "bounding box outline absent");
        patchContainsBlue(65, 65); // bounding box must be an outline, not filled
        action.setRenderTarget(nullptr);
        action.apply(root);
        require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, action.getLastError().getString());
        log = action.getRecordingLog().getString();
        QFile planFile(QString::fromUtf8(artifacts) + "/bbox-plan.txt");
        require(planFile.open(QIODevice::WriteOnly), "plan file open failed");
        planFile.write(log.data(), log.size());
        planFile.close();
        require(log.find("offsetPrimitiveStyle=2") != std::string::npos, "bbox/grid line primitives missing");
        require(log.find("diff=[1.0000,0.0000,0.0000,") == std::string::npos,
                "bbox mode rendered the filled selected shape");
        action.setRenderTarget(target.get());

        std::vector<bool> bboxMask(pixels.size()/4);
        for (size_t i=0; i<pixels.size(); i+=4)
            bboxMask[i/4] = pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20;
        for (int clipping=0; clipping<3; ++clipping) {
            camera->nearDistance = clipping==0 ? 13 : clipping==2 ? 12 : 1;
            camera->farDistance = clipping==1 ? 11 : clipping==2 ? 12.03f : 30;
            submit((std::string("bbox-clip-")+std::to_string(clipping)).c_str());
            int blue = 0;
            for (size_t i=0; i<pixels.size(); i+=4) {
                const bool green = pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20;
                require(green == bboxMask[i/4], "bbox lost edges beyond near/far");
                blue += pixels[i]<20 && pixels[i+1]<20 && pixels[i+2]>235;
            }
            if (clipping==0 || clipping==2)
                require(blue==0, "bbox projection leaked into main scene");
        }
        camera->nearDistance=1;
        camera->farDistance=30;
        camera->ref();
        auto* perspective = new SoPerspectiveCamera;
        perspective->position.setValue(0,0,10);
        perspective->heightAngle=.65f;
        perspective->nearDistance=1; perspective->farDistance=30;
        root->replaceChild(camera,perspective);
        submit("bbox-perspective");
        for (size_t i=0; i<pixels.size(); i+=4)
            bboxMask[i/4] = pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20;
        require(std::count(bboxMask.begin(),bboxMask.end(),true)>100,
                "perspective bbox baseline absent");
        for (int clipping=0; clipping<2; ++clipping) {
            perspective->nearDistance=clipping==0 ? 13 : 1;
            perspective->farDistance=clipping==0 ? 30 : 11;
            submit((std::string("bbox-perspective-clip-")+std::to_string(clipping)).c_str());
            for (size_t i=0; i<pixels.size(); i+=4)
                require((pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20)==bboxMask[i/4],
                        "perspective bbox lost near/far edges");
        }
        perspective->position.setValue(0,0,-5);
        perspective->nearDistance=1; perspective->farDistance=30;
        submit("bbox-behind-eye");
        for (size_t i=0; i<pixels.size(); i+=4)
            require(!(pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20),
                    "bbox behind perspective eye remained visible");
        root->replaceChild(perspective,camera);
        camera->unref();
        camera->position.setValue(20,0,10);
        submit("bbox-lateral-clipped");
        for (size_t i=0; i<pixels.size(); i+=4)
            require(!(pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20),
                    "bbox bypassed lateral clipping");
        camera->position.setValue(0,0,10);
        annotation->setDetail(new SoFaceDetail);
        submit("detail");
        pixel(64, 64, 255, 0, 0);
        annotation->setDetail(nullptr);
        submit("detail-cleared");
        patchContainsBlue(65, 65);
        source->removeChild(selected);
        submit("removed-path");
        patchContainsBlue(64, 62);
        for (size_t i = 0; i < pixels.size(); i += 4)
            require(!(pixels[i] < 20 && pixels[i+1] > 235 && pixels[i+2] < 20),
                    "removed bounding box remained");
        root->removeChild(annotation);
        require(target->resize(SbVec2i32(192, 96)), "target resize failed");
        action.setViewportRegion(SbViewportRegion(192, 96));
        submit("resized-grid");
        require(gridLines->numVertices.getNum() == 60, "grid did not update aspect ratio");
        int dark = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            dark += pixels[i] < 20 && pixels[i+1] < 20 && pixels[i+2] < 20;
        require(dark > 500, "grid absent over scene after resize");
        require(QOpenGLContext::currentContext() == initialQtContext,
                "callback overlays replaced the application Qt GL context");
        action.setRenderTarget(nullptr);
        selected->unref();
        root->unref();
        return 1;
    } catch (const std::exception& e) {
        error = e.what();
        return 0;
    }
}

extern "C" int delayed_viewprovider_test(const char* artifacts)
{
    try {
        auto* document = App::GetApplication().getDocument("DelayedOverlayFixture");
        require(document, "fixture document missing");
        auto* object = document->getObject("Box");
        require(object, "fixture object missing");
        auto* provider = Gui::Application::Instance->getViewProvider(object);
        require(provider, "real Part ViewProvider missing");
        auto* params = Gui::ViewParams::instance();
        const bool oldProjected = params->getRenderProjectedBBox();
        const bool oldTight = params->getUseTightBoundingBox();
        const bool oldShow = params->getShowSelectionBoundingBox();
        struct Preferences {
            Gui::ViewParams* params; bool projected, tight, show;
            ~Preferences() {
                params->setRenderProjectedBBox(projected);
                params->setUseTightBoundingBox(tight);
                params->setShowSelectionBoundingBox(show);
            }
        } preferences{params, oldProjected, oldTight, oldShow};
        params->setShowSelectionBoundingBox(true);

        std::unique_ptr<SoWgpuRenderTarget> target(
            SoWgpuRenderTarget::createOffscreen(SbVec2i32(128, 128)));
        require(target && target->getStatus() == SoWgpuRenderTarget::TARGET_READY,
                "ViewProvider GPU unavailable");
        target->setDepthReadbackEnabled(FALSE);
        auto render = [&](SoNode* geometry, const std::string& name) {
            auto* scene = new SoSeparator;
            scene->ref();
            struct Scene { SoSeparator* node; ~Scene() { node->unref(); } } scope{scene};
            auto* camera = new SoOrthographicCamera;
            camera->position.setValue(0, 0, 10);
            camera->height = 8; camera->nearDistance = 1; camera->farDistance = 30;
            scene->addChild(camera);
            auto* lighting = new SoLightModel;
            lighting->model = SoLightModel::BASE_COLOR;
            scene->addChild(lighting);
            scene->addChild(geometry);
            SoWgpuRenderAction action(SbViewportRegion(128,128));
            action.setBackgroundColor(SbColor4f(1,1,1,1));
            action.setRenderTarget(target.get());
            action.apply(scene);
            require(action.getLastStatus() == SoWgpuRenderAction::SUCCESS,
                    action.getLastError().getString());
            std::vector<uint8_t> pixels;
            target->readbackRGBA(pixels);
            require(pixels.size() == 128*128*4, "ViewProvider readback absent");
            QImage image(pixels.data(),128,128,QImage::Format_RGBA8888);
            require(image.save(QString::fromUtf8(artifacts)+"/"+
                               QString::fromStdString(name)+".png"), "ViewProvider PNG failed");
            return pixels;
        };
        std::vector<uint8_t> previous, localPixels;
        for (const char* subname : {"", "Face1"}) {
        for (bool projected : {false, true}) {
            for (bool tight : {false, true}) {
                params->setRenderProjectedBBox(projected);
                params->setUseTightBoundingBox(tight);
                auto* annotation = new Gui::SoFCPathAnnotation(provider, subname);
                auto* path = new SoPath(provider->getRoot());
                path->ref();
                annotation->setPath(path);
                path->unref();
                const std::string name = std::string("provider-")+
                    (*subname ? "face1-" : "object-")+
                    (projected ? "projected-" : "local-")+(tight ? "tight" : "loose");
                auto pixels = render(annotation, name);
                int green = 0, minX=128, maxX=-1, minY=128, maxY=-1;
                for (int y=0; y<128; ++y) for (int x=0; x<128; ++x) {
                    size_t i=(y*128+x)*4;
                    if (pixels[i]<20 && pixels[i+1]>235 && pixels[i+2]<20) {
                        ++green;
                        minX=std::min(minX,x); maxX=std::max(maxX,x);
                        minY=std::min(minY,y); maxY=std::max(maxY,y);
                    }
                }
                require(green > 100, "real ViewProvider bbox missing");
                // A rectangular Part box has identical tight and scene-graph bounds.
                if (tight && (!*subname || !projected))
                    require(pixels == previous, "tight/loose Part box bounds disagree");
                if (!*subname) {
                    // Analytic XY bounds of the 2x1 box, rotated 30 degrees,
                    // translated (1,.4), at 16 pixels/world unit. A missing or
                    // doubled placement cannot pass merely by drawing green.
                    require(std::abs(minX-62)<=2 && std::abs(maxX-98)<=2 &&
                            std::abs(minY-43)<=2 && std::abs(maxY-73)<=2,
                            "real Part bbox placement/rotation incorrect");
                }
                else if (projected && tight) {
                    require(pixels != previous, "Face1 tight bbox ignored subname");
                }
                if (!*subname && !projected && !tight) localPixels=pixels;
                if (!*subname && projected && !tight)
                    require(pixels != localPixels, "projected bbox ignored global-axis alignment");
                previous = pixels;
            }
        }
        }
        return 1;
    } catch (const std::exception& e) {
        error = e.what();
        return 0;
    }
}

extern "C" int delayed_selection_menu_test(void* parent, int confirm,
                                           int (*observe)(int))
{
    try {
        auto* document = App::GetApplication().getActiveDocument();
        require(document, "menu document missing");
        auto* object = document->getObject("Box");
        require(object, "menu object missing");
        Gui::SelectionMenu menu(static_cast<QWidget*>(parent));
        std::vector<Gui::PickData> picks;
        for (const char* face : {"Face1", "Face2"})
            picks.push_back({object,face,document->getName(),"Box",face});
        bool valid = true;
        int observedHovers = 0;
        QString failure;
        auto fail = [&](const char* message) {
            valid=false; failure=QString::fromUtf8(message); menu.close();
        };
        QAction* first=nullptr;
        QAction* second=nullptr;
        QTimer::singleShot(100, &menu, [&] {
            for (auto* category : menu.actions()) {
                if (!category->menu()) continue;
                for (auto* action : category->menu()->actions()) {
                    if (action->text().contains("Face1")) first=action;
                    if (action->text().contains("Face2")) second=action;
                }
            }
            if (!first || !second || !Gui::Selection().isClarifySelectionActive()) {
                fail("production menu entries/active state missing"); return;
            }
            menu.onHover(first);
        });
        QTimer::singleShot(800, &menu, [&] {
            if (!valid) return;
            if (!observe(1)) { fail("Face1 menu hover failed"); return; }
            ++observedHovers;
            menu.onHover(second);
        });
        QTimer::singleShot(1500, &menu, [&] {
            if (!valid) return;
            if (!observe(2)) { fail("Face2 menu hover failed"); return; }
            ++observedHovers;
            if (!confirm) { menu.close(); return; }
            auto* submenu = qobject_cast<QMenu*>(second->parent());
            if (!submenu) { fail("Face2 submenu missing"); return; }
            menu.setActiveAction(submenu->menuAction());
            QKeyEvent open(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
            QApplication::sendEvent(&menu, &open);
            QTimer::singleShot(400, submenu, [&, submenu] {
            if (!submenu->isVisible()) { fail("Face submenu did not open"); return; }
            submenu->setActiveAction(second);
            QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(submenu, &accept);
            });
        });
        QTimer::singleShot(5000,&menu,[&] { fail("production menu did not finish"); });
        Gui::Selection().clearSelection();
        auto picked=menu.doPick(picks,static_cast<QWidget*>(parent)->mapToGlobal(QPoint(5,5)));
        require(valid,failure.toUtf8().constData());
        require(observedHovers == 2, "menu closed before both hover checks");
        require(!Gui::Selection().isClarifySelectionActive(), "menu active state leaked");
        require(Gui::Selection().getPreselection().pObjectName == nullptr ||
                !*Gui::Selection().getPreselection().pObjectName, "menu preselection leaked");
        require(confirm ? picked.subName=="Face2" : picked.obj==nullptr,
                "menu confirmation/cancellation result incorrect");
        return 1;
    } catch (const std::exception& e) { error=e.what(); return 0; }
}
