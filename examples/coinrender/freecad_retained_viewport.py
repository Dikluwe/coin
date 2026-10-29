"""Opt-in Assembly viewport using one isolated retained BGFX PBR/IBL renderer.

Load from a FreeCAD macro and call attach(worker, environment, assembly=None).
The existing FreeCAD graph is kept for picking and document interaction, with
its render manager deactivated. Only the worker captures/plans visible pixels.
On X11 the worker presents directly to the native viewport window. Qt handles
input/layout only. The old readback mode is explicit and retained for controls.
It supports rigid assembly links, object selection/preselection, camera changes,
visibility and placement edits. Face-specific shading, joints/gizmos and topology
edits are outside this profile. Reattach after changing source geometry.
"""
import FreeCAD as App
import FreeCADGui as Gui
from PySide6 import QtCore, QtGui, QtWidgets
from pivy import coin
from shiboken6 import isValid
from pathlib import Path
import json
import os
import time
import math
import tempfile
import shutil
import weakref
from freecad_mesh_export import tessellate_for_viewport


def _iv_quote(value):
    return '"'+str(value).replace(chr(92),chr(92)*2).replace('"',chr(92)+'"')+'"'


def _numbers(values):
    values = tuple(float(v) for v in values)
    if not all(math.isfinite(v) for v in values):
        raise ValueError("Non-finite viewport state")
    return " ".join(format(v, ".9g") for v in values)


def _camera(view):
    node = view.getCameraNode()
    perspective = hasattr(node, "heightAngle")
    return [int(perspective), *node.position.getValue(),
            *node.orientation.getValue().getValue(), node.nearDistance.getValue(),
            node.farDistance.getValue(), node.focalDistance.getValue(),
            (node.heightAngle if perspective else node.height).getValue()]


class _DocumentLifetime:
    def __init__(self, owner):
        self.owner = weakref.ref(owner)

    def slotDeletedDocument(self, document):
        # Gui emits this signal before deleting the document's views. The App
        # deleted-document notification runs after Gui's deletion handler.
        owner = self.owner()
        if owner is not None and document.Document == owner.document:
            owner._retire_native_surface(document_deleted=True)


class _Canvas(QtWidgets.QWidget):
    def __init__(self, owner, parent):
        super().__init__(parent)
        self.owner = owner
        self.image = QtGui.QImage()
        self.setObjectName("CoinRetainedPbrViewport")
        self.setMouseTracking(True)
        self.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.setAttribute(QtCore.Qt.WA_OpaquePaintEvent)
        if owner.presentation == "native":
            self.setAttribute(QtCore.Qt.WA_NativeWindow)
            self.setAttribute(QtCore.Qt.WA_PaintOnScreen)
            self.setAttribute(QtCore.Qt.WA_NoSystemBackground)
        self.drag = None

    def paintEngine(self):
        if self.owner.presentation == "native":
            return None
        return super().paintEngine()

    def paintEvent(self, event):
        if self.owner.presentation == "native":
            # Exposure republishes the retained frame without a Qt pixel copy.
            if not self.owner.closed:
                self.owner.poll(force=True)
            return
        painter = QtGui.QPainter(self)
        painter.fillRect(self.rect(), QtCore.Qt.black)
        if not self.image.isNull():
            # Keep the previous publication at its original pixel size until a
            # matching resized frame arrives; never distort picking coordinates.
            painter.drawImage(0, 0, self.image)
        else:
            painter.setPen(QtCore.Qt.white)
            painter.drawText(20, 30, "Preparing retained PBR viewport…")

    def mousePressEvent(self, event):
        self.setFocus()
        if event.button() == QtCore.Qt.LeftButton:
            self.owner.pick(event.position(), select=True)
        else:
            self.drag = event.position()
        event.accept()

    def mouseReleaseEvent(self, event):
        self.drag = None
        event.accept()

    def mouseMoveEvent(self, event):
        if self.drag is not None and event.buttons() & QtCore.Qt.RightButton:
            delta = event.position() - self.drag
            self.drag = event.position()
            rotation = App.Rotation(App.Vector(0, 0, 1), -delta.x() * .35)
            rotation = rotation * App.Rotation(App.Vector(1, 0, 0), -delta.y() * .35)
            self.owner.view.getCameraNode().orientation.setValue(
                *(rotation * self.owner.view.getCameraOrientation()).Q)
        elif self.drag is not None and event.buttons() & QtCore.Qt.MiddleButton:
            delta = event.position() - self.drag
            self.drag = event.position()
            node = self.owner.view.getCameraNode()
            scale = (node.focalDistance.getValue() if hasattr(node, "heightAngle")
                     else node.height.getValue()) / max(1, self.height())
            shift = self.owner.view.getCameraOrientation().multVec(
                App.Vector(-delta.x() * scale, delta.y() * scale, 0))
            node.position.setValue(*(a + b for a, b in zip(node.position.getValue(), shift)))
        else:
            self.owner.pick(event.position())
        event.accept()

    def wheelEvent(self, event):
        node = self.owner.view.getCameraNode()
        scale = math.exp(-event.angleDelta().y() / 1200)
        if hasattr(node, "height"):
            node.height.setValue(max(.001, node.height.getValue() * scale))
        else:
            node.heightAngle.setValue(min(2.8, max(.01, node.heightAngle.getValue() * scale)))
        event.accept()

    def leaveEvent(self, event):
        Gui.Selection.clearPreselection()

    def keyPressEvent(self, event):
        if event.key() == QtCore.Qt.Key_Escape:
            self.owner.close()
        elif event.key() == QtCore.Qt.Key_F:
            self.owner.view.fitAll()
        else:
            super().keyPressEvent(event)


