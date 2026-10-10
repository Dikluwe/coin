// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: Kongsberg Oil & Gas Technologies AS
// SPDX-FileCopyrightText: 2026 Joao Matos
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the     *
 *   License, or (at your option) any later version.                          *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of               *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Lesser General Public License for more details.                      *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD.  If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                         *
 *                                                                            *
 ******************************************************************************/

/*!
  \class SIM::Coin3D::Quarter::QuarterWidget QuarterWidget.h Quarter/QuarterWidget.h

  \brief The QuarterWidget class is the main class in Quarter. It
  provides a widget for Coin rendering. It provides scenegraph
  management and event handling.

  If you want to modify the GL format for an existing QuarterWidget, you can
  set up a new GL context for the widget, e.g.:

  \code
  QGLContext * context = new QGLContext(QGLFormat(QGL::SampleBuffers), viewer);
  if (context->create()) {
    viewer->setContext(context);
  }
  \endcode
*/

#ifdef _MSC_VER
#pragma warning(disable : 4267)
#endif

#include <cassert>
#include <functional>

#if HAVE_CONFIG_H
# include <config.h>
# ifdef  HAVE_GL_GL_H
#  include <GL/gl.h>
# endif
#endif

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QOpenGLDebugLogger>
#include <QOpenGLDebugMessage>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWindow>
#include <cstdio>

#include <Inventor/C/basic.h>
#include <Inventor/SbByteBuffer.h>

#include <Inventor/SbColor.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoEventManager.h>
#include <Inventor/SoRenderManager.h>
#include <Gui/Selection/Selection.h>
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <cstdlib>
#include <string>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <QtGui/qguiapplication_platform.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <QTabBar>
#include <QRegion>
#include <vector>
#endif
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/scxml/ScXML.h>
#include <Inventor/scxml/SoScXMLStateMachine.h>

#include <Base/Profiler.h>

#include "QuarterWidget.h"
#include "InteractionMode.h"
#include "QuarterP.h"
#include "QuarterWidgetP.h"
#include "eventhandlers/EventFilter.h"
#include "eventhandlers/DragDropHandler.h"


using namespace SIM::Coin3D::Quarter;

/*!
  \enum SIM::Coin3D::Quarter::QuarterWidget::TransparencyType

  Various settings for how to do rendering of transparent objects in
  the scene. Some of the settings will provide faster rendering, while
  others gives you better quality rendering.

  See \ref SoGLRenderAction::TransparencyType for a full description of the modes
*/

/*!
  \enum SIM::Coin3D::Quarter::QuarterWidget::RenderMode

  Sets how rendering of primitives is done.

  See \ref SoRenderManager::RenderMode for a full description of the modes
*/

/*!
  \enum SIM::Coin3D::Quarter::QuarterWidget::StereoMode

  Sets how stereo rendering is performed.

  See \ref SoRenderManager::StereoMode for a full description of the modes
*/

  enum StereoMode {
    MONO = SoRenderManager::MONO,
    ANAGLYPH = SoRenderManager::ANAGLYPH,
    QUAD_BUFFER = SoRenderManager::QUAD_BUFFER,
    INTERLEAVED_ROWS = SoRenderManager::INTERLEAVED_ROWS,
    INTERLEAVED_COLUMNS = SoRenderManager::INTERLEAVED_COLUMNS
  };

#define PRIVATE(obj) obj->pimpl

#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
class WgpuPresentWidget final : public QWidget {
public:
    using FrameCallback = std::function<void()>;

    WgpuPresentWidget(QWidget* parent, FrameCallback callback)
        : QWidget(parent), frameCallback(std::move(callback))
    {}

    QPaintEngine * paintEngine() const override { return nullptr; }

    void requestFrame()
    {
        // The owner coalesces requests; never render synchronously from events.
        if (frameCallback)
            frameCallback();
    }


    void updateVisibleClip()
    {
        auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (!x11)
            return;
        QRegion clip(rect());
        for (QWidget* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            const QPoint origin = mapFromGlobal(ancestor->mapToGlobal(QPoint()));
            clip &= QRegion(QRect(origin, ancestor->size()));
            if (!ancestor->mask().isEmpty())
                clip &= ancestor->mask().translated(origin);
            if (ancestor->isWindow())
                break;
        }
        // Alien Qt tab bars paint above the viewport, but an Xlib child would
        // otherwise cover them. Clip presentation, not the camera or picking.
        for (QTabBar* bar : window()->findChildren<QTabBar*>()) {
            if (bar->isVisible())
                clip -= QRegion(QRect(mapFromGlobal(bar->mapToGlobal(QPoint())), bar->size()));
        }
        const qreal ratio = devicePixelRatioF();
        if (clipInitialized && clip == lastClip && ratio == lastClipRatio)
            return;
        std::vector<XRectangle> rectangles;
        for (const QRect& area : clip) {
            const int x0 = qRound(area.x() * ratio);
            const int y0 = qRound(area.y() * ratio);
            const int x1 = qRound((area.x() + area.width()) * ratio);
            const int y1 = qRound((area.y() + area.height()) * ratio);
            rectangles.push_back({static_cast<short>(x0), static_cast<short>(y0),
                                  static_cast<unsigned short>(x1 - x0),
                                  static_cast<unsigned short>(y1 - y0)});
        }
        const XserverRegion region = XFixesCreateRegion(
            x11->display(), rectangles.empty() ? nullptr : rectangles.data(),
            static_cast<int>(rectangles.size()));
        XFixesSetWindowShapeRegion(x11->display(), static_cast<::Window>(winId()),
                                  ShapeBounding, 0, 0, region);
        XFixesDestroyRegion(x11->display(), region);
        XFlush(x11->display());
        lastClip = clip;
        lastClipRatio = ratio;
        clipInitialized = true;
    }

protected:
    bool event(QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::WinIdChange)
            clipInitialized = false;
        const bool handled = QWidget::event(event);
        if (type == QEvent::Show || type == QEvent::Resize ||
            type == QEvent::WinIdChange || type == QEvent::WindowStateChange)
            requestFrame();
        return handled;
    }

    void paintEvent(QPaintEvent *) override { requestFrame(); }

private:
    FrameCallback frameCallback;
    QRegion lastClip;
    qreal lastClipRatio = 0;
    bool clipInitialized = false;
};
#endif

//We need to avoid buffer swapping when initializing a QPainter on this widget
class CustomGLWidget : public QOpenGLWidget {
public:
    QSurfaceFormat myFormat;

