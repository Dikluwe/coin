"""Proposed materials and presentation for AssemblyExample, not a CAD BOM import.

All RGB values are scene-linear, as expected by SoPBRMaterial. Painted metal is
a dielectric coating; metallic=1 is reserved for the exposed metal surfaces.
The preset operates on the renderer's descriptors, never saves ViewObject data.
"""
import FreeCAD as App
from pivy import coin


def _material(color, metallic, roughness, coat=0, coat_roughness=.18):
    return dict(baseColor=color, metallic=metallic, roughness=roughness,
                clearcoat=coat, clearcoatRoughness=coat_roughness)


def assembly_example_materials(assembly):
    paint = _material((.78, .42, .025), 0, .28, .55, .16)
    graphite = _material((.025, .032, .043), 0, .3, .35, .2)
    steel = _material((.23, .26, .3), 1, .38)
    chrome = _material((.68, .71, .74), 1, .12)
    pins = _material((.48, .52, .57), 1, .22)
    result = {}
    for part in assembly.Group:
        if part.TypeId != "App::Link" or not hasattr(part, "Shape"):
            continue
        name = part.Name
        if "CylinderInner" in name:
            material = chrome
        elif "CylinderOuter" in name or name == "Base":
            material = graphite
        elif name == "BasePin":
            material = pins
        elif name == "Bucket" or name.startswith("BucketLink"):
            material = steel
        else:
            material = paint
        result[name] = dict(material)
    return result


def frame_assembly(view, assembly, detail=False):
    """Frame assembly geometry explicitly, excluding loose source parts/joints.

    Bounding-box queries prepare the view; they do not invoke scene capture or
    rendering. The worker still receives the one ordinary camera state packet.
    """
    bounds = App.BoundBox()
    placement = assembly.getGlobalPlacement()
    for part in assembly.Group:
        if part.TypeId != "App::Link" or not hasattr(part, "Shape") or not part.ViewObject.Visibility:
            continue
        if detail and part.Name not in ("Base", "BasePin", "BoomCylinderOuter", "BoomCylinderInner"):
            continue
        box = part.Shape.BoundBox
        for x in (box.XMin, box.XMax):
            for y in (box.YMin, box.YMax):
                for z in (box.ZMin, box.ZMax):
                    bounds.add(placement.multVec(App.Vector(x, y, z)))
    center = bounds.Center
    camera = view.getCameraNode()
    offset = App.Vector(1200, 850, 600)
    camera.position.setValue(*(center + offset))
    camera.pointAt(coin.SbVec3f(*center), coin.SbVec3f(0, 0, 1))
    camera.focalDistance.setValue(offset.Length)
    camera.nearDistance.setValue(1)
    camera.farDistance.setValue(5000)
    if hasattr(camera, "height"):
        camera.height.setValue(360 if detail else 760)
    else:
        camera.heightAngle.setValue(.23 if detail else .48)
