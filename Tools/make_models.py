#!/usr/bin/env python3
"""Builds the models the vehicle templates use, as Wavefront OBJ files: a car and its
wheels, a light aeroplane, trees, a race track, hills. Everything is made of a few
operations (stretch a skin over rings, turn a profile around an axle, add a box), so the
shapes can be changed by changing numbers. Used by make_templates.py."""
import math, os, random

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def unit(a):
    length = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / length, a[1] / length, a[2] / length)

def turn(degrees, axis):
    """A rotation, as the scene file writes it: x, y, z, w."""
    half = math.radians(degrees) / 2
    return (axis[0] * math.sin(half), axis[1] * math.sin(half), axis[2] * math.sin(half), math.cos(half))

def then(a, b):
    """The rotation that does b first and a after."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)

def turned(q, v):
    u = (q[0], q[1], q[2])
    t = tuple(2 * c for c in cross(u, v))
    c2 = cross(u, t)
    return (v[0] + q[3] * t[0] + c2[0], v[1] + q[3] * t[1] + c2[1], v[2] + q[3] * t[2] + c2[2])

# Where the lifting surfaces of the aeroplane are, and how they are set: the simulation
# flies with these, and the models are made to fit them.
AIRPLANE = {
    "wing_l": ((-2.725, 0.15, 0.14), then(turn(-4, (0, 0, 1)), turn(2.5, (1, 0, 0)))),
    "wing_r": ((2.725, 0.15, 0.14), then(turn(4, (0, 0, 1)), turn(2.5, (1, 0, 0)))),
    "tail": ((0, 0.25, 4.4), turn(-2.6, (1, 0, 0))),
    "fin": ((0, 0.9, 4.6), turn(-90, (0, 0, 1))),
}

class Mesh:
    def __init__(self):
        self.triangles = []                 # ((p0, p1, p2), (n0, n1, n2))

    def tri(self, a, b, c, outward=None, normals=None):
        normal = unit(cross(sub(b, a), sub(c, a)))
        if outward is not None and dot(normal, outward) < 0:
            b, c = c, b
            normal = (-normal[0], -normal[1], -normal[2])
            if normals: normals = (normals[0], normals[2], normals[1])
        self.triangles.append(((a, b, c), normals or (normal, normal, normal)))

    def quad(self, a, b, c, d, outward=None, normals=None):
        self.tri(a, b, c, outward, normals and (normals[0], normals[1], normals[2]))
        self.tri(a, c, d, outward, normals and (normals[0], normals[2], normals[3]))

    def loft(self, rings, smooth=False, caps=True, keep=None):
        """A skin over rings of points (all with as many points). `keep(segment, side)` chooses faces."""
        centers = [tuple(sum(p[i] for p in ring) / len(ring) for i in range(3)) for ring in rings]
        count = len(rings[0])
        for s in range(len(rings) - 1):
            for k in range(count):
                if keep and not keep(s, k): continue
                a, b = rings[s][k], rings[s][(k + 1) % count]
                c, d = rings[s + 1][(k + 1) % count], rings[s + 1][k]
                middle = tuple((a[i] + b[i] + c[i] + d[i]) / 4 for i in range(3))
                center = tuple((centers[s][i] + centers[s + 1][i]) / 2 for i in range(3))
                normals = None
                if smooth:
                    normals = tuple(unit(sub(p, cen)) for p, cen in ((a, centers[s]), (b, centers[s]), (c, centers[s + 1]), (d, centers[s + 1])))
                self.quad(a, b, c, d, sub(middle, center), normals)
        if caps and not keep:
            for ring, center, other in ((rings[0], centers[0], centers[1]), (rings[-1], centers[-1], centers[-2])):
                for k in range(count):
                    self.tri(center, ring[k], ring[(k + 1) % count], sub(center, other))

    def revolve_x(self, profile, segments=24, center=(0, 0, 0)):
        """Turns a profile of (x, radius) around the x axis: wheels."""
        rings = []
        for x, radius in profile:
            rings.append([(center[0] + x, center[1] + radius * math.cos(2 * math.pi * k / segments), center[2] + radius * math.sin(2 * math.pi * k / segments))
                          for k in range(segments)])
        for s in range(len(rings) - 1):
            for k in range(segments):
                a, b, c, d = rings[s][k], rings[s][(k + 1) % segments], rings[s + 1][(k + 1) % segments], rings[s + 1][k]
                middle = tuple((a[i] + b[i] + c[i] + d[i]) / 4 for i in range(3))
                axis = (middle[0], center[1], center[2])
                slope = unit(cross(sub(d, a), sub(b, a)))
                self.quad(a, b, c, d, slope if dot(slope, sub(middle, axis)) > 0 or abs(profile[s][1] - profile[s + 1][1]) > abs(profile[s][0] - profile[s + 1][0]) * 3
                          else sub(middle, axis))

    def box(self, center, size):
        x, y, z = (s / 2 for s in size)
        corners = [(center[0] + sx * x, center[1] + sy * y, center[2] + sz * z) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        for a, b, c, d in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            middle = tuple((corners[a][i] + corners[c][i]) / 2 for i in range(3))
            self.quad(corners[a], corners[b], corners[c], corners[d], sub(middle, center))

    def write(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write("# made by Tools/make_models.py\n")
            index = 1
            for points, normals in self.triangles:
                for p in points: f.write("v %.3f %.3f %.3f\n" % p)
                for n in normals: f.write("vn %.2f %.2f %.2f\n" % n)
                f.write("f %d//%d %d//%d %d//%d\n" % (index, index, index + 1, index + 1, index + 2, index + 2))
                index += 3

def table(points):
    """A value that changes along a shape: eased between the given (z, value), so that corners are round."""
    def at(z):
        if z <= points[0][0]: return points[0][1]
        for (z0, v0), (z1, v1) in zip(points, points[1:]):
            if z <= z1:
                t = (z - z0) / (z1 - z0)
                t = t * t * (3 - 2 * t) * 0.6 + t * 0.4
                return v0 + (v1 - v0) * t
        return points[-1][1]
    return at

def skin(rings, meshes, choose=None, around=False, ends=(None, None)):
    """A skin over rings of points, shaded smoothly along the rings (and around them too if asked).
    `choose(segment, side)` names which of `meshes` a face goes to (None leaves a hole).
    `ends`: which mesh closes the first and the last ring."""
    count, length = len(rings[0]), len(rings)
    centers = [tuple(sum(p[i] for p in ring) / count for i in range(3)) for ring in rings]
    face = {}
    for s in range(length - 1):
        for k in range(count):
            a, b, c, d = rings[s][k], rings[s][(k + 1) % count], rings[s + 1][(k + 1) % count], rings[s + 1][k]
            normal = cross(sub(c, a), sub(d, b))
            size = math.sqrt(dot(normal, normal))
            if size < 1e-12:
                face[s, k] = (0, 0, 0)
                continue
            middle = tuple((a[i] + b[i] + c[i] + d[i]) / 4 for i in range(3))
            center = tuple((centers[s][i] + centers[s + 1][i]) / 2 for i in range(3))
            sign = 1 if dot(normal, sub(middle, center)) >= 0 else -1
            face[s, k] = tuple(sign * n / size for n in normal)
    def blend(s, k, ring_step, side_step):
        """The normal at a corner: the faces that meet there along the skin, and around it if asked."""
        total = [0, 0, 0]
        for ds in ((0, ring_step) if ring_step else (0,)):
            for dk in ((0, side_step) if around else (0,)):
                found = face.get((s + ds, (k + dk) % count))
                if found and dot(found, face[s, k]) > 0.35:
                    total = [total[i] + found[i] for i in range(3)]
        return unit(tuple(total)) if dot(total, total) > 1e-12 else face[s, k]
    for s in range(length - 1):
        for k in range(count):
            which = choose(s, k) if choose else 0
            if which is None or face[s, k] == (0, 0, 0): continue
            a, b, c, d = rings[s][k], rings[s][(k + 1) % count], rings[s + 1][(k + 1) % count], rings[s + 1][k]
            normals = (blend(s, k, -1, -1), blend(s, k, -1, 1), blend(s, k, 1, 1), blend(s, k, 1, -1))
            meshes[which].quad(a, b, c, d, face[s, k], normals)
    for ring, center, other, which in ((rings[0], centers[0], centers[1], ends[0]), (rings[-1], centers[-1], centers[-2], ends[1])):
        if which is None: continue
        for k in range(count): meshes[which].tri(center, ring[k], ring[(k + 1) % count], sub(center, other))

# ---------------------------------------------------------------- the car
def car(folder):
    """A saloon, 4.3 m long, nose toward -z, standing on y = 0, with arches for wheels of
    0.33 m at z = -1.3 and +1.3, 0.8 m to each side."""
    top = table([(-2.15, .58), (-2.0, .71), (-1.3, .87), (-0.8, .93), (-0.05, 1.37), (0.85, 1.37), (1.55, 1.03), (2.0, .99), (2.15, .8)])
    belt = table([(-2.15, .54), (-2.0, .66), (-1.3, .83), (-0.8, .89), (1.55, .97), (2.0, .93), (2.15, .76)])
    width = table([(-2.15, .66), (-1.95, .86), (-1.3, .92), (1.3, .92), (1.95, .88), (2.15, .7)])
    top_width = table([(-2.15, .5), (-1.95, .72), (-0.8, .82), (-0.05, .63), (0.85, .63), (1.55, .78), (2.15, .58)])
    bottom = table([(-2.15, .38), (-1.95, .2), (1.95, .2), (2.15, .4)])
    def arch(z):
        for wheel in (-1.3, 1.3):
            if abs(z - wheel) < 0.4: return 0.33 + math.sqrt(0.4 ** 2 - (z - wheel) ** 2)
        return 0
    stations = [round(-2.15 + 0.05 * i, 4) for i in range(87)]
    def ring(z):
        w, wt, yb, ym, yt = width(z), top_width(z), bottom(z), belt(z), top(z)
        ya = max(yb, arch(z))
        wi = min(0.62, w - 0.04)
        half = [(0, yb), (wi, yb), (wi, ya), (w - 0.02, max(yb + 0.06, ya)), (w, min(max(0.52, ya + 0.04), ym - 0.02)), (w - 0.03, ym),
                (wt, max(ym + 0.012, yt - 0.05)), (max(wt - 0.14, 0.2), yt), (0, yt + 0.015)]
        return [(x, y, z) for x, y in half] + [(-x, y, z) for x, y in reversed(half[1:-1])]
    rings = [ring(z) for z in stations]
    def choose(segment, side):
        z0, z1 = stations[segment], stations[segment + 1]
        def within(low, high): return z0 >= low - 1e-6 and z1 <= high + 1e-6
        if side in (5, 10) and (within(-0.55, 0.36) or within(0.45, 0.95)): return 1            # the side windows, with a pillar between
        if side in (6, 7, 8, 9) and (within(-0.78, -0.07) or within(0.87, 1.53)): return 1      # the windscreen and the rear window
        return 0
    body, glass = Mesh(), Mesh()
    skin(rings, (body, glass), choose, ends=(0, 0))
    body.write(folder + "/car_body.obj")
    glass.write(folder + "/car_glass.obj")
    # Lights and trim: pieces just proud of the body.
    lights = Mesh()
    for x in (-0.56, 0.56): lights.box((x, 0.6, -2.085), (0.36, 0.1, 0.1))
    lights.write(folder + "/car_headlights.obj")
    tail = Mesh()
    for x in (-0.56, 0.56): tail.box((x, 0.82, 2.1), (0.42, 0.1, 0.1))
    tail.write(folder + "/car_taillights.obj")
    trim = Mesh()
    trim.box((0, 0.47, -2.13), (0.8, 0.12, 0.06))                # the grille
    trim.box((0, 0.3, -2.02), (1.56, 0.07, 0.3))                 # the front splitter
    trim.box((0, 0.31, 2.03), (1.5, 0.09, 0.28))                 # the rear diffuser
    for x in (-0.95, 0.95): trim.box((x, 0.98, -0.62), (0.14, 0.09, 0.12))   # mirrors
    trim.write(folder + "/car_trim.obj")
    # A wheel, its axle along x, 0.66 m across: the tire, and the rim that shows on both sides.
    tire = Mesh()
    tire.revolve_x([(-0.115, 0.215), (-0.115, 0.295), (-0.09, 0.325), (-0.03, 0.33), (0.03, 0.33), (0.09, 0.325), (0.115, 0.295), (0.115, 0.215)], 28)
    tire.write(folder + "/wheel_tire.obj")
    rim = Mesh()
    for side in (-1, 1):
        rim.revolve_x([(side * 0.112, 0.218), (side * 0.085, 0.2), (side * 0.07, 0.07), (side * 0.1, 0.05), (side * 0.105, 0.0)], 20)
        for k in range(5):                                       # five spokes
            a = 2 * math.pi * k / 5
            rim.box((side * 0.1, 0.13 * math.cos(a), 0.13 * math.sin(a)), (0.02, 0.05 + 0.16 * abs(math.cos(a)), 0.05 + 0.16 * abs(math.sin(a))))
    rim.write(folder + "/wheel_rim.obj")

# ---------------------------------------------------------------- the aeroplane
def airfoil(u):
    """Half the thickness of a wing section at a share `u` of its chord, for a section 12 percent thick."""
    return 0.6 * (0.2969 * math.sqrt(u) - 0.126 * u - 0.3516 * u * u + 0.2843 * u ** 3 - 0.1036 * u ** 4)

def lifting_surface(x0, x1, chord0, chord1, hinge, trailing, moving, thickness=1.0, y=0.0, tip=True):
    """A wing lying along x, from x0 to x1, its trailing edge straight at z = `trailing` and
    the line its moving parts turn about at z = `hinge`. `moving` is a list of (from, to)
    along x. Returns the fixed mesh, and for each moving part its mesh, made around the
    middle of its hinge line, and where that middle is."""
    def chord(x): return chord0 + (chord1 - chord0) * (x - x0) / (x1 - x0)
    def section(x, shares, scale=1.0):
        """The points of a section from the lower surface round the nose to the upper."""
        c = chord(x)
        lower = [(x, y - airfoil(u) * c * thickness * 0.8 * scale, trailing - c + u * c) for u in reversed(shares)]
        upper = [(x, y + airfoil(u) * c * thickness * 1.2 * scale, trailing - c + u * c) for u in shares[1:]]
        return lower + upper
    def share_of_hinge(x): return 1 - (trailing - hinge) / chord(x)
    front = [0, 0.012, 0.05, 0.12, 0.25, 0.42, 0.6, 0.8, 1.0]
    fixed = Mesh()
    span = x1 - x0
    stations = [x0 + span * t for t in (0, 0.25, 0.5, 0.75, 1.0)]
    rings = [section(x, [u * share_of_hinge(x) for u in front]) for x in stations]
    if tip:                                                      # rounded off at the end
        end = x1 + (0.06 if x1 > x0 else -0.06) * abs(chord1)
        rings.append([(end, py * 0.4 + y * 0.6, trailing - chord1 * 0.9 + (pz - (trailing - chord1)) * 0.75) for _, py, pz in rings[-1]])
    skin(rings, (fixed,), around=True, ends=(0, 0))
    def tail_piece(mesh, a, b, origin):
        """From the hinge line back to the trailing edge, between two places along the wing."""
        pieces = []
        for x in (a, b):
            c, uh = chord(x), share_of_hinge(x)
            half = airfoil(uh) * c * thickness
            edge = airfoil(0.98) * c * thickness
            pieces.append([(x - origin[0], py - origin[1], pz - origin[2]) for py, pz in
                           ((y - half * 0.8, hinge), (y - edge, trailing), (y + edge, trailing), (y + half * 1.2, hinge), (y, hinge - half * 0.7))])
        skin(pieces, (mesh,), ends=(0, 0))
    parts = []
    edges = sorted([x0, x1] + [v for pair in moving for v in pair], key=lambda v: (v - x0) / span)
    for a, b in zip(edges, edges[1:]):
        if abs(b - a) < 1e-6: continue
        middle = (a + b) / 2
        if any(min(m) - 1e-6 <= middle <= max(m) + 1e-6 for m in moving):
            gap = 0.012 if b > a else -0.012                     # a slot each side, so that it reads as a separate piece
            origin = (middle, y, hinge)
            mesh = Mesh()
            tail_piece(mesh, a + gap, b - gap, origin)
            parts.append((mesh, origin))
        else:
            tail_piece(fixed, a, b, (0, 0, 0))
    return fixed, parts

def airplane(folder):
    """A high-wing four-seater, nose toward -z, its center of mass at the origin. The fixed
    parts are made in the aeroplane's own axes; each wing, the tailplane and the fin are made
    in the axes of their own entity (see `AIRPLANE` for where those are), and each moving
    surface around its hinge. Returns where the hinges are, in the axes of their surface."""
    # ---- the fuselage: rounded boxes from the spinner back to the tail
    stations = [-2.42, -2.36, -2.2, -1.9, -1.45, -1.0, -0.4, 0.3, 0.9, 1.5, 2.3, 3.2, 4.1, 4.95]
    half_width = table([(-2.42, .3), (-2.2, .46), (-1.45, .56), (-0.4, .6), (0.9, .58), (2.3, .34), (4.1, .13), (4.95, .05)])
    upper = table([(-2.42, .25), (-2.2, .36), (-1.45, .42), (-1.0, .46), (-0.4, .78), (0.9, .76), (1.5, .6), (2.3, .5), (4.95, .42)])
    lower = table([(-2.42, -.28), (-2.2, -.44), (-1.45, -.55), (0.3, -.6), (1.5, -.42), (3.2, -.05), (4.95, .26)])
    count = 20
    def ring(z):
        w, top, low = half_width(z), upper(z), lower(z)
        middle, half = (top + low) / 2, (top - low) / 2
        points = []
        for k in range(count):
            a = 2 * math.pi * (k + 0.5) / count
            cx, cy = math.cos(a), math.sin(a)
            # Squarer than a circle: a cabin has sides.
            points.append((w * math.copysign(abs(cx) ** 0.6, cx), middle + half * math.copysign(abs(cy) ** 0.6, cy), z))
        return points
    rings = [ring(z) for z in stations]
    def choose(segment, side):
        z0, z1 = stations[segment], stations[segment + 1]
        angle = 360 * (side + 0.5) / count                       # 90 is the top
        if z0 >= -1.0 - 1e-6 and z1 <= -0.4 + 1e-6 and 18 < angle < 162: return 1                 # the windscreen
        if z0 >= -0.4 - 1e-6 and z1 <= 0.9 + 1e-6 and (18 < angle < 60 or 120 < angle < 162): return 1    # the side windows
        if z0 >= 0.9 - 1e-6 and z1 <= 1.5 + 1e-6 and (18 < angle < 70 or 110 < angle < 162): return 1     # and the ones behind
        if z1 <= -2.2 + 1e-6: return 2                           # the nose ring, painted
        if 198 < angle < 342 or z0 >= 4.1 - 1e-6: return 0       # the belly, and the last of the tail
        if (-18 < angle - 360 < 18 or angle < 14 or 166 < angle < 198): return 2                  # a stripe along each side
        return 0
    fuselage, glass, accent = Mesh(), Mesh(), Mesh()
    skin(rings, (fuselage, glass, accent), choose, around=True, ends=(2, 0))
    # ---- legs for the wheels, and the struts that hold the wings
    gear = Mesh()
    def rod(mesh, a, b, wide, deep):
        skin([[(p[0] + dx * wide, p[1], p[2] + dz * deep) for dx, dz in ((-1, -1), (1, -1), (1, 1), (-1, 1))] for p in (a, b)], (mesh,), ends=(0, 0))
    for side in (-1, 1):
        rod(gear, (side * 0.35, -0.5, 0.35), (side * 1.18, -0.9, 0.35), 0.035, 0.09)
        rod(fuselage, (side * 0.55, -0.42, -0.05), (side * 2.5, 0.68, -0.02), 0.025, 0.07)
    rod(gear, (0, -0.4, -1.75), (0, -0.85, -1.7), 0.03, 0.04)
    gear.box((0, -0.14, -2.41), (0.3, 0.1, 0.04))                 # the air intake under the spinner
    gear.write(folder + "/plane_gear.obj")
    fuselage.write(folder + "/plane_fuselage.obj")
    glass.write(folder + "/plane_glass.obj")

    # ---- wings, tailplane and fin, each in its own entity's axes, with their moving parts
    hinges = {}
    for side, name in ((-1, "l"), (1, "r")):
        # The entity is at the middle of the wing; the root is toward the fuselage.
        root, tip = -2.45 * side, 2.72 * side
        fixed, parts = lifting_surface(root, tip, 1.6, 1.12, 0.47, 0.78, [(root + 0.25 * side, root + 2.3 * side), (root + 2.4 * side, tip - 0.25 * side)], y=0.5)
        fixed.write(folder + "/plane_wing_%s.obj" % name)
        (flap, flap_at), (aileron, aileron_at) = parts
        flap.write(folder + "/plane_flap_%s.obj" % name)
        aileron.write(folder + "/plane_aileron_%s.obj" % name)
        hinges["flap_" + name], hinges["aileron_" + name] = flap_at, aileron_at
    for side in (-1, 1):
        fixed, parts = lifting_surface(0.0, 1.7 * side, 1.0, 0.62, 0.12, 0.5, [(0.1 * side, 1.66 * side)], thickness=0.8)
        fixed.write(folder + "/plane_tail_%s.obj" % ("l" if side < 0 else "r"))
        parts[0][0].write(folder + "/plane_elevator_%s.obj" % ("l" if side < 0 else "r"))
        hinges["elevator_" + ("l" if side < 0 else "r")] = parts[0][1]
    # The fin's entity stands on its side: its x points down, so the fin goes toward -x.
    fixed, parts = lifting_surface(0.55, -0.9, 1.35, 0.7, 0.2, 0.62, [(0.5, -0.86)], thickness=0.8)
    fixed.write(folder + "/plane_fin.obj")
    parts[0][0].write(folder + "/plane_rudder.obj")
    hinges["rudder"] = parts[0][1]
    accent.write(folder + "/plane_accent.obj")

    # ---- the propeller, around its own middle, turning about z; and a wheel, its axle along x
    blades = Mesh()
    for side in (-1, 1):
        skin([[(side * r, c * math.cos(t) * s, c * math.sin(t) * s * 0.4 + twist * s) for s, twist in ((-1, -1), (0, 0.25), (1, 1), (0, -0.25))]
              for r, c, t, twist in ((0.1, .06, 1.0, .03), (0.35, .085, 0.7, .02), (0.7, .075, 0.4, .012), (0.95, .04, 0.25, .006))], (blades,), ends=(0, 0))
    skin([[(r * math.cos(2 * math.pi * k / 12), r * math.sin(2 * math.pi * k / 12), z) for k in range(12)] for z, r in ((-0.3, .015), (-0.24, .08), (-0.12, .15), (0.04, .18))],
         (blades,), around=True, ends=(0, 0))
    blades.write(folder + "/plane_propeller.obj")
    wheel = Mesh()
    wheel.revolve_x([(-0.07, 0.0), (-0.07, 0.15), (-0.045, 0.19), (0, 0.2), (0.045, 0.19), (0.07, 0.15), (0.07, 0.0)], 16)
    wheel.write(folder + "/plane_wheel.obj")
    hub = Mesh()
    for side in (-1, 1): hub.revolve_x([(side * 0.072, 0.0), (side * 0.076, 0.09), (side * 0.072, 0.1)], 12)
    hub.write(folder + "/plane_hub.obj")
    return hinges

# ---------------------------------------------------------------- scenery
def trees(path, count, inside, outside, clear, seed=3):
    """Many trees as one model: a trunk and two cones each. `clear(x, z)` says where none may stand."""
    rng = random.Random(seed)
    trunks, leaves = Mesh(), Mesh()
    placed = 0
    while placed < count:
        x, z = rng.uniform(-outside, outside), rng.uniform(-outside, outside)
        if max(abs(x), abs(z)) < inside or clear(x, z): continue
        placed += 1
        height = rng.uniform(5, 11)
        spread = height * rng.uniform(0.22, 0.3)
        def circle(y, r, n=6): return [(x + r * math.cos(2 * math.pi * k / n), y, z + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
        trunks.loft([circle(0, 0.25), circle(height * 0.3, 0.2)], caps=False)
        for base, tip, r in ((0.22, 0.75, 1.0), (0.5, 1.0, 0.7)):
            leaves.loft([circle(height * base, spread * r, 7), circle(height * tip, 0.03, 7)], smooth=True)
    trunks.write(path.replace(".obj", "_trunks.obj"))
    leaves.write(path.replace(".obj", "_leaves.obj"))

def hills(path, size, flat, seed=5):
    """The land far away: flat within `flat` meters of the middle, rising beyond. Returns the highest tops as (x, height, z)."""
    rng = random.Random(seed)
    waves = [(rng.uniform(0.0006, 0.0022), rng.uniform(0, 6.28), rng.uniform(0, 6.28), rng.uniform(0.4, 1.0)) for _ in range(9)]
    def height(x, z):
        distance = math.hypot(x, z)
        rise = max(0.0, min(1.0, (distance - flat) / (flat * 1.4)))
        rise = rise * rise * (3 - 2 * rise)
        h = sum(a * math.sin(x * f * math.cos(p) + z * f * math.sin(p) + q) for f, p, q, a in waves)
        return rise * max(0.0, 150 + 90 * h) - 0.4
    n = 44
    step = 2 * size / n
    mesh = Mesh()
    grid = [[(-size + i * step, height(-size + i * step, -size + j * step), -size + j * step) for j in range(n + 1)] for i in range(n + 1)]
    def normal(i, j):
        a, b = grid[min(i + 1, n)][j], grid[max(i - 1, 0)][j]
        c, d = grid[i][min(j + 1, n)], grid[i][max(j - 1, 0)]
        return unit(cross(sub(c, d), sub(a, b)))
    tops = []
    for i in range(n):
        for j in range(n):
            quad = (grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j])
            if max(p[1] for p in quad) < 0.5: continue           # the flat middle is the scene's own ground
            mesh.quad(*quad, outward=(0, 1, 0), normals=(normal(i, j), normal(i, j + 1), normal(i + 1, j + 1), normal(i + 1, j)))
    for i in range(2, n - 1, 3):
        for j in range(2, n - 1, 3):
            p = grid[i][j]
            if p[1] > 150 and all(p[1] >= grid[i + a][j + b][1] for a in (-2, 0, 2) for b in (-2, 0, 2)): tops.append(p)
    mesh.write(path)
    return sorted(tops, key=lambda p: -p[1])[:8]

def track_points(count=220):
    """The middle of a road that comes back to its start: (x, z) all the way round."""
    points = []
    for k in range(count):
        t = 2 * math.pi * k / count
        radius = 150 + 42 * math.sin(2 * t + 0.6) + 26 * math.sin(3 * t) + 14 * math.cos(5 * t)
        points.append((radius * math.sin(t) * 1.25, -radius * math.cos(t) + 150))
    return points

def track(folder, half_width=5.5):
    """The road as a ribbon just above the ground, its red and white curbs, and the line down its middle."""
    points = track_points()
    road, red, white, line = Mesh(), Mesh(), Mesh(), Mesh()
    count = len(points)
    def edge(k, offset, y):
        (x0, z0), (x1, z1) = points[(k - 1) % count], points[(k + 1) % count]
        dx, dz = x1 - x0, z1 - z0
        length = math.hypot(dx, dz)
        return (points[k % count][0] - dz / length * offset, y, points[k % count][1] + dx / length * offset)
    for k in range(count):
        up = (0, 1, 0)
        road.quad(edge(k, -half_width, 0.03), edge(k, half_width, 0.03), edge(k + 1, half_width, 0.03), edge(k + 1, -half_width, 0.03), up)
        curb = red if k % 2 else white
        for side in (-1, 1):
            curb.quad(edge(k, side * half_width, 0.09), edge(k, side * (half_width + 0.8), 0.09), edge(k + 1, side * (half_width + 0.8), 0.09), edge(k + 1, side * half_width, 0.09), up)
        if k % 4 < 2: line.quad(edge(k, -0.09, 0.07), edge(k, 0.09, 0.07), edge(k + 1, 0.09, 0.07), edge(k + 1, -0.09, 0.07), up)
    road.write(folder + "/track_road.obj")
    red.write(folder + "/track_curb_red.obj")
    white.write(folder + "/track_curb_white.obj")
    line.write(folder + "/track_line.obj")
    return points

# ---------------------------------------------------------------- a small circuit
CIRCUIT_CORNERS = [(-60, 0), (0, 0), (60, 0), (98, 8), (114, 36), (98, 62), (64, 66), (34, 54), (4, 62), (-22, 82), (-54, 86), (-84, 70), (-92, 42),
                   (-114, 28), (-118, 4), (-98, -6)]

def circuit_points(step=2.0):
    """The middle of a short circuit that comes back to its start, as points `step` meters
    apart: a smooth curve through CIRCUIT_CORNERS. It starts on the straight, going toward +x."""
    corners = CIRCUIT_CORNERS
    count = len(corners)
    fine = []
    for k in range(count):
        p0, p1, p2, p3 = corners[(k - 1) % count], corners[k], corners[(k + 1) % count], corners[(k + 2) % count]
        for j in range(40):
            t = j / 40.0
            fine.append(tuple(0.5 * ((2 * p1[a]) + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t * t +
                                     (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t * t * t) for a in (0, 1)))
    # The same curve at even steps, starting where the straight's middle is.
    start = min(range(len(fine)), key=lambda i: math.hypot(fine[i][0], fine[i][1]))
    fine = fine[start:] + fine[:start]
    lengths = [math.hypot(fine[(i + 1) % len(fine)][0] - fine[i][0], fine[(i + 1) % len(fine)][1] - fine[i][1]) for i in range(len(fine))]
    total = sum(lengths)
    pieces = int(round(total / step))
    points, walked, i = [], 0.0, 0
    for k in range(pieces):
        target = total * k / pieces
        while walked + lengths[i] < target:
            walked += lengths[i]
            i += 1
        t = (target - walked) / lengths[i]
        a, b = fine[i], fine[(i + 1) % len(fine)]
        points.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return points

def circuit(folder, half_width=5.0, verge=4.6):
    """The circuit's models (road, edge lines, curbs in the corners, the start's checkers) and
    what a scene needs to know about it: where the walls stand, where the gates of a lap are."""
    points = circuit_points()
    count = len(points)
    def direction(k):
        (x0, z0), (x1, z1) = points[(k - 1) % count], points[(k + 1) % count]
        length = math.hypot(x1 - x0, z1 - z0)
        return ((x1 - x0) / length, (z1 - z0) / length)
    def edge(k, offset, y):
        dx, dz = direction(k)
        return (points[k % count][0] - dz * offset, y, points[k % count][1] + dx * offset)
    def bend(k):
        """How sharply the road turns here, in turns of 1/meters: positive toward the side of positive offsets."""
        (ax, az), (bx, bz) = direction(k - 2), direction(k + 2)
        turned = math.atan2(ax * bz - az * bx, ax * bx + az * bz)
        return turned / 8.0
    up = (0, 1, 0)
    road, red, white, line, checker = Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    for k in range(count):
        road.quad(edge(k, -half_width, 0.03), edge(k, half_width, 0.03), edge(k + 1, half_width, 0.03), edge(k + 1, -half_width, 0.03), up)
        for side in (-1, 1):
            inner, outer = side * (half_width - 0.35), side * (half_width - 0.2)
            line.quad(edge(k, inner, 0.05), edge(k, outer, 0.05), edge(k + 1, outer, 0.05), edge(k + 1, inner, 0.05), up)
        # Curbs where it turns: on the inside of the bend all the way round it, and on the outside too.
        if abs(bend(k)) > 1.0 / 70.0:
            stripe = red if (k // 1) % 2 else white
            for side in (-1, 1):
                a, b = side * half_width, side * (half_width + 1.1)
                stripe.quad(edge(k, a, 0.06), edge(k, b, 0.09), edge(k + 1, b, 0.09), edge(k + 1, a, 0.06), up)
    # The start: two rows of checkers across the road, and two boxes to line up in behind it.
    squares = 10
    for row in range(2):
        for column in range(squares):
            if (row + column) % 2: continue
            x0, x1 = points[0][0] + row * 0.5, points[0][0] + (row + 1) * 0.5
            z0 = points[0][1] - half_width + 2 * half_width * column / squares
            z1 = points[0][1] - half_width + 2 * half_width * (column + 1) / squares
            checker.quad((x0, 0.05, z0), (x1, 0.05, z0), (x1, 0.05, z1), (x0, 0.05, z1), up)
    for slot, side in ((0, -1), (1, 1)):
        x, z = points[0][0] - 6.0 - slot * 7.0, points[0][1] + side * 2.3
        for a, b, c, d in ((x - 0.08, z - 1.3, x + 0.08, z + 1.3), (x - 3.6, z - 1.3, x, z - 1.14), (x - 3.6, z + 1.14, x, z + 1.3)):
            line.quad((a, 0.05, b), (c, 0.05, b), (c, 0.05, d), (a, 0.05, d), up)
    # The pit lane: it leaves the road where the last hairpin opens onto the straight, runs
    # behind the pit wall in front of the garages, and comes back after the straight.
    pit_z, pit_half = -16.5, 3.0
    def bezier(a, b, c, d, steps):
        out = []
        for i in range(steps + 1):
            t = i / steps
            u = 1 - t
            out.append((u * u * u * a[0] + 3 * u * u * t * b[0] + 3 * u * t * t * c[0] + t * t * t * d[0],
                        u * u * u * a[1] + 3 * u * u * t * b[1] + 3 * u * t * t * c[1] + t * t * t * d[1]))
        return out
    def on_road(k, offset):
        e = edge(k, offset, 0)
        return (e[0], e[2])
    def ahead(k, metres, offset):
        x, z = on_road(k, offset)
        dx, dz = direction(k)
        return (x + dx * metres, z + dz * metres)
    leave, rejoin = count - 56, 50
    pit = bezier(on_road(leave, -2.5), ahead(leave, 16, -3.5), (-88.0, pit_z), (-66.0, pit_z), 18)
    pit += [(x, pit_z) for x in range(-64, 66, 2)]
    pit += bezier((66.0, pit_z), (88.0, pit_z), ahead(rejoin, -16, -3.5), on_road(rejoin, -2.5), 18)[1:]
    pit_box = (20.0, pit_z - 1.0)
    lane = Mesh()
    for k in range(len(pit) - 1):
        (x0, z0), (x1, z1) = pit[k], pit[k + 1]
        length = math.hypot(x1 - x0, z1 - z0)
        if length < 1e-4: continue
        nx, nz = -(z1 - z0) / length, (x1 - x0) / length
        a, b = (x0 - nx * pit_half, 0.025, z0 - nz * pit_half), (x0 + nx * pit_half, 0.025, z0 + nz * pit_half)
        c, d = (x1 + nx * pit_half, 0.025, z1 + nz * pit_half), (x1 - nx * pit_half, 0.025, z1 - nz * pit_half)
        road.quad(a, b, c, d, up)
        for side in (-1, 1):
            i0, i1 = side * (pit_half - 0.3), side * (pit_half - 0.15)
            line.quad((x0 + nx * i0, 0.045, z0 + nz * i0), (x0 + nx * i1, 0.045, z0 + nz * i1),
                      (x1 + nx * i1, 0.045, z1 + nz * i1), (x1 + nx * i0, 0.045, z1 + nz * i0), up)
    # The box where the tyres are changed: a rectangle on the lane, in front of a garage.
    bx, bz = pit_box
    for a, b, c, d in ((bx - 2.6, bz - 1.4, bx + 2.6, bz - 1.25), (bx - 2.6, bz + 1.25, bx + 2.6, bz + 1.4),
                       (bx - 2.6, bz - 1.4, bx - 2.45, bz + 1.4), (bx + 2.45, bz - 1.4, bx + 2.6, bz + 1.4)):
        line.quad((a, 0.05, b), (c, 0.05, b), (c, 0.05, d), (a, 0.05, d), up)
    road.write(folder + "/circuit_road.obj")
    red.write(folder + "/circuit_curb_red.obj")
    white.write(folder + "/circuit_curb_white.obj")
    line.write(folder + "/circuit_lines.obj")
    checker.write(folder + "/circuit_start.obj")

    # The walls: on each side, a little way off the road, in pieces about four meters long.
    walls = []
    for side in (-1, 1):
        offset = side * (half_width + verge)
        path = [edge(k, offset, 0) for k in range(0, count, 2)]
        for k in range(len(path)):
            a, b = path[k], path[(k + 1) % len(path)]
            length = math.hypot(b[0] - a[0], b[2] - a[2])
            dx, dz = direction(k * 2 + 1)
            if length < 0.5 or (b[0] - a[0]) * dx + (b[2] - a[2]) * dz < 0: continue      # the inside of a tight bend folds on itself: nothing there
            mx, mz = (a[0] + b[0]) * 0.5, (a[2] + b[2]) * 0.5
            # Not where the pit lane goes through.
            if any((mx - px) ** 2 + (mz - pz) ** 2 < (pit_half + 2.6) ** 2 for px, pz in pit): continue
            walls.append((mx, mz, math.degrees(math.atan2(-(b[2] - a[2]), b[0] - a[0])), length + 0.15, k % 2))
    tightest = min(1.0 / max(abs(bend(k)), 1e-6) for k in range(count))
    gates = [points[int(count * share)] for share in (0.0, 0.25, 0.5, 0.75)]
    return dict(points=points, walls=walls, gates=gates, half_width=half_width, length=count * 2.0, tightest=tightest,
                pit=pit, pit_half=pit_half, pit_box=pit_box,
                start=(points[0][0] - 9.5, points[0][1] - 2.3))