class RetainedViewport(QtCore.QObject):
    frameReady = QtCore.Signal(object)
    failed = QtCore.Signal(str)

    def __init__(self, worker, environment, assembly=None, directory=None, record=False, presentation="native", material_overrides=None, effects=None, studio_setup=False, surface_maps=None):
        super().__init__(Gui.getMainWindow())
        if presentation not in ("native", "readback"):
            raise ValueError("Unknown viewport presentation")
        if presentation == "native" and QtGui.QGuiApplication.platformName() != "xcb":
            raise ValueError("The native BGFX FreeCAD viewport currently requires X11/xcb")
        self.effects = dict(planarReflection=False, roughPlanarReflection=False, reflectionScale=2, reflectionPlane=(0.,0.,1.,0.),
                            reflectionBudgetBytes=64*1024*1024, reflectionPlaneTolerance=.0001, shadows=False, ambientOcclusion=False, background=False,
                            occlusionRadius=20., occlusionStrength=.85, shadowMapSize=1024,
                            shadowBias=.0004, resourceBudgetBytes=128*1024*1024,
                            backgroundTop=(.22,.27,.34), backgroundBottom=(.68,.71,.75))
        if set(effects or {}) - set(self.effects):
            raise ValueError("Unknown viewport effect")
        self.effects.update(effects or {})
        self._effect_packet()
        self._reflection_packet()
        self.studio_setup = studio_setup
        self.presentation = presentation
        self.presented_size = QtCore.QSize()
        self._pick_size = QtCore.QSize()
        self.surface_destroy_events = 0
        self.surface_retirement_fences = 0
        self.surface_retired_before_destroy = False
        self._document_deleted = False
        self._ui_restored = False
        self.document = App.ActiveDocument
        if self.document is None:
            raise ValueError("Open an assembly document first")
        if assembly is None:
            assemblies = [o for o in self.document.Objects if o.TypeId == "Assembly::AssemblyObject"]
            if len(assemblies) != 1:
                raise ValueError("Choose one Assembly::AssemblyObject")
            assembly = assemblies[0]
        self.assembly = assembly
        self.parts = [o for o in assembly.Group if o.TypeId == "App::Link" and hasattr(o, "Shape")]
        if not self.parts:
            raise ValueError("The assembly has no shape links")
        self.surface_maps={name:dict(values) for name,values in (surface_maps or {}).items()}
        if set(self.surface_maps)-{part.Name for part in self.parts}:
            raise ValueError("Unknown surface-map link")
        for name,mapping in self.surface_maps.items():
            if set(mapping)-{"baseColor","orm","emissive","normal","directionS","directionT"}:
                raise ValueError("Unknown surface-map field for "+name)
            for key in ("baseColor","orm","emissive","normal"):
                if key in mapping:
                    path=Path(mapping[key]).resolve()
                    if not path.is_file():raise ValueError("Surface image does not exist: "+str(path))
                    mapping[key]=str(path)
            for key,default in (("directionS",(.02,0,0)),("directionT",(0,.02,0))):
                values=tuple(float(v) for v in mapping.get(key,default))
                if len(values)!=3 or any(not math.isfinite(v) for v in values) or sum(v*v for v in values)==0:
                    raise ValueError("Invalid surface projection for "+name)
                mapping[key]=values
        self.material_overrides = {name: dict(values) for name, values in (material_overrides or {}).items()}
        unknown = set(self.material_overrides) - {part.Name for part in self.parts}
        if unknown:
            raise ValueError("Unknown material override links: " + ", ".join(sorted(unknown)))
        # Validate all authoring before taking over the UI or starting a worker.
        for part in self.parts:
            self.material_for(part)
        if any(getattr(o, "LinkScale", 1) != 1 or
               any(abs(v-1) > 1e-7 for v in getattr(o, "ScaleVector", (1, 1, 1)))
               for o in self.parts):
            raise ValueError("The viewport profile requires rigid, unscaled links")
        self.worker = str(Path(worker).resolve())
        self.environment = str(Path(environment).resolve())
        if not Path(self.worker).is_file() or not Path(self.environment).is_file():
            raise ValueError("Worker executable or IBL package is missing")
        self.record_frames = record
        self._temporary_directory = directory is None
        self.directory = Path(directory or tempfile.mkdtemp(prefix="coin-freecad-viewport-"))
        self.directory.mkdir(parents=True, exist_ok=True)
        if (self.directory / "scene.iv").exists():
            raise ValueError("Use a fresh viewport session directory")
        self.view = Gui.activeDocument().activeView()
        self.closed = False
        self.busy = False
        self.frames = []
        self.last_error = ""
        self.last_key = None
        self.sequence = 0
        self.environment_rotation = 0
        self.environment_intensity = 1
        self.exposure = 0
        self.mesh_builds = 0
        self.mesh_reports = {}
        self.tessellation_attempts = 0
        self._stdout = b""
        self._stderr = b""
        self._request = None
        self.manager = self.view.getViewer().getSoRenderManager()
        # Find the actual Quarter viewport belonging to the active document.
        area = Gui.getMainWindow().findChild(QtWidgets.QMdiArea)
        window = area.activeSubWindow() if area else None
        candidates = [w for w in (window.findChildren(QtWidgets.QGraphicsView) if window else [])
                      if "View3DInventorViewer" in w.metaObject().className()]
        if len(candidates) != 1:
            raise ValueError("Activate the assembly's 3D view before attaching")
        self.quarter = candidates[0]
        self._view_window = window
        self._gui_observer = None
        self.native = self.quarter.viewport()
        self._native_updates = self.native.updatesEnabled()
        self._native_visible = self.native.isVisible()
        self.legacy_render_calls = 0
        self._audit_node = coin.SoCallback()
        self._audit_node.setCallback(self._audit_legacy_render)
        self._source_root = self.view.getSceneGraph()
        self._other_roots = []
        self._export_scene()
        self.process = QtCore.QProcess(self)
        self.process.setProcessChannelMode(QtCore.QProcess.SeparateChannels)
        self.process.readyReadStandardOutput.connect(self._receive)
        self.process.readyReadStandardError.connect(self._read_errors)
        self.process.errorOccurred.connect(lambda error: self._fatal(self.process.errorString()) if not self.closed else None)
        self.process.finished.connect(self._finished)
        self.canvas = _Canvas(self, self.quarter)
        self.canvas.setGeometry(self.native.geometry())
        self.canvas.show()
        self.canvas.raise_()
        self._surface_handle = None
        self._overlay_masks = []
        self.native_window = 0
        if self.presentation == "native":
            self.native_window = int(self.canvas.winId())
            self._surface_handle = self.canvas.windowHandle()
            self._surface_handle.installEventFilter(self)
            QtGui.QGuiApplication.sync()
            # FreeCAD's floating dock overlays cache the GL viewport behind
            # their transparent areas. Those caches cannot composite a foreign
            # Vulkan child surface. Keep docked panels usable; mask only the
            # overlaid containers intersecting this view, restoring exact masks
            # on detach. Overlay/gizmo composition is a separate profile.
            bounds = QtCore.QRect(self.canvas.mapToGlobal(QtCore.QPoint()), self.canvas.size())
            for widget in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
                if widget.metaObject().className() != "Gui::OverlayTabWidget" or not widget.isVisible():
                    continue
                rectangle = QtCore.QRect(widget.mapToGlobal(QtCore.QPoint()), widget.size())
                if bounds.intersects(rectangle):
                    self._overlay_masks.append((widget, widget.mask()))
                    widget.setMask(QtGui.QRegion(-2, -2, 1, 1))
        self.quarter.installEventFilter(self)
        self._view_window.installEventFilter(self)
        self.native.installEventFilter(self)
        self.manager.deactivate()
        self.native.setUpdatesEnabled(False)
        if self.presentation == "native":
            # A disabled QOpenGLWidget can still contribute its old texture to
            # Qt's top-level composition. Hide that surface while BGFX owns the
            # pixels, keeping Quarter and its graph available for picking.
            self.native.hide()
            self.canvas.raise_()
        self._source_root.ref()
        self._source_root.insertChild(self._audit_node, 0)
        self.quarter.destroyed.connect(lambda: self.close(document_deleted=True))
        # Source parts must not also be picked underneath the assembly links.
        for root in self.document.RootObjects:
            if root != assembly and root.ViewObject.Visibility:
                self._other_roots.append(root)
                root.ViewObject.Visibility = False
        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(33)
        self.timer.timeout.connect(self.poll)
        arguments = [str(self.directory / "scene.iv"), self.environment,
                           str(self.directory), str(len(self.parts)),
                           str(self._frame_size().width()), str(self._frame_size().height())]
        if self.presentation == "native":
            arguments.extend(["--window", str(self.native_window)])
        if os.environ.get("COIN_VIEWPORT_METRICS") == "1":
            arguments.append("--metrics")
        self.process.start(self.worker, arguments)
        self.process.started.connect(lambda: self.poll(force=True))
        self.timer.start()
        App.addDocumentObserver(self)
        if self.presentation == "native":
            self._gui_observer = _DocumentLifetime(self)
            Gui.addDocumentObserver(self._gui_observer)
            QtWidgets.QApplication.instance().aboutToQuit.connect(self._quit_application)

    def _export_scene(self):
        """One document-to-node import; no Coin capture or render planning here."""
        camera = _camera(self.view)
        kind = "PerspectiveCamera" if camera[0] else "OrthographicCamera"
        with (self.directory / "scene.iv").open("w") as out:
            out.write("#Inventor V2.1 ascii\nSeparator {\nDEF ViewportCamera " + kind + " {}\n")
            out.write("LightModel { model PHONG }\n")
            if self.studio_setup:
                out.write("DirectionalLight { direction -0.45 -0.65 -1 intensity 1.7 color 1 .98 .94 }\n")
            for i, obj in enumerate(self.parts):
                shape = obj.Shape.copy()
                shape.Placement = App.Placement()
                try:
                    vertices, triangles, report = tessellate_for_viewport(shape)
                except ValueError as error:
                    raise ValueError("Invalid viewport mesh for " + obj.Name + ": " + str(error)) from error
                self.mesh_reports[obj.Name] = report
                self.tessellation_attempts += len(report["attempts"])
                if not triangles:
                    raise ValueError("Empty assembly link: " + obj.Name)
                material = self.material_for(obj)
                maps = self.surface_maps.get(obj.Name,{})
                if obj.LinkedObject.ViewObject.Transparency:
                    raise ValueError("Transparent assembly surfaces are outside the PBR viewport profile")
                out.write(f"DEF Visible_{i} Switch {{ whichChild -3 Separator {{\n")
                out.write(f"DEF Placement_{i} Translation {{}}\nDEF Orientation_{i} Rotation {{}}\n")
                out.write(f"DEF Material_{i} SoPBRMaterial {{ baseColor {_numbers(material['baseColor'])} "
                          + " ".join(f"{key} {material[key]:.9g}" for key in
                                     ("metallic", "roughness", "clearcoat", "clearcoatRoughness", "normalScale", "anisotropy", "anisotropyRotation"))
                          + " emissive " + _numbers(material["emissive"])
                          + f" normalYFlip {'TRUE' if material['normalYFlip'] else 'FALSE'} "
                          + " ".join(f"{field}TextureUnit {unit}" for field,unit in (("baseColor",0),("orm",1),("emissive",2)) if field in maps)
                          + " }\n")
                if maps or material["anisotropy"]>0:
                    for field,unit in (("baseColor",0),("orm",1),("emissive",2)):
                        if field not in maps:continue
                        out.write(f'TextureUnit {{ unit {unit} }} Texture2 {{ filename {_iv_quote(maps[field])} }}\n')
                        out.write('TextureCoordinatePlane { directionS '+_numbers(maps.get('directionS',(.02,0,0)))+' directionT '+_numbers(maps.get('directionT',(0,.02,0)))+' }\n')
                    out.write('TextureUnit { unit 0 }\n')
                    out.write('TextureCoordinatePlane { directionS '+_numbers(maps.get('directionS',(.02,0,0)))+' directionT '+_numbers(maps.get('directionT',(0,.02,0)))+' }\n')
                    if 'normal' in maps:
                        out.write(f'BumpMap {{ filename {_iv_quote(maps["normal"])} }}\n')
                        out.write('TextureCoordinatePlane { directionS '+_numbers(maps.get('directionS',(.02,0,0)))+' directionT '+_numbers(maps.get('directionT',(0,.02,0)))+' }\n')
                out.write("ShapeHints { vertexOrdering COUNTERCLOCKWISE shapeType SOLID creaseAngle 0.6 }\nCoordinate3 { point [\n")
                for vertex in vertices:
                    out.write(_numbers(vertex) + ",\n")
                out.write("] }\nIndexedFaceSet { coordIndex [\n")
                for a, b, c in triangles:
                    out.write(f"{a},{b},{c},-1,\n")
                out.write("] }\n} }\n")
                self.mesh_builds += 1
            (self.directory / "mesh-reports.json").write_text(json.dumps(self.mesh_reports,indent=2))
            if self.studio_setup:
                # Presentation geometry joins the same retained root and plan.
                bounds = App.BoundBox()
                placement = self.assembly.getGlobalPlacement()
                for part in self.parts:
                    box = part.Shape.BoundBox
                    for px in (box.XMin,box.XMax):
                        for py in (box.YMin,box.YMax):
                            for pz in (box.ZMin,box.ZMax):
                                bounds.add(placement.multVec(App.Vector(px,py,pz)))
                x,y,z = bounds.Center.x,bounds.Center.y,bounds.ZMin-.5
                points = [(x-800,y-800,z),(x+800,y-800,z),(x+800,y+800,z),(x-800,y+800,z)]
                self.effects["reflectionPlane"]=(0.,0.,1.,float(format(z,".9g")))
                floor_material=("baseColor .55 .58 .62 metallic 1 roughness "+(".32" if self.effects["roughPlanarReflection"] else ".035")) if self.effects["planarReflection"] else "baseColor .23 .25 .28 metallic 0 roughness .9"
                out.write("Separator { SoPBRMaterial { "+floor_material+" } "
                          "ShapeHints { vertexOrdering COUNTERCLOCKWISE shapeType SOLID } Coordinate3 { point [")
                out.write(",".join(_numbers(point) for point in points))
                out.write("] } IndexedFaceSet { coordIndex [0,1,2,-1,0,2,3,-1] } }\n")
            out.write("}\n")

    def _effect_packet(self):
        e = self.effects
        for key in ("shadows", "ambientOcclusion", "background"):
            if e[key] not in (False, True):
                raise ValueError("Invalid effect flag: " + key)
        for key, lo, hi in (("occlusionRadius", 1e-9, 1e6), ("occlusionStrength",0,1), ("shadowBias",0,.05)):
            if not math.isfinite(e[key]) or not lo <= e[key] <= hi:
                raise ValueError("Invalid effect parameter: " + key)
        size = e["shadowMapSize"]
        if not isinstance(size,int) or not 128 <= size <= 4096 or size & (size-1):
            raise ValueError("Invalid shadow map size")
        budget = e["resourceBudgetBytes"]
        if not isinstance(budget,int) or not 0 < budget < 2**64:
            raise ValueError("Invalid effects budget")
        colors = [*e["backgroundTop"], *e["backgroundBottom"]]
        if len(e["backgroundTop"]) != 3 or len(e["backgroundBottom"]) != 3 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in colors):
            raise ValueError("Invalid background color")
        return "FX " + _numbers([int(e[k]) for k in ("shadows","ambientOcclusion","background")]) + " " + _numbers([
            e["occlusionRadius"], e["occlusionStrength"], size, e["shadowBias"]]) + " " + str(budget) + " " + _numbers(colors)

    def _reflection_packet(self):
        e=self.effects
        plane=tuple(float(v) for v in e["reflectionPlane"])
        if len(plane)!=4 or any(not math.isfinite(v) for v in plane) or abs(sum(v*v for v in plane[:3])-1)>.0001:
            raise ValueError("Invalid planar reflection plane")
        if not isinstance(e["roughPlanarReflection"],bool) or not isinstance(e["planarReflection"],bool) or e["reflectionScale"] not in (1,2,4):
            raise ValueError("Invalid planar reflection mode/scale")
        if not isinstance(e["reflectionBudgetBytes"],int) or not 0<e["reflectionBudgetBytes"]<2**64:
            raise ValueError("Invalid planar reflection budget")
        tolerance=float(e["reflectionPlaneTolerance"])
        if not math.isfinite(tolerance) or tolerance<=0:raise ValueError("Invalid reflection plane tolerance")
        return "REFL "+_numbers([int(e["planarReflection"]),*plane,e["reflectionScale"]])+" "+str(e["reflectionBudgetBytes"])+" "+_numbers([tolerance])+" ROUGH "+str(int(e["roughPlanarReflection"]))

    def material_for(self, obj):
        values = dict(baseColor=tuple(obj.LinkedObject.ViewObject.ShapeColor[:3]), emissive=(0.,0.,0.),
                      metallic=.65, roughness=.35, clearcoat=0., clearcoatRoughness=0., normalScale=1., normalYFlip=False, anisotropy=0., anisotropyRotation=0.)
        override = self.material_overrides.get(obj.Name, {})
        if set(override) - set(values):
            raise ValueError("Unknown PBR material field for " + obj.Name)
        values.update(override)
        color = tuple(float(v) for v in values["baseColor"])
        if len(color) != 3 or any(not math.isfinite(v) or v < 0 or v > 1 for v in color):
            raise ValueError("Invalid PBR base color for " + obj.Name)
        values["baseColor"] = color
        emission=tuple(float(v) for v in values["emissive"])
        if len(emission)!=3 or any(not math.isfinite(v) or v<0 or v>65503 for v in emission):
            raise ValueError("Invalid PBR emission for "+obj.Name)
        values["emissive"]=emission
        for key in ("metallic", "roughness", "clearcoat", "clearcoatRoughness", "anisotropy"):
            values[key] = float(values[key])
            if not math.isfinite(values[key]) or not 0 <= values[key] <= 1:
                raise ValueError("Invalid PBR " + key + " for " + obj.Name)
        values['anisotropyRotation']=float(values['anisotropyRotation'])
        if not math.isfinite(values['anisotropyRotation']):raise ValueError('Invalid anisotropy rotation')
        values['normalScale']=float(values['normalScale'])
        if not math.isfinite(values['normalScale']) or not 0<=values['normalScale']<=65504:
            raise ValueError("Invalid normal scale for "+obj.Name)
        if values['normalYFlip'] not in (True,False):raise ValueError("Invalid normal Y flip")
        return values

    def _matches(self, selection, part):
        if selection is None:
            return False
        if getattr(selection, "DocumentName", self.document.Name) != self.document.Name:
            return False
        name = getattr(selection, "ObjectName", "")
        if name == part.Name:
            return True
        subs = getattr(selection, "SubElementNames", ())
        return name == self.assembly.Name and (not subs or any(part.Name in s.split(".") for s in subs))

    def _frame_size(self):
        scale = self.canvas.devicePixelRatioF()
        return QtCore.QSize(max(1, round(self.canvas.width()*scale)), max(1, round(self.canvas.height()*scale)))

    def poll(self, force=False):
        if self.closed or self.busy or self.process.state() != QtCore.QProcess.Running:
            return
        if self.presentation == "native" and (not self.canvas.isVisible() or self.canvas.window().isMinimized()):
            return
        try:
            self.manager.deactivate()
            selected = Gui.Selection.getSelectionEx()
            hover = Gui.Selection.getPreselection()
            size = self._frame_size()
            if size != self._pick_size:
                # The disabled GL widget no longer maintains the pick viewport.
                # Updating this metadata does not invoke its render action.
                viewport = coin.SbViewportRegion(size.width(), size.height())
                self.manager.setViewportRegion(viewport)
                self.view.getViewer().getSoEventManager().setViewportRegion(viewport)
                self._pick_size = size
            fields = [size.width(), size.height(), *_camera(self.view),
                      self.environment_rotation, self.environment_intensity, self.exposure]
            for part in self.parts:
                # Shape placement includes the linked source's own placement;
                # assembly parent placement is composed explicitly.
                placement = self.assembly.getGlobalPlacement() * part.Shape.Placement
                fields.extend([*placement.Base, *placement.Rotation.Q,
                               int(self.assembly.ViewObject.Visibility and part.ViewObject.Visibility),
                               int(any(self._matches(item, part) for item in selected)),
                               int(self._matches(hover, part))])
            material_fields=[]
            anisotropy_fields=[]
            for part in self.parts:
                material=self.material_for(part)
                anisotropy_fields.extend([material["anisotropy"],material["anisotropyRotation"]])
                material_fields.extend([*material['baseColor'],*material['emissive'],material['metallic'],material['roughness'],
                    material['clearcoat'],material['clearcoatRoughness'],material['normalScale'],int(material['normalYFlip'])])
            key = _numbers(fields) + " " + self._effect_packet() + " PBR " + _numbers(material_fields) + " ANIS " + _numbers(anisotropy_fields) + " " + self._reflection_packet()
            if key == self.last_key and not force:
                return
            self.sequence += 1
            command = f"FRAME {self.sequence} {key} END\n"
            self._request = command
            self.last_key = key
            self.busy = True
            self._request_started = time.perf_counter()
            self.process.write(command.encode("ascii"))
            if self.record_frames:
                with (self.directory / "requests.txt").open("a") as log:
                    log.write(command)
        except Exception as error:
            self._fatal(str(error))

    def _audit_legacy_render(self, userdata, action):
        if action.isOfType(coin.SoGLRenderAction.getClassTypeId()):
            self.legacy_render_calls += 1

    def _read_errors(self):
        self._stderr = (self._stderr + bytes(self.process.readAllStandardError()))[-65536:]
        (self.directory / "worker.stderr").write_bytes(self._stderr)

    def _receive(self):
        self._stdout += bytes(self.process.readAllStandardOutput())
        if self.closed:
            self._stdout = b""
            return
        while b"\n" in self._stdout:
            line, self._stdout = self._stdout.split(b"\n", 1)
            try:
                result = json.loads(line)
                self.busy = False
                if result["status"] != "ok":
                    self.last_error = result["error"]
                    self.failed.emit(self.last_error)
                    continue
                width, height = result["width"], result["height"]
                self.presented_size = QtCore.QSize(width, height)
                if self.presentation == "native":
                    if (result["native_window"] != self.native_window or
                        result["pixel_readbacks"] != 0 or result["cpu_pixel_bytes"] != 0):
                        raise ValueError("Native presentation unexpectedly used CPU pixel transport")
                else:
                    path = self.directory / f"frame-{result['id']}.rgba"
                    raw = path.read_bytes()
                    if len(raw) != width * height * 4:
                        raise ValueError("Incomplete retained viewport image")
                    image = QtGui.QImage(raw, width, height, width * 4, QtGui.QImage.Format_RGBA8888).copy()
                    image.setDevicePixelRatio(self.canvas.devicePixelRatioF())
                    self.canvas.image = image
                    self.canvas.update()
                result["mesh_builds"] = self.mesh_builds
                result["tessellation_attempts"] = self.tessellation_attempts
                result["legacy_render_calls"] = self.legacy_render_calls
                result["legacy_updates_enabled"] = self.native.updatesEnabled()
                result["qt_image_bytes"] = self.canvas.image.sizeInBytes()
                if "render_ms" in result:
                    result["ack_ms"] = (time.perf_counter()-self._request_started)*1000
                self.frames.append(result)
                if self.record_frames:
                    with (self.directory / "frames.jsonl").open("a") as log:
                        log.write(json.dumps(result) + "\n")
                else:
                    self.frames = self.frames[-64:]
                    if self.presentation == "readback":
                        path.unlink(missing_ok=True)
                        path.with_suffix(".depth").unlink(missing_ok=True)
                self.last_error = ""
                self.frameReady.emit(result)
            except Exception as error:
                self._fatal(str(error))

    def pick(self, position, select=False):
        # During a resize wait for the matching publication, so a click cannot
        # select an object at coordinates from a differently sized frame.
        if self.presented_size != self._frame_size() or self.busy:
            return
        self.poll()
        if self.busy or self.closed:
            return
        info = self.pick_info(position)
        if select:
            Gui.Selection.clearSelection()
            if info:
                Gui.Selection.addSelection(self.document.Name, info["Object"], info.get("Component", ""))
        elif info:
            Gui.Selection.setPreselection(self.document.getObject(info["Object"]), info.get("Component", ""),
                                         info.get("x", 0), info.get("y", 0), info.get("z", 0))
        else:
            Gui.Selection.clearPreselection()

    def pick_info(self, position):
        scale = self.canvas.devicePixelRatioF()
        return self.view.getObjectInfo((int(position.x()*scale),
                                       self._frame_size().height()-int(position.y()*scale)-1))

    def eventFilter(self, watched, event):
        if (watched == self._surface_handle and event.type() == QtCore.QEvent.PlatformSurface and
            event.surfaceEventType() == QtGui.QPlatformSurfaceEvent.SurfaceAboutToBeDestroyed):
            # The target must die before Qt destroys its XID. Ordinary detach is
            # asynchronous; actual surface teardown needs this bounded fence.
            self.surface_destroy_events += 1
            if self.process.state() != QtCore.QProcess.NotRunning:
                self._retire_native_surface(document_deleted=True)
            self.surface_retired_before_destroy = self.process.state() == QtCore.QProcess.NotRunning
        if (self.presentation == "native" and watched == self._view_window and
            event.type() == QtCore.QEvent.Close and not self.closed):
            self._retire_native_surface(document_deleted=False)
        if not self.closed and watched == self.quarter and event.type() == QtCore.QEvent.Resize:
            QtCore.QTimer.singleShot(0, self._resize)
        if not self.closed and watched == self.native and event.type() == QtCore.QEvent.Paint:
            return True
        return False

    def _resize(self):
        if not self.closed:
            self.canvas.setGeometry(self.native.geometry())
            self.canvas.raise_()

    def slotDeletedDocument(self, document):
        if document == self.document:
            self.close(document_deleted=True)

    def slotChangedObject(self, obj, prop):
        if self.closed:
            return
        sources = [part.LinkedObject for part in self.parts]
        if ((obj in self.parts and prop in ("LinkedObject", "Scale", "ScaleVector", "LinkTransform")) or
            (obj in sources and prop == "Shape") or
            (obj == self.assembly and prop == "Group")):
            self._fatal("Assembly geometry/profile changed; reattach the retained viewport")

    def _finished(self, code, status):
        if not self.closed:
            self._fatal("Retained renderer exited: " + str(code))
        self._restore_ui()
        if self._temporary_directory and not self.record_frames:
            shutil.rmtree(self.directory, ignore_errors=True)

    def _quit_application(self):
        if not self.closed:
            self._retire_native_surface(document_deleted=True)

    def _retire_native_surface(self, document_deleted):
        if self.closed and self.process.state() == QtCore.QProcess.NotRunning:
            return
        self.surface_retirement_fences += 1
        self.close(document_deleted=document_deleted)
        if self.process.state() != QtCore.QProcess.NotRunning and not self.process.waitForFinished(2000):
            self.process.kill()
            self.process.waitForFinished(1000)
        self.surface_retired_before_destroy = (self.process.state() == QtCore.QProcess.NotRunning and
                                               isValid(self.canvas) and
                                               int(self.canvas.effectiveWinId()) == self.native_window)

    def _remove_gui_observer(self):
        if self._gui_observer is not None:
            Gui.removeDocumentObserver(self._gui_observer)
            self._gui_observer = None

    def _fatal(self, message):
        self.last_error = message
        self.failed.emit(message)
        App.Console.PrintError("Retained viewport: " + message + "\n")
        self.close()

    def close(self, document_deleted=False):
        self._document_deleted = self._document_deleted or document_deleted
        if self.closed:
            return
        self.closed = True
        self.timer.stop()
        App.removeDocumentObserver(self)
        # Avoid deleting Gui's observer while it is executing its own callback.
        QtCore.QTimer.singleShot(0, self._remove_gui_observer)
        self.process.write(b"QUIT\n")
        self.process.closeWriteChannel()
        # Asynchronous retirement; Qt remains responsive even if a GPU is busy.
        QtCore.QTimer.singleShot(2000, lambda: self.process.kill()
                                if self.process.state() != QtCore.QProcess.NotRunning else None)
        self._source_root.removeChild(self._audit_node)
        self._source_root.unref()
        if self.process.state() == QtCore.QProcess.NotRunning:
            self._restore_ui()

    def _restore_ui(self):
        for widget, mask in self._overlay_masks:
            if isValid(widget):
                widget.setMask(mask)
        self._overlay_masks.clear()
        if self._document_deleted or self._ui_restored:
            return
        self._ui_restored = True
        if self.presentation == "native" and self._surface_handle is not None:
            self._surface_handle.removeEventFilter(self)
        if not self._document_deleted:
            self.quarter.removeEventFilter(self)
            self._view_window.removeEventFilter(self)
            self.native.removeEventFilter(self)
            self.canvas.image = QtGui.QImage()
            self.canvas.hide()
            self.canvas.deleteLater()
            for root in self._other_roots:
                root.ViewObject.Visibility = True
            self.native.setUpdatesEnabled(self._native_updates)
            if self._native_visible:
                self.native.show()
            self.manager.activate()
            self.quarter.viewport().update()


_active = None


def attach(worker, environment, assembly=None, directory=None, record=False, presentation="native", material_overrides=None, effects=None, studio_setup=False, surface_maps=None):
    """Attach to the active Assembly 3D view; Escape or close() restores Coin/GL."""
    global _active
    if _active is not None:
        _active.close()
        if _active.process.state() != QtCore.QProcess.NotRunning and not _active.process.waitForFinished(2000):
            _active.process.kill()
            _active.process.waitForFinished(1000)
    _active = RetainedViewport(worker, environment, assembly, directory, record, presentation, material_overrides, effects, studio_setup, surface_maps)
    return _active
