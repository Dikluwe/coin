"""Validate closed CAD solids before their one-time retained scene import.

This module has no Coin renderer dependency. Retessellation repairs an invalid
CAD-to-triangle result before scene capture; it never invokes capture/planning.
"""
import math


def triangle_mesh_report(vertices, triangles):
    points = [tuple(float(c) for c in vertex) for vertex in vertices]
    if not points or any(len(p) != 3 or not all(math.isfinite(c) for c in p) for p in points):
        raise ValueError("Mesh needs finite 3D vertices")
    edges = {}
    terms = []
    degenerate = 0
    origin = points[0]
    for face in triangles:
        if len(face) != 3 or any(not isinstance(i, int) or i < 0 or i >= len(points) for i in face):
            raise ValueError("Mesh has an invalid triangle index")
        a, b, c = face
        if len({a, b, c}) != 3:
            degenerate += 1
        pa, pb, pc = (tuple(points[i][k] - origin[k] for k in range(3)) for i in face)
        ab = tuple(pb[k] - pa[k] for k in range(3))
        ac = tuple(pc[k] - pa[k] for k in range(3))
        cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
        if a != b and b != c and c != a and all(v == 0 for v in cross):
            degenerate += 1
        terms.append(sum(pa[k] * cross[k] for k in range(3)) / 6)
        for u, v in ((a, b), (b, c), (c, a)):
            key = (min(u, v), max(u, v))
            entry = edges.setdefault(key, [0, 0])
            entry[0] += 1
            entry[1] += 1 if u < v else -1
    boundary = sum(count == 1 for count, _ in edges.values())
    nonmanifold = sum(count > 2 for count, _ in edges.values())
    inconsistent = sum(count == 2 and winding != 0 for count, winding in edges.values())
    volume = math.fsum(terms)
    return dict(vertices=len(points), triangles=len(triangles), boundary_edges=boundary,
                nonmanifold_edges=nonmanifold, inconsistent_edges=inconsistent,
                degenerate_triangles=degenerate, signed_volume=volume,
                closed_solid=bool(triangles) and not (boundary or nonmanifold or inconsistent or degenerate) and math.isfinite(volume) and volume > 0)


def tessellate_for_viewport(shape, linear_deflection=.2, mesh_factory=None):
    """Keep the normal mesh when valid; retry only a broken closed solid.

    OCC's default .1-radian meshing can omit a planar trimmed face of a valid
    BRep. Explicit alternate angular deflections repair AssemblyExample's link.
    Each candidate must be a closed, consistently oriented triangle manifold.
    No CAD topology/placement is modified; callers supply an independent copy.
    """
    vertices, triangles = shape.tessellate(linear_deflection)
    first = triangle_mesh_report(vertices, triangles)
    expects_solid = shape.ShapeType == "Solid" and shape.isClosed()
    attempts = [dict(method="Shape.tessellate", linear_deflection=linear_deflection, **first)]
    if expects_solid and not first["closed_solid"]:
        if mesh_factory is None:
            import MeshPart
            mesh_factory = MeshPart.meshFromShape
        for angle in (.2, .3):
            # A fresh copy prevents a failed candidate's triangulation cache
            # from determining the next result. The source document is untouched.
            mesh = mesh_factory(Shape=shape.copy(), LinearDeflection=linear_deflection,
                                AngularDeflection=angle, Relative=False)
            candidate_vertices, candidate_triangles = mesh.Topology
            report = triangle_mesh_report(candidate_vertices, candidate_triangles)
            attempts.append(dict(method="MeshPart.meshFromShape", angular_deflection=angle,
                                 linear_deflection=linear_deflection, **report))
            if report["closed_solid"]:
                vertices, triangles = candidate_vertices, candidate_triangles
                break
        else:
            last = attempts[-1]
            raise ValueError("Closed CAD solid produced an open or misoriented mesh: "
                             + str(last["boundary_edges"]) + " boundary edges, "
                             + str(last["inconsistent_edges"]) + " inconsistent edges")
    source_volume=float(shape.Volume) if expects_solid else None
    return vertices, triangles, dict(expects_solid=expects_solid, source_volume=source_volume, attempts=attempts,
                                    repaired=len(attempts) > 1)