    CustomGLWidget(const QSurfaceFormat& format, QWidget* parent = nullptr, const QOpenGLWidget* shareWidget = nullptr, Qt::WindowFlags f = Qt::WindowFlags())
     : QOpenGLWidget(parent, f), myFormat(format)
    {
        Q_UNUSED(shareWidget);
        QSurfaceFormat surfaceFormat(format);
        surfaceFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        // With the settings below we could determine deprecated OpenGL API
        // but can't do this since otherwise it will complain about almost any
        // OpenGL call in Coin3d
        //surfaceFormat.setMajorVersion(3);
        //surfaceFormat.setMinorVersion(2);
        //surfaceFormat.setProfile(QSurfaceFormat::CoreProfile);

        // On Wayland, we typically get a core profile unless we explicitly
        // request a compatibility profile. On llvmpipe, this still seems to
        // "just work" even if out of spec; on proprietary Nvidia drivers, it
        // does not.
        surfaceFormat.setRenderableType(QSurfaceFormat::OpenGL);
        surfaceFormat.setProfile(QSurfaceFormat::CompatibilityProfile);
        surfaceFormat.setOption(QSurfaceFormat::DeprecatedFunctions, true);

#if defined (_DEBUG) && 0
        surfaceFormat.setOption(QSurfaceFormat::DebugContext);
#endif
        setFormat(surfaceFormat);
    }
    ~CustomGLWidget() override = default;

    void initializeGL() override
    {
#if defined (_DEBUG) && 0
        QOpenGLContext *context = QOpenGLContext::currentContext();
        if (context && context->hasExtension(QByteArrayLiteral("GL_KHR_debug"))) {
            QOpenGLDebugLogger *logger = new QOpenGLDebugLogger(this);
            connect(logger, &QOpenGLDebugLogger::messageLogged, this, &CustomGLWidget::handleLoggedMessage);

            if (logger->initialize())
                logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
        }
#endif

        connect(this, &CustomGLWidget::resized, this, &CustomGLWidget::slotResized);
    }
    // paintGL() is invoked when e.g. using the method grabFramebuffer of this class
    // \code
    // from PySide import QtWidgets
    // mw = Gui.getMainWindow()
    // mdi = mw.findChild(QtWidgets.QMdiArea)
    // gl = mdi.findChild(QtWidgets.QOpenGLWidget)
    // img = gl.grabFramebuffer()
    // \endcode
    void paintGL() override
    {
        QuarterWidget* qw = qobject_cast<QuarterWidget*>(parentWidget());
        if (qw) {
            qw->redraw();
        }
    }

    bool event(QEvent *e) override
    {
        // If a debug logger is activated then Qt's default implementation
        // first releases the context before stopping the logger. However,
        // the logger needs the active context and thus crashes because it's
        // null.
        if (e->type() == QEvent::WindowChangeInternal) {
            if (!qApp->testAttribute(Qt::AA_ShareOpenGLContexts)) {
                QOpenGLDebugLogger* logger = this->findChild<QOpenGLDebugLogger*>();
                if (logger) {
                    logger->stopLogging();
                    delete logger;
                }
            }
        }

        return QOpenGLWidget::event(e);
    }
    void handleLoggedMessage(const QOpenGLDebugMessage &message)
    {
        qDebug() << message;
    }
    void showEvent(QShowEvent*) override
    {
        update(); // force update when changing window mode
    }
    void slotResized()
    {
        update(); // fixes flickering on some systems
    }
};

/*! constructor */
QuarterWidget::QuarterWidget(const QSurfaceFormat & format, QWidget * parent, const QOpenGLWidget * sharewidget, Qt::WindowFlags f)
  : inherited(parent)
{
  Q_UNUSED(f); 
  this->constructor(format, sharewidget);
}

/*! constructor */
QuarterWidget::QuarterWidget(QWidget * parent, const QOpenGLWidget * sharewidget, Qt::WindowFlags f)
  : inherited(parent)
{
  Q_UNUSED(f); 
  this->constructor(QSurfaceFormat(), sharewidget);
}

/*! constructor */
QuarterWidget::QuarterWidget(QOpenGLContext * context, QWidget * parent, const QOpenGLWidget * sharewidget, Qt::WindowFlags f)
  : inherited(parent)
{
  Q_UNUSED(f); 
  this->constructor(context->format(), sharewidget);
}

void
QuarterWidget::constructor(const QSurfaceFormat & format, const QOpenGLWidget * sharewidget)
{
  QGraphicsScene* scene = new QGraphicsScene(this);
  setScene(scene);
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
  if (qEnvironmentVariableIntValue("FREECAD_COIN_WGPU") == 1) {
    auto * raster = new QWidget(this);
    // Give the presentation surface and interactive native overlays a common
    // native parent. Alien ancestry cannot reliably stack these sibling windows.
    raster->setAttribute(Qt::WA_DontCreateNativeAncestors);
    raster->setAttribute(Qt::WA_NativeWindow);
    setViewport(raster);
  }
  else
#endif
    setViewport(new CustomGLWidget(format, this, sharewidget));
  
  setFrameStyle(QFrame::NoFrame);
  setAutoFillBackground(false);
  viewport()->setAutoFillBackground(false);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
  PRIVATE(this) = new QuarterWidgetP(
      this, qobject_cast<QOpenGLWidget*>(viewport()) ? sharewidget : nullptr);
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
  PRIVATE(this)->wgpuformat = format;
  PRIVATE(this)->wgpuenabled = qEnvironmentVariableIntValue("FREECAD_COIN_WGPU") == 1;
  setProperty("wgpuFrameCount", QVariant::fromValue(qulonglong(0)));
  connect(this, &QuarterWidget::devicePixelRatioChanged, this, [this](qreal) {
      this->requestWgpuFrame();
  });
#endif

  PRIVATE(this)->sorendermanager = new SoRenderManager;
  PRIVATE(this)->initialsorendermanager = true;
  PRIVATE(this)->soeventmanager = new SoEventManager;
  PRIVATE(this)->initialsoeventmanager = true;
  PRIVATE(this)->processdelayqueue = true;

  //Mind the order of initialization as the XML state machine uses
  //callbacks which depends on other state being initialized
  PRIVATE(this)->eventfilter = new EventFilter(this);
  PRIVATE(this)->interactionmode = new InteractionMode(this);

  PRIVATE(this)->currentStateMachine = nullptr;

  PRIVATE(this)->headlight = new SoDirectionalLight;
  PRIVATE(this)->headlight->ref();

  PRIVATE(this)->sorendermanager->setAutoClipping(SoRenderManager::VARIABLE_NEAR_PLANE);
  PRIVATE(this)->sorendermanager->setRenderCallback(QuarterWidgetP::rendercb, this);
  PRIVATE(this)->sorendermanager->setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 0.0f));
  PRIVATE(this)->sorendermanager->activate();
  PRIVATE(this)->sorendermanager->addPreRenderCallback(QuarterWidgetP::prerendercb, PRIVATE(this));
  PRIVATE(this)->sorendermanager->addPostRenderCallback(QuarterWidgetP::postrendercb, PRIVATE(this));

  PRIVATE(this)->soeventmanager->setNavigationState(SoEventManager::MIXED_NAVIGATION);

  // set up a cache context for the default SoGLRenderAction
  PRIVATE(this)->sorendermanager->getGLRenderAction()->setCacheContext(this->getCacheContextId());

  this->setMouseTracking(true);

  // Qt::StrongFocus means the widget will accept keyboard focus by
  // both tabbing and clicking
  this->setFocusPolicy(Qt::StrongFocus);

  this->installEventFilter(PRIVATE(this)->eventfilter);
  this->installEventFilter(PRIVATE(this)->interactionmode);

  initialized = false;
}

