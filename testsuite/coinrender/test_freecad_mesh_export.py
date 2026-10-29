#!/usr/bin/env python3
"""Closed CAD surfaces must not lose faces during native viewport import."""
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('mesh_export',ROOT/'examples/coinrender/freecad_mesh_export.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
POINTS=[(0.,0.,0.),(1.,0.,0.),(0.,1.,0.),(0.,0.,1.)]
FACES=[(0,2,1),(0,1,3),(0,3,2),(1,2,3)]

class Shape:
    ShapeType='Solid'
    Volume=1/6
    def __init__(self,faces):self.faces=faces;self.calls=[]
    def isClosed(self):return True
    def tessellate(self,tolerance):self.calls.append(tolerance);return POINTS,self.faces
    def copy(self):return Shape(self.faces)

class MeshExportTest(unittest.TestCase):
    def test_closed_volume_translation_and_winding(self):
        report=module.triangle_mesh_report(POINTS,FACES)
        self.assertTrue(report['closed_solid']);self.assertAlmostEqual(report['signed_volume'],1/6)
        shifted=[tuple(v+1e6 for v in p) for p in POINTS]
        self.assertAlmostEqual(module.triangle_mesh_report(shifted,FACES)['signed_volume'],1/6)
        inverted=[tuple(reversed(f)) for f in FACES]
        self.assertFalse(module.triangle_mesh_report(POINTS,inverted)['closed_solid'])
    def test_missing_face_is_not_an_opaque_solid(self):
        report=module.triangle_mesh_report(POINTS,FACES[:-1])
        self.assertEqual(report['boundary_edges'],3);self.assertFalse(report['closed_solid'])
        reversed_face=FACES[:-1]+[tuple(reversed(FACES[-1]))]
        self.assertEqual(module.triangle_mesh_report(POINTS,reversed_face)['inconsistent_edges'],3)
        self.assertFalse(module.triangle_mesh_report(POINTS,FACES+[FACES[-1]])['closed_solid'])
    def test_valid_mesh_keeps_normal_path(self):
        source=Shape(FACES)
        def forbidden(**kwargs):self.fail('Healthy mesh should not be retessellated')
        vertices,faces,report=module.tessellate_for_viewport(source,mesh_factory=forbidden)
        self.assertIs(vertices,POINTS);self.assertIs(faces,FACES);self.assertFalse(report['repaired'])
        self.assertEqual(len(source.calls),1)
    def test_retry_accepts_only_closed_candidate(self):
        source=Shape(FACES[:-1]);calls=[]
        def factory(**kwargs):
            calls.append(kwargs);self.assertIsNot(kwargs['Shape'],source)
            return type('Mesh',(),{'Topology':(POINTS,FACES if len(calls)==2 else FACES[:-1])})()
        _,faces,report=module.tessellate_for_viewport(source,mesh_factory=factory)
        self.assertEqual(faces,FACES);self.assertTrue(report['repaired'])
        self.assertEqual([c['AngularDeflection'] for c in calls],[.2,.3])
        self.assertTrue(all(c['LinearDeflection']==.2 and not c['Relative'] for c in calls))
        self.assertEqual(source.faces,FACES[:-1]);self.assertEqual(len(report['attempts']),3)
    def test_bad_candidates_refuse_export(self):
        def factory(**kwargs):return type('Mesh',(),{'Topology':(POINTS,FACES[:-1])})()
        with self.assertRaisesRegex(ValueError,'Closed CAD solid produced an open'):
            module.tessellate_for_viewport(Shape(FACES[:-1]),mesh_factory=factory)
    def test_invalid_mesh_data(self):
        with self.assertRaisesRegex(ValueError,'triangle index'):
            module.triangle_mesh_report(POINTS,[(0,1,99)])
        with self.assertRaisesRegex(ValueError,'finite'):
            module.triangle_mesh_report([(float('nan'),0,0)],[(0,0,0)])
        self.assertFalse(module.triangle_mesh_report(POINTS,[(0,0,1)])['closed_solid'])

if __name__=='__main__':unittest.main()
