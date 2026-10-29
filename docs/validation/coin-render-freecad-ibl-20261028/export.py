import FreeCAD as App
import Part
import json
from pathlib import Path
out=Path('/tmp/freecad-retained-ibl')
doc=App.newDocument('RetainedPBRBracket')
base=Part.makeBox(60,42,12,App.Vector(-30,-21,0))
boss=Part.makeCylinder(17,24,App.Vector(0,0,12))
shape=base.fuse(boss).cut(Part.makeCylinder(9,40,App.Vector(0,0,-1)))
for x in (-22,22):
    for y in (-13,13):
        shape=shape.cut(Part.makeCylinder(3,16,App.Vector(x,y,-2)))
obj=doc.addObject('PartDesign::Feature','Bracket');obj.Shape=shape.removeSplitter();doc.recompute()
doc.saveAs(str(out/'bracket.FCStd'))
vertices,triangles=obj.Shape.tessellate(0.1)
with (out/'bracket.iv').open('w') as f:
    f.write('#Inventor V2.1 ascii\nSeparator {\nPerspectiveCamera { position 90 -115 100 orientation 1 0 0 0 nearDistance 1 farDistance 400 heightAngle 0.65 }\nLightModel { model PHONG }\nSoPBRMaterial { baseColor 0.8 0.58 0.25 metallic 1 roughness 0.28 }\nShapeHints { vertexOrdering COUNTERCLOCKWISE shapeType SOLID creaseAngle 0.6 }\nCoordinate3 { point [\n')
    for v in vertices:f.write(f'{v.x:.9g} {v.y:.9g} {v.z:.9g},\n')
    f.write('] }\nIndexedFaceSet { coordIndex [\n')
    for a,b,c in triangles:f.write(f'{a},{b},{c},-1,\n')
    f.write('] }\n}\n')
(out/'geometry.json').write_text(json.dumps(dict(freecad=App.Version(),vertices=len(vertices),triangles=len(triangles),valid=obj.Shape.isValid(),volume=obj.Shape.Volume),indent=2))
print((out/'geometry.json').read_text())
App.closeDocument(doc.Name)