void
QuarterWidget::replaceViewport()
{
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
  delete PRIVATE(this)->wgpuadapter;
  PRIVATE(this)->wgpuadapter = nullptr;
  delete PRIVATE(this)->wgpupresent;
  PRIVATE(this)->wgpupresent = nullptr;
  if (PRIVATE(this)->wgpuenabled && !PRIVATE(this)->wgpufailed) {
    this->requestWgpuFrame();
    return;
  }
#endif
  CustomGLWidget* oldvp = dynamic_cast<CustomGLWidget*>(viewport());
  if (!oldvp) return;
  CustomGLWidget* newvp = new CustomGLWidget(oldvp->myFormat, this);
  PRIVATE(this)->replaceGLWidget(newvp);
  setViewport(newvp);
  viewport()->setMouseTracking(true);

  setAutoFillBackground(false);
  viewport()->setAutoFillBackground(false);
}

/*! destructor */
QuarterWidget::~QuarterWidget()
{
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
  delete PRIVATE(this)->wgpuadapter;
  PRIVATE(this)->wgpuadapter = nullptr;
  delete PRIVATE(this)->wgpupresent;
  PRIVATE(this)->wgpupresent = nullptr;
#endif
  if (PRIVATE(this)->currentStateMachine) {
    this->removeStateMachine(PRIVATE(this)->currentStateMachine);
    delete PRIVATE(this)->currentStateMachine;
  }
  PRIVATE(this)->headlight->unref();
  PRIVATE(this)->headlight = nullptr;
  this->setSceneGraph(nullptr);
  this->setSoRenderManager(nullptr);
  this->setSoEventManager(nullptr);
  delete PRIVATE(this)->eventfilter;
  delete PRIVATE(this);
}

/*!
  You can set the cursor you want to use for a given navigation
  state. See the Coin documentation on navigation for information
  about available states
*/
void
QuarterWidget::setStateCursor(const SbName & state, const QCursor & cursor)
{
  assert(QuarterP::statecursormap);
  // will overwrite the value of an existing item
  QuarterP::statecursormap->insert(state, cursor);
}

/*!
  Maps a state to a cursor

  \param[in] state Named state in the statemachine
  \retval Cursor corresponding to the given state
*/
QCursor
QuarterWidget::stateCursor(const SbName & state)
{
  assert(QuarterP::statecursormap);
  return QuarterP::statecursormap->value(state);
}

/*!
  \property QuarterWidget::headlightEnabled

  \copydetails QuarterWidget::setHeadlightEnabled
*/

/*!
  Enable/disable the headlight. This will toggle the SoDirectionalLight::on
  field (returned from getHeadlight()).
*/
void
QuarterWidget::setHeadlightEnabled(bool onoff)
{
  PRIVATE(this)->headlight->on = onoff;
}

/*!
  Returns true if the headlight is on, false if it is off
*/
bool
QuarterWidget::headlightEnabled() const
{
  return PRIVATE(this)->headlight->on.getValue();
}

/*!
  Returns the light used for the headlight.
*/
SoDirectionalLight *
QuarterWidget::getHeadlight() const
{
  return PRIVATE(this)->headlight;
}

/*!
  \property QuarterWidget::clearZBuffer

  \copydetails QuarterWidget::setClearZBuffer
*/

/*!
  Specify if you want the z buffer to be cleared before
  redraw. This is on by default.
*/
void
QuarterWidget::setClearZBuffer(bool onoff)
{
  PRIVATE(this)->clearzbuffer = onoff;
}

/*!
  Returns true if the z buffer is cleared before rendering.
*/
bool
QuarterWidget::clearZBuffer() const
{
  return PRIVATE(this)->clearzbuffer;
}

/*!
  \property QuarterWidget::clearWindow

  \copydetails QuarterWidget::setClearWindow
*/

/*!
  Specify if you want the rendering buffer to be cleared before
  rendering. This is on by default.
 */
void
QuarterWidget::setClearWindow(bool onoff)
{
  PRIVATE(this)->clearwindow = onoff;
}

/*!
  Returns true if the rendering buffer is cleared before rendering.
*/
bool
QuarterWidget::clearWindow() const
{
  return PRIVATE(this)->clearwindow;
}

/*!
  \property QuarterWidget::interactionModeEnabled

  \copydetails QuarterWidget::setInteractionModeEnabled
*/

/*!
  Enable/disable interaction mode.

  Specifies whether you may use the alt-key to enter interaction mode.
*/
void
QuarterWidget::setInteractionModeEnabled(bool onoff)
{
  PRIVATE(this)->interactionmode->setEnabled(onoff);
}

/*!
  Returns true if interaction mode is enabled, false otherwise.
 */
bool
QuarterWidget::interactionModeEnabled() const
{
  return PRIVATE(this)->interactionmode->enabled();
}

/*!
  \property QuarterWidget::interactionModeOn

  \copydetails QuarterWidget::setInteractionModeOn
*/

/*!
  Turn interaction mode on or off.
*/
void
QuarterWidget::setInteractionModeOn(bool onoff)
{
  PRIVATE(this)->interactionmode->setOn(onoff);
}

/*!
  Returns true if interaction mode is on.
 */
bool
QuarterWidget::interactionModeOn() const
{
  return PRIVATE(this)->interactionmode->on();
}

/*!
  Returns the Coin cache context id for this widget.
*/
uint32_t
QuarterWidget::getCacheContextId() const
{
  return PRIVATE(this)->getCacheContextId();
}

/*!
  \property QuarterWidget::transparencyType

  \copydetails QuarterWidget::setTransparencyType
*/

/*!
  Sets the transparency type to be used for the scene.
*/
void
QuarterWidget::setTransparencyType(TransparencyType type)
{
  assert(PRIVATE(this)->sorendermanager);
  PRIVATE(this)->sorendermanager->getGLRenderAction()->setTransparencyType((SoGLRenderAction::TransparencyType)type);
  PRIVATE(this)->sorendermanager->scheduleRedraw();
}

/*!
  \retval The current \ref TransparencyType
*/
QuarterWidget::TransparencyType
QuarterWidget::transparencyType() const
{
  assert(PRIVATE(this)->sorendermanager);
  SoGLRenderAction * action = PRIVATE(this)->sorendermanager->getGLRenderAction();
  return static_cast<QuarterWidget::TransparencyType>(action->getTransparencyType());
}

/*!
  \property QuarterWidget::renderMode

  \copydetails QuarterWidget::setRenderMode
*/

/*!
  \copydoc RenderMode
*/
void
QuarterWidget::setRenderMode(RenderMode mode)
{
  assert(PRIVATE(this)->sorendermanager);
  PRIVATE(this)->sorendermanager->setRenderMode(static_cast<SoRenderManager::RenderMode>(mode));
  PRIVATE(this)->sorendermanager->scheduleRedraw();
}

/*!
  \retval The current \ref RenderMode
*/
QuarterWidget::RenderMode
QuarterWidget::renderMode() const
{
  assert(PRIVATE(this)->sorendermanager);
  return static_cast<RenderMode>(PRIVATE(this)->sorendermanager->getRenderMode());
}

/*!
  \property QuarterWidget::stereoMode

  \copydetails QuarterWidget::setStereoMode
*/

/*!
  \copydoc StereoMode
*/
void
QuarterWidget::setStereoMode(StereoMode mode)
{
  assert(PRIVATE(this)->sorendermanager);
  PRIVATE(this)->sorendermanager->setStereoMode(static_cast<SoRenderManager::StereoMode>(mode));
  PRIVATE(this)->sorendermanager->scheduleRedraw();
}


/*!
  \retval The current \ref StereoMode
*/
QuarterWidget::StereoMode
QuarterWidget::stereoMode() const
{
  assert(PRIVATE(this)->sorendermanager);
  return static_cast<StereoMode>(PRIVATE(this)->sorendermanager->getStereoMode());
}

/*!
  \property QuarterWidget::devicePixelRatio

  \copydetails QuarterWidget::devicePixelRatio
*/

/*!
  The ratio between logical and physical pixel sizes -- obtained from the window that
the widget is located within, and updated whenever any change occurs, emitting a devicePixelRatioChanged signal.  Only available for version Qt 5.6 and above (will be 1.0 for all previous versions)
 */

qreal
QuarterWidget::devicePixelRatio() const
{
  return PRIVATE(this)->device_pixel_ratio;
}

/*!
  Sets the Inventor scenegraph to be rendered
 */
void
QuarterWidget::setSceneGraph(SoNode * node)
{
  if (node == PRIVATE(this)->scene) {
    return;
  }

  if (PRIVATE(this)->scene) {
    PRIVATE(this)->scene->unref();
    PRIVATE(this)->scene = nullptr;
  }

  SoCamera * camera = nullptr;
  SoSeparator * superscene = nullptr;
  bool viewall = false;

  if (node) {
    PRIVATE(this)->scene = node;
    PRIVATE(this)->scene->ref();

    superscene = new SoSeparator;
    superscene->addChild(PRIVATE(this)->headlight);

    // if the scene does not contain a camera, add one
    if (!(camera = PRIVATE(this)->searchForCamera(node))) {
      camera = new SoPerspectiveCamera;
      superscene->addChild(camera);
      viewall = true;
    }

    superscene->addChild(node);
  }

  PRIVATE(this)->soeventmanager->setCamera(camera);
  PRIVATE(this)->sorendermanager->setCamera(camera);
  PRIVATE(this)->soeventmanager->setSceneGraph(superscene);
  PRIVATE(this)->sorendermanager->setSceneGraph(superscene);

  if (viewall) { this->viewAll(); }
  if (superscene) { superscene->touch(); }
}

/*!
  Returns pointer to root of scene graph
*/
SoNode *
QuarterWidget::getSceneGraph() const
{
  return PRIVATE(this)->scene;
}

#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
SoNode *
QuarterWidget::getWgpuSceneGraph() const
{
  return PRIVATE(this)->sorendermanager
    ? PRIVATE(this)->sorendermanager->getSceneGraph()
    : PRIVATE(this)->scene;
}
#endif

/*!
  Set the render manager for the widget.
*/
void
QuarterWidget::setSoRenderManager(SoRenderManager * manager)
{
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
  delete PRIVATE(this)->wgpuadapter;
  PRIVATE(this)->wgpuadapter = nullptr;
#endif
  bool carrydata = false;
  SoNode * scene = nullptr;
  SoCamera * camera = nullptr;
  SbViewportRegion vp;
  if (PRIVATE(this)->sorendermanager && manager) {
    scene = PRIVATE(this)->sorendermanager->getSceneGraph();
    camera = PRIVATE(this)->sorendermanager->getCamera();
    vp = PRIVATE(this)->sorendermanager->getViewportRegion(); // clazy:exclude=rule-of-two-soft
    carrydata = true;
  }

  // ref before deleting the old scene manager to avoid that the nodes are deleted
  if (scene) scene->ref();
  if (camera) camera->ref();
  
  if (PRIVATE(this)->initialsorendermanager) {
    delete PRIVATE(this)->sorendermanager;
    PRIVATE(this)->initialsorendermanager = false;
  }
  PRIVATE(this)->sorendermanager = manager;
  if (carrydata) {
    PRIVATE(this)->sorendermanager->setSceneGraph(scene);
    PRIVATE(this)->sorendermanager->setCamera(camera);
    PRIVATE(this)->sorendermanager->setViewportRegion(vp);
  }

  if (scene) scene->unref();
  if (camera) camera->unref();
}

/*!
  Returns a pointer to the render manager.
*/
SoRenderManager *
QuarterWidget::getSoRenderManager() const
{
  return PRIVATE(this)->sorendermanager;
}

/*!
  Set the Coin event manager for the widget.
*/
void
QuarterWidget::setSoEventManager(SoEventManager * manager)
{
  bool carrydata = false;
  SoNode * scene = nullptr;
  SoCamera * camera = nullptr;
  SbViewportRegion vp;
  if (PRIVATE(this)->soeventmanager && manager) {
    scene = PRIVATE(this)->soeventmanager->getSceneGraph();
    camera = PRIVATE(this)->soeventmanager->getCamera();
    vp = PRIVATE(this)->soeventmanager->getViewportRegion(); // clazy:exclude=rule-of-two-soft
    carrydata = true;
  }

  // ref before deleting the old scene manager to avoid that the nodes are deleted
  if (scene) scene->ref();
  if (camera) camera->ref();

  if (PRIVATE(this)->initialsoeventmanager) {
    delete PRIVATE(this)->soeventmanager;
    PRIVATE(this)->initialsoeventmanager = false;
  }
  PRIVATE(this)->soeventmanager = manager;
  if (carrydata) {
    PRIVATE(this)->soeventmanager->setSceneGraph(scene);
    PRIVATE(this)->soeventmanager->setCamera(camera);
    PRIVATE(this)->soeventmanager->setViewportRegion(vp);
  }

  if (scene) scene->unref();
  if (camera) camera->unref();
}

/*!
  Returns a pointer to the event manager
*/
SoEventManager *
QuarterWidget::getSoEventManager() const
{
  return PRIVATE(this)->soeventmanager;
}

/*!
  Returns a pointer to the event filter
 */
EventFilter *
QuarterWidget::getEventFilter() const
{
  return PRIVATE(this)->eventfilter;
}

/*!
  Reposition the current camera to display the entire scene
 */
void
QuarterWidget::viewAll()
{
  const SbName viewallevent("sim.coin3d.coin.navigation.ViewAll");
  for (int c = 0; c < PRIVATE(this)->soeventmanager->getNumSoScXMLStateMachines(); ++c) {
    SoScXMLStateMachine * sostatemachine =
      PRIVATE(this)->soeventmanager->getSoScXMLStateMachine(c);
    if (sostatemachine->isActive()) {
      sostatemachine->queueEvent(viewallevent);
      sostatemachine->processEventQueue();
    }
  }
}

/*!
  Sets the current camera in seek mode, if supported by the underlying navigation system.
  Camera typically seeks towards what the mouse is pointing at.
*/
void
QuarterWidget::seek()
{
  const SbName seekevent("sim.coin3d.coin.navigation.Seek");
  for (int c = 0; c < PRIVATE(this)->soeventmanager->getNumSoScXMLStateMachines(); ++c) {
    SoScXMLStateMachine * sostatemachine =
      PRIVATE(this)->soeventmanager->getSoScXMLStateMachine(c);
    if (sostatemachine->isActive()) {
      sostatemachine->queueEvent(seekevent);
      sostatemachine->processEventQueue();
    }
  }
}

bool
QuarterWidget::updateDevicePixelRatio() {
    qreal dev_pix_ratio = 1.0;
    QWidget* winwidg = window();
    QWindow* win = nullptr;
    if(winwidg) {
        win = winwidg->windowHandle();
    }
    if(win) {
        dev_pix_ratio = win->devicePixelRatio();
    }
    else {
        dev_pix_ratio = ((QGuiApplication*)QGuiApplication::instance())->devicePixelRatio();
    }
    if(PRIVATE(this)->device_pixel_ratio != dev_pix_ratio) {
        PRIVATE(this)->device_pixel_ratio = dev_pix_ratio;
        Q_EMIT devicePixelRatioChanged(dev_pix_ratio);
        return true;
    }
    return false;
}

/*!
  Overridden from QGLWidget to resize the Coin scenegraph
 */
void QuarterWidget::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    updateDevicePixelRatio();

    const qreal ratio = devicePixelRatio();
    const QSize logicalSize = viewport()->size();
    const int width = std::max(1, static_cast<int>(ratio * logicalSize.width()));
    const int height = std::max(1, static_cast<int>(ratio * logicalSize.height()));

    const SbViewportRegion vp(width, height);
    PRIVATE(this)->sorendermanager->setViewportRegion(vp);
    PRIVATE(this)->soeventmanager->setViewportRegion(vp);
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
    if (PRIVATE(this)->wgpupresent)
        PRIVATE(this)->wgpupresent->setGeometry(viewport()->rect());
    if (PRIVATE(this)->wgpuadapter)
        PRIVATE(this)->wgpuadapter->resize(SbVec2i32(width, height));
    this->requestWgpuFrame();
#endif
    if (scene())
        scene()->setSceneRect(QRect(QPoint(0, 0), logicalSize));
}

/*!
  Overridden from QGLWidget to render the scenegraph
*/
void QuarterWidget::paintEvent(QPaintEvent* event)
{
    ZoneScoped;

#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
    if (this->requestWgpuFrame())
        return;
#endif

    if (updateDevicePixelRatio()) {
        const qreal dev_pix_ratio = devicePixelRatio();
        const QSize logicalSize = viewport()->size();
        const int width = std::max(1, static_cast<int>(dev_pix_ratio * logicalSize.width()));
        const int height = std::max(1, static_cast<int>(dev_pix_ratio * logicalSize.height()));
        SbViewportRegion vp(width, height);
        PRIVATE(this)->sorendermanager->setViewportRegion(vp);
        PRIVATE(this)->soeventmanager->setViewportRegion(vp);
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
        if (PRIVATE(this)->wgpuadapter)
            PRIVATE(this)->wgpuadapter->resize(SbVec2i32(width, height));
#endif
    }

    if(!initialized) {
        this->getSoRenderManager()->reinitialize();
        initialized = true;
    }

    getSoRenderManager()->activate();

    QOpenGLWidget* w = static_cast<QOpenGLWidget*>(this->viewport());
    if (!w->isValid()) {
        qWarning() << "No valid GL context found!";
        return;
    }

    PRIVATE(this)->autoredrawenabled = false;
    if (PRIVATE(this)->processdelayqueue && SoDB::getSensorManager()->isDelaySensorPending()) {
        w->doneCurrent();
        SoDB::getSensorManager()->processDelayQueue(false);
        w->makeCurrent();
    }

    assert(w->isValid() && "No valid GL context found!");


    // Causes an OpenGL error on resize
    //glDrawBuffer(w->format().swapBehavior() == QSurfaceFormat::DoubleBuffer ? GL_BACK : GL_FRONT);

    w->makeCurrent();
    PRIVATE(this)->timesincelastframe.restart();
    this->actualRedraw();

    QOpenGLFunctions* functions = w->context() ? w->context()->functions() : nullptr;
    const bool multisampleEnabled = functions && functions->glIsEnabled(GL_MULTISAMPLE) == GL_TRUE;

    //start the standard graphics view processing for all widgets and graphic items. As 
    //QGraphicsView initaliizes a QPainter which changes the Opengl context in an unpredictable 
    //manner we need to store the context and recreate it after Qt is done.
    inherited::paintEvent(event);
    w->makeCurrent();

    if (functions) {
        if (multisampleEnabled) {
            functions->glEnable(GL_MULTISAMPLE);
        }
        else {
            functions->glDisable(GL_MULTISAMPLE);
        }
    }

    // Causes an OpenGL error on resize
    //if (w->format().swapBehavior() == QSurfaceFormat::DoubleBuffer)
    //    w->context()->swapBuffers(w->context()->surface());

    PRIVATE(this)->autoredrawenabled = true;

    // process the delay queue the next time we enter this function,
    // unless we get here after a call to redraw().
    PRIVATE(this)->processdelayqueue = true;

    // Nothing above can have asked for another frame, so any request that is
    // still outstanding has been satisfied by the render we just did. The
    // frame is timed from before actualRedraw(), not from here.
    PRIVATE(this)->frameRendered();
}

#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
bool QuarterWidget::requestWgpuFrame()
{
    if (!PRIVATE(this)->wgpuenabled || PRIVATE(this)->wgpufailed)
        return false;
    if (!PRIVATE(this)->wgpuframequeued) {
        PRIVATE(this)->wgpuframequeued = true;
        QMetaObject::invokeMethod(this, [this] {
            PRIVATE(this)->wgpuframequeued = false;
            this->renderWgpuFrameNow();
        }, Qt::QueuedConnection);
    }
    return true;
}

void QuarterWidget::finishWgpuFrame() {}

void QuarterWidget::renderWgpuFrameNow()
{
    if (PRIVATE(this)->wgpufailed || PRIVATE(this)->wgpuframerunning ||
        !isVisible() || window()->isMinimized() || viewport()->size().isEmpty())
        return;
    PRIVATE(this)->wgpuframerunning = true;
    PRIVATE(this)->autoredrawenabled = false;
    PRIVATE(this)->timesincelastframe.restart();
    PRIVATE(this)->frameRendered();
    getSoRenderManager()->activate();
    initialized = true;
    updateDevicePixelRatio();
    const qreal ratio = devicePixelRatio();
    const SbVec2i32 size(
        std::max(1, static_cast<int>(ratio * viewport()->width())),
        std::max(1, static_cast<int>(ratio * viewport()->height())));
    const SbViewportRegion vp(size[0], size[1]);
    getSoRenderManager()->setViewportRegion(vp);
    getSoEventManager()->setViewportRegion(vp);
    if (!PRIVATE(this)->wgpupresent) {
        auto * present = new WgpuPresentWidget(viewport(), [this] {
            // Geometry/show changes made by this frame are already satisfied.
            if (!PRIVATE(this)->wgpuframerunning)
                this->redraw();
        });
        present->setObjectName(QStringLiteral("CoinWgpuPresent"));
        present->setAttribute(Qt::WA_DontCreateNativeAncestors);
        present->setAttribute(Qt::WA_NativeWindow);
        present->setAttribute(Qt::WA_TransparentForMouseEvents);
        present->setWindowFlag(Qt::WindowTransparentForInput, true);
        present->setFocusPolicy(Qt::NoFocus);
        present->setAttribute(Qt::WA_NoSystemBackground);
        present->setAttribute(Qt::WA_PaintOnScreen);
        present->setAttribute(Qt::WA_OpaquePaintEvent);
        present->setAutoFillBackground(false);
        present->setGeometry(viewport()->rect());
        present->winId();
        PRIVATE(this)->wgpupresent = present;
        present->show();
    }
    PRIVATE(this)->wgpupresent->setGeometry(viewport()->rect());
    static_cast<WgpuPresentWidget*>(PRIVATE(this)->wgpupresent)->updateVisibleClip();
    const quintptr windowId = PRIVATE(this)->wgpupresent->winId();
    if (PRIVATE(this)->wgpuadapter && (PRIVATE(this)->wgpuwindow != windowId ||
        PRIVATE(this)->wgpuadapter->getSceneManager()->getRenderTarget()->getOptions().textureSamplingPolicy !=
            (this->property("coinRenderPortableSampling").toBool() ? COIN_RENDER_SAMPLING_PORTABLE : COIN_RENDER_SAMPLING_NATIVE))) {
        delete PRIVATE(this)->wgpuadapter;
        PRIVATE(this)->wgpuadapter = nullptr;
    }
    QString reason;
    if (!PRIVATE(this)->wgpuadapter) {
        auto * x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11) {
            CoinRenderNativeSurfaceDescriptor surface{};
            surface.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
            surface.structSize = sizeof(surface);
            surface.type = COIN_RENDER_SURFACE_XLIB;
            surface.native.xlib.display = x11->display();
            const ::Window presentwindow = static_cast<::Window>(PRIVATE(this)->wgpupresent->winId());
            XserverRegion inputregion = XFixesCreateRegion(x11->display(), nullptr, 0);
            XFixesSetWindowShapeRegion(x11->display(), presentwindow, ShapeInput, 0, 0, inputregion);
            XFixesDestroyRegion(x11->display(), inputregion);
            surface.native.xlib.window = static_cast<uint64_t>(presentwindow);
            CoinRenderOptions options;
            const char* backend = std::getenv("WGPU_BACKEND");
            const char* bgfx = std::getenv("COIN_BGFX_RENDERER");
            options.renderer = (backend && std::string(backend) == "gl") ||
                (bgfx && std::string(bgfx) == "opengl") ? COIN_RENDER_RENDERER_OPENGL : COIN_RENDER_RENDERER_VULKAN;
            options.textureSamplingPolicy = this->property("coinRenderPortableSampling").toBool() ?
                COIN_RENDER_SAMPLING_PORTABLE : COIN_RENDER_SAMPLING_NATIVE;
            PRIVATE(this)->wgpuadapter = new CoinRenderManagerAdapter(
                *getSoRenderManager(), surface, size, options);
            this->setProperty("coinRenderActiveSamplingPolicy", int(options.textureSamplingPolicy));
            PRIVATE(this)->wgpuwindow = windowId;
        }
        else {
            reason = QStringLiteral("Qt is not using the X11/xcb platform plugin");
        }
    }
    bool rendered = false;
    auto candidateStatus = CoinRenderAction::BACKEND_ERROR;
    if (PRIVATE(this)->wgpuadapter) {
        PRIVATE(this)->wgpuadapter->setSceneGraphOverride(this->getWgpuSceneGraph());
        if (PRIVATE(this)->wgpuadapter->resize(size)) {
            candidateStatus = PRIVATE(this)->wgpuadapter->render();
            rendered = candidateStatus == CoinRenderAction::SUCCESS;
        }
        if (!rendered)
            reason = QString::fromUtf8(PRIVATE(this)->wgpuadapter->getLastError().getString());
    }
    PRIVATE(this)->autoredrawenabled = true;
    PRIVATE(this)->processdelayqueue = true;
    PRIVATE(this)->wgpuframerunning = false;
    setProperty("wgpuFrameStatus", int(candidateStatus));
    setProperty("wgpuFrameError", rendered ? QString() : reason);
    if (!rendered && candidateStatus == CoinRenderAction::UNSUPPORTED
        && PRIVATE(this)->wgpuadapter->getSceneManager()->getRenderTarget()->getLastSubmissionSerial() > 0
        && PRIVATE(this)->wgpuadapter->getSceneManager()->getRenderTarget()->getStatus()
            == CoinRenderTarget::TARGET_READY) {
        // Admission failure leaves a healthy target and its last publication.
        // Keep it alive so a corrected scene can recover on the next redraw.
        qWarning() << "CoinRender frame rejected; previous image retained:" << reason;
        return;
    }
    if (rendered) {
        const auto frames = property("wgpuFrameCount").toULongLong() + 1;
        setProperty("wgpuFrameCount", QVariant::fromValue(frames));
        const bool glViewport = qobject_cast<QOpenGLWidget *>(viewport()) != nullptr;
        setProperty("wgpuOpenGLViewport", glViewport);
        if (qEnvironmentVariableIntValue("FREECAD_COIN_WGPU_TRACE") == 1)
            std::fprintf(stderr, "COIN_WGPU_FRAME %llu qt_gl_viewport=%d gl_prepass=0 size=%d %d\n",
                         static_cast<unsigned long long>(frames), glViewport ? 1 : 0,
                         size[0], size[1]);
        this->finishWgpuFrame();
        return;
    }
    qWarning() << "Coin WGPU initialization/render failed; switching viewport to Coin/GL:" << reason;
    setProperty("wgpuFallbackReason", reason);
    PRIVATE(this)->wgpufailed = true;
    delete PRIVATE(this)->wgpuadapter;
    PRIVATE(this)->wgpuadapter = nullptr;
    PRIVATE(this)->wgpupresent->hide();
    delete PRIVATE(this)->wgpupresent;
    PRIVATE(this)->wgpupresent = nullptr;
    // Construct without a parent while the old raster viewport is still installed.
    // Parenting a QOpenGLWidget into a hierarchy that still owns a native child
    // can crash Qt/XCB in QWindow::setFlags(); setViewport() reparents it safely.
    auto * gl = new CustomGLWidget(PRIVATE(this)->wgpuformat, nullptr);
    // Qt can recreate the top-level surface when a GL viewport replaces a
    // raster/native one. Render again only after the new widget's FBO is ready.
    connect(gl, &QOpenGLWidget::resized, this, [this] {
        initialized = false;
        this->redraw();
    }, Qt::QueuedConnection);
    PRIVATE(this)->replaceGLWidget(gl);
    setViewport(gl);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    initialized = false;
    viewport()->update();
}
#endif

bool QuarterWidget::viewportEvent(QEvent* event)
{
#ifdef FREECAD_COIN_WGPU_EXPERIMENTAL
    if (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
        event->type() == QEvent::WindowStateChange)
        this->requestWgpuFrame();
#endif

    // If no item is selected still let the graphics scene handle it but
    // additionally handle it by this viewer. This is e.g. needed when
    // resizing a widget item because the cursor may already be outside
    // this widget.
    if (event->type() == QEvent::MouseButtonDblClick ||
        event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
        QGraphicsItem *item = itemAt(mouse->pos());
        if (!item) {
            QGraphicsView::viewportEvent(event);
            return false;
        }
    }
    else if (event->type() == QEvent::MouseMove ||
             event->type() == QEvent::MouseButtonRelease) {
        QGraphicsScene* glScene = this->scene();
        if (!(glScene && glScene->mouseGrabberItem())) {
            QGraphicsView::viewportEvent(event);
            return false;
        }
    }
    else if (event->type() == QEvent::Wheel) {
        auto wheel = static_cast<QWheelEvent*>(event);
        QPoint pos = wheel->position().toPoint();
        QGraphicsItem* item = itemAt(pos);
        if (!item) {
            QGraphicsView::viewportEvent(event);
            return false;
        }
    }

    return QGraphicsView::viewportEvent(event);
}

/*!
  Used for rendering the scene. Usually Coin/Quarter will automatically redraw
  the scene graph at regular intervals, after the scene is modified.

  However, if you want to disable this functionality and gain full control over
  when the scene is rendered yourself, you can turn off autoredraw in the
  render manager and render the scene by calling this method.
*/
void
QuarterWidget::redraw()
{
  // The request may be deferred to honor the frame rate limit, but it is
  // never dropped, so every caller still gets its frame.
  //
  // When stylesheet is used, there is recursive repaint warning caused by
  // repaint() here. It happens when switching active documents. Based on call
  // stacks, it happens like this, the repaint event first triggers a series
  // calls of QWidgetPrivate::paintSiblingsRecrusive(), and then reaches one of
  // the QuarterWidget. From its paintEvent(), it calls
  // SoSensorManager::processDelayQueue(), which triggers redraw() of another
  // QuarterWidget. And if repaint() is called here, it will trigger another
  // series call of QWidgetPrivate::paintSiblingRecursive(), and eventually
  // back to the first QuarterWidget, at which time the "Recursive repaint
  // detected" Qt warning message will be printed.
  //
  // Note that the recursive repaint is not infinite due to setting
  // 'processdelayqueue = false' in issueRedraw(). However, it does cause
  // annoying flickering, and actually crash on Windows.
  PRIVATE(this)->requestRedraw();
}

/*!
  Returns the upper limit on how often the scene is rendered. A negative value
  follows the refresh rate of the screen the widget is shown on, zero renders
  as fast as the driver allows and a positive value is a limit in frames per
  second.
*/
int
QuarterWidget::maxFrameRate() const
{
  return PRIVATE(this)->maxframerate;
}

/*!
  Sets the upper limit on how often the scene is rendered. Rendering faster
  than the display can show only wastes GPU work, so the default is to follow
  the refresh rate of the screen.

  \sa maxFrameRate()
*/
void
QuarterWidget::setMaxFrameRate(int fps)
{
  PRIVATE(this)->setMaxFrameRate(fps);
}

/*!
  Overridden from QGLWidget to render the scenegraph
 */
void
QuarterWidget::actualRedraw()
{
  ZoneScoped;
  PRIVATE(this)->sorendermanager->render(PRIVATE(this)->clearwindow,
                                         PRIVATE(this)->clearzbuffer);
}


/*!
  Passes an event to the event manager.

  \param[in] event to pass
  \retval Returns true if the event was successfully processed
*/
bool
QuarterWidget::processSoEvent(const SoEvent * event)
{
  return
    event &&
    PRIVATE(this)->soeventmanager &&
    PRIVATE(this)->soeventmanager->processEvent(event);
}

/*!
  \property QuarterWidget::backgroundColor
  \copydoc QuarterWidget::setBackgroundColor
*/

/*!
  Set background color to a given QColor

  Remember that QColors are given in integers between 0 and 255, as
  opposed to SbColor4f which is in [0, 1]. The default alpha value for
  a QColor is 255, but you'll probably want to set it to zero before
  using it as an OpenGL clear color.
 */
void
QuarterWidget::setBackgroundColor(const QColor & color)
{
  SbColor4f bgcolor(SbClamp(color.red()   / 255.0, 0.0, 1.0),
                    SbClamp(color.green() / 255.0, 0.0, 1.0),
                    SbClamp(color.blue()  / 255.0, 0.0, 1.0),
                    SbClamp(color.alpha() / 255.0, 0.0, 1.0));

  PRIVATE(this)->sorendermanager->setBackgroundColor(bgcolor);
  PRIVATE(this)->sorendermanager->scheduleRedraw();
}

/*!
  Returns color used for clearing the rendering area before
  rendering the scene.
 */
QColor
QuarterWidget::backgroundColor() const
{
  SbColor4f bg = PRIVATE(this)->sorendermanager->getBackgroundColor();

  return {SbClamp(int(bg[0] * 255.0), 0, 255),
                SbClamp(int(bg[1] * 255.0), 0, 255),
                SbClamp(int(bg[2] * 255.0), 0, 255),
                SbClamp(int(bg[3] * 255.0), 0, 255)};
}

/*!
  Returns the context menu used by the widget.
*/
QMenu *
QuarterWidget::getContextMenu() const
{
  return PRIVATE(this)->contextMenu();
}

/*!
  \retval Is context menu enabled?
*/
bool
QuarterWidget::contextMenuEnabled() const
{
  return PRIVATE(this)->contextmenuenabled;
}

/*!
  \property QuarterWidget::contextMenuEnabled

  \copydetails QuarterWidget::setContextMenuEnabled
*/

/*!
  Controls the display of the context menu

  \param[in] yes Context menu on?
*/
void
QuarterWidget::setContextMenuEnabled(bool yes)
{
  PRIVATE(this)->contextmenuenabled = yes;
}

/*!
  Convenience method that adds a state machine to the current
  SoEventManager.  It also initializes the scene graph
  root and active camera for the state machine, and finally it sets
  up the default Quarter cursor handling.

  \sa removeStateMachine
*/
void
QuarterWidget::addStateMachine(SoScXMLStateMachine * statemachine)
{
  SoEventManager * em = this->getSoEventManager();
  em->addSoScXMLStateMachine(statemachine);
  statemachine->setSceneGraphRoot(this->getSoRenderManager()->getSceneGraph());
  statemachine->setActiveCamera(this->getSoRenderManager()->getCamera());
  statemachine->addStateChangeCallback(QuarterWidgetP::statechangecb, PRIVATE(this));
}

/*!
  Convenience method that removes a state machine from the current
  SoEventManager.

  \sa addStateMachine
*/
void
QuarterWidget::removeStateMachine(SoScXMLStateMachine * statemachine)
{
  SoEventManager * em = this->getSoEventManager();
  statemachine->setSceneGraphRoot(nullptr);
  statemachine->setActiveCamera(nullptr);
  em->removeSoScXMLStateMachine(statemachine);
}

/*!
  See \ref QWidget::minimumSizeHint
 */
QSize
QuarterWidget::minimumSizeHint() const
{
  return {50, 50};
}

/*!  Returns a list of grouped actions that corresponds to the
  TransparencyType enum. If you want to create a menu in your
  application that controls the transparency type used in
  QuarterWidget, add these actions to the menu.
 */
QList<QAction *>
QuarterWidget::transparencyTypeActions() const
{
  return PRIVATE(this)->transparencyTypeActions();
}

/*!  Returns a list of grouped actions that corresponds to the
  StereoMode enum. If you want to create a menu in your
  application that controls the stereo mode used in
  QuarterWidget, add these actions to the menu.
 */
QList<QAction *>
QuarterWidget::stereoModeActions() const
{
  return PRIVATE(this)->stereoModeActions();
}

/*!  Returns a list of grouped actions that corresponds to the
  RenderMode enum. If you want to create a menu in your
  application that controls the render mode type used in
  QuarterWidget, add these actions to the menu.
 */
QList<QAction *>
QuarterWidget::renderModeActions() const
{
  return PRIVATE(this)->renderModeActions();
}

/*!
  \property QuarterWidget::navigationModeFile

  A url pointing to a navigation mode file which is a scxml file
  that defines the possible states for the Coin navigation system

  Supports:
  \li \b coin for internal Coin resources
  \li \b file for file system path to resources

  \sa scxml
*/

/*!
  Removes any navigationModeFile set.
*/
void
QuarterWidget::resetNavigationModeFile() {
  this->setNavigationModeFile(QUrl());
}

/**
 * Sets up the default cursors for the widget.
 */
void QuarterWidget::setupDefaultCursors()
{
    this->setStateCursor("interact", Qt::ArrowCursor);
    this->setStateCursor("idle", Qt::OpenHandCursor);
    this->setStateCursor("rotate", Qt::ClosedHandCursor);
    this->setStateCursor("pan", Qt::SizeAllCursor);
    this->setStateCursor("zoom", Qt::SizeVerCursor);
    this->setStateCursor("dolly", Qt::SizeVerCursor);
    this->setStateCursor("seek", Qt::CrossCursor);
    this->setStateCursor("spin", Qt::OpenHandCursor);
}

/*!
  Sets a navigation mode file. Supports the schemes "coin" and "file"

  \param[in] url URL to the resource
*/
void
QuarterWidget::setNavigationModeFile(const QUrl & url)
{
  QString filename;

  if (url.scheme()=="coin") {
    filename = url.path();

    //Workaround for differences between url scheme, and Coin internal
    //scheme in Coin 3.0.
    if (filename[0]=='/') {
      filename.remove(0,1);
    }

    filename = url.scheme()+':'+filename;
  }
  else if (url.scheme()=="file")
    filename = url.toLocalFile();
  else if (url.isEmpty()) {
    if (PRIVATE(this)->currentStateMachine) {
      this->removeStateMachine(PRIVATE(this)->currentStateMachine);
      delete PRIVATE(this)->currentStateMachine;
      PRIVATE(this)->currentStateMachine = nullptr;
      PRIVATE(this)->navigationModeFile = url;
    }
    return;
  }
  else {
    qDebug()<<url.scheme()<<"is not recognized";
    return;
  }

  QByteArray filenametmp = filename.toLocal8Bit();
  ScXMLStateMachine * stateMachine = nullptr;

  if (filenametmp.startsWith("coin:")){
    stateMachine = ScXML::readFile(filenametmp.data());
  }
  else {
    //Use Qt to read the file in case it is a Qt resource
    QFile file(filenametmp);
    if (file.open(QIODevice::ReadOnly)){
      QByteArray fileContents = file.readAll();
      stateMachine = ScXML::readBuffer(SbByteBuffer(fileContents.size(), fileContents.constData()));
      file.close();
    }
  }

  if (stateMachine &&
      stateMachine->isOfType(SoScXMLStateMachine::getClassTypeId())) {
    SoScXMLStateMachine * newsm = 
      static_cast<SoScXMLStateMachine *>(stateMachine);
    if (PRIVATE(this)->currentStateMachine) {
      this->removeStateMachine(PRIVATE(this)->currentStateMachine);
      delete PRIVATE(this)->currentStateMachine;
    }
    this->addStateMachine(newsm);
    newsm->initialize();
    PRIVATE(this)->currentStateMachine = newsm;
  }
  else {
    delete stateMachine;
    stateMachine = nullptr;
    qDebug()<<filename;
    qDebug()<<"Unable to load"<<url;
    return;
  }

  //If we have gotten this far, we have successfully loaded the
  //navigation file, so we set the property
  PRIVATE(this)->navigationModeFile = url;

  if (QUrl(DEFAULT_NAVIGATIONFILE) == PRIVATE(this)->navigationModeFile ) {

    // set up default cursors for the examiner navigation states
    //FIXME: It may be overly restrictive to not do this for arbitrary
    //navigation systems? - BFG 20090117
    setupDefaultCursors();
  }
}

/*!
  \retval The current navigation mode file
*/
const QUrl &
QuarterWidget::navigationModeFile() const
{
  return PRIVATE(this)->navigationModeFile;
}

#undef PRIVATE
