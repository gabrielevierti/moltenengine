#!/usr/bin/env python3
"""Writes the example projects, which are not part of the engine but a download of their
own: into ../MoltenProjects, or the folder given as the first argument; with --zip, also
as MoltenProjects.zip beside that folder. Small, complete games made only of
what the editor offers (the kit's components, prefabs, a visual script), each a project
that can be opened as it is. Run from the repository's root:  python3 Tools/make_templates.py

The scenes are written here as text, in the same format the editor saves, so that the
templates can be regenerated when a component's fields change."""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

_arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
ROOT = os.path.abspath(_arguments[0]) if _arguments else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "MoltenProjects")

def q(text):
    return '"' + str(text).replace("\\", "\\\\").replace('"', '\\"') + '"'

def n(*values):
    return " ".join(("%g" % v) for v in values)

def pitch(degrees):
    """A rotation that tips the view down (negative) or up, as a quaternion x y z w."""
    a = math.radians(degrees) / 2
    return (math.sin(a), 0, 0, math.cos(a))

def yaw(degrees):
    a = math.radians(degrees) / 2
    return (0, math.sin(a), 0, math.cos(a))

class Scene:
    """A scene or a prefab of the project in `folder`. Its materials are assets of the
    project: files in Assets/Materials that the scene names."""
    def __init__(self, folder, settings=()):
        self.folder = folder
        self.settings = list(settings)
        self.lines = []

    def material(self, name, color, metallic=0.0, roughness=0.6, emissive=(0, 0, 0), water=None):
        """`water`: (how fine the waves are, how steep) makes it a surface of water."""
        path = "Materials/%s.material" % name
        write(self.folder + "/Assets/" + path, "\n".join([
            "molten-material 1", "shader water" if water else "shader lit", "baseColor " + n(*color), "metallic " + n(metallic), "roughness " + n(roughness),
            "emissive " + n(*emissive)] + (["water " + n(*water)] if water else []) + [""]))
        return path

    def entity(self, name, position=(0, 0, 0), rotation=(0, 0, 0, 1), scale=(1, 1, 1)):
        self.lines.append("entity " + q(name))
        self.lines.append("  transform " + n(*position, *rotation, *scale))
        return self

    def mesh(self, mesh, material, shadows=True):
        self.lines.append("  mesh %s -1 1 1 1 1 %d 1" % (q(mesh), 1 if shadows else 0))
        self.lines.append("  materialAsset " + q(material))
        return self

    def box(self, half, offset=(0, 0, 0), trigger=False):
        self.lines.append("  collider box " + n(*half, *offset) + (" 1" if trigger else " 0"))
        return self

    def sphere(self, radius, trigger=False):
        self.lines.append("  collider sphere " + n(radius, 0, 0, 0) + (" 1" if trigger else " 0"))
        return self

    def body(self, kind, mass=1, restitution=0.1, friction=0.6, damping=0.05, gravity=1):
        self.lines.append("  body %s %s" % (kind, n(mass, restitution, friction, damping, gravity)))
        return self

    def light(self, kind, color, intensity, range_=10, shadows=False):
        self.lines.append("  light %s %s %d" % (kind, n(*color, intensity, range_, 0.35, 0.52), 1 if shadows else 0))
        return self

    def camera(self, fov=60, active=True, far=500, near=0.1):
        """A far plane a long way off needs a near plane that is not too close: the depth of
        the picture is shared out between them, and things lying on one another far away flicker."""
        self.lines.append("  camera %s 0 5 %d" % (n(math.radians(fov), near, far), 1 if active else 0))
        return self

    def particles(self, rate, life, speed, direction, spread, radius, gravity, drag, size, color_a, color_b, additive, maximum, wind=0, burst=0):
        self.lines.append("  particles " + n(rate, *life, *speed, *direction, math.radians(spread), radius, *gravity, drag))
        self.lines.append("  particleLook " + n(*size, *color_a, *color_b) + " %d 1 %d %s %d" % (1 if additive else 0, maximum, n(wind), burst))
        return self

    def component(self, name, **fields):
        self.lines.append("  component " + q(name))
        for field, value in fields.items():
            if isinstance(value, bool):
                value = "1" if value else "0"
            elif isinstance(value, (tuple, list)):
                value = n(*value)
            elif isinstance(value, (int, float)):
                value = "%g" % value
            self.lines.append("    field %s %s" % (field, q(value)))
        return self

    def index(self):
        """The position of the entity just added, for `under`."""
        return sum(1 for line in self.lines if line.startswith("entity ")) - 1

    def under(self, parent):
        self.lines.append("  parent %d" % parent)
        return self

    def child(self):
        """The entity just added is a child of the one `mark()` was called on."""
        self.lines.append("  parent %d" % self.marked)
        return self

    def mark(self):
        self.marked = sum(1 for line in self.lines if line.startswith("entity ")) - 1
        return self

    def text(self):
        out = ["molten-scene 1"]
        if self.settings:
            out.append("settings")
            out += ["  " + line for line in self.settings]
        return "\n".join(out + self.lines) + "\n"

def write(path, text):
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w") as f:
        f.write(text)

def project(folder, name, description, scripts=()):
    """`scripts`: files of Tools/TemplateScripts that are this game's own code."""
    write(folder + "/" + folder + ".molten",
          "molten-project 1\nname %s\nstartScene \"Scenes/Main.scene\"\nversion \"0.1.0\"\n" % q(name))
    write(folder + "/.gitignore", "Library/\nSettings/\n.DS_Store\n")
    write(folder + "/template.txt", description + "\n")
    os.makedirs(os.path.join(ROOT, folder, "Assets", "Scripts"), exist_ok=True)
    for script in scripts:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "TemplateScripts", script)) as source:
            write(folder + "/Assets/Scripts/" + script, source.read())
    write(folder + "/Assets/Scripts/README.txt",
          "The game's own C++: every .cpp file here is built into the game when the project opens and whenever a file changes.\n"
          "Game > New Script in the editor adds one.\n" +
          ("This game's scripts: " + ", ".join(scripts) + ". Everything else it uses comes with the engine.\n" if scripts else
           "This template needs none: it is made of the components that come with the engine.\n"))

def roll(degrees):
    a = math.radians(degrees) / 2
    return (0, 0, math.sin(a), math.cos(a))

def sounds(folder):
    import make_sounds
    make_sounds.write_all(os.path.join(ROOT, folder, "Assets", "Audio"))

DAY = ["skyZenith 0.06 0.18 0.5", "skyHorizon 0.45 0.58 0.75", "skyGround 0.12 0.14 0.17", "skyIntensity 1", "environmentIntensity 1",
       "shadows 1 140", "ambientOcclusion 1 0.6 1", "bloom 1 0.06 1", "exposure 1", "toneMapper 0", "vignette 0.2", "clouds 0.45 1"]

def hud(s, help_lines, left, right, center=None):
    """The game's read-outs: what the keys do at the top left, a big number in each bottom
    corner with a small name above it, and a line between them."""
    for row, line in enumerate(help_lines):
        s.entity("Help %d" % (row + 1))
        s.component("HudText", text=line, anchor=(0, 0), offset=(22, 18 + row * 19), size=14, color=(1, 1, 1, 0.72))
    for name, items, side in (("Left", left, 0), ("Right", right, 1)):
        for column, (label, text) in enumerate(items):
            x = (26 + column * 170) * (1 if side == 0 else -1)
            s.entity("%s Label %d" % (name, column + 1))
            s.component("HudText", text=label, anchor=(side, 1), offset=(x, -86), size=12, color=(1, 1, 1, 0.6))
            s.entity("%s Value %d" % (name, column + 1))
            s.component("HudText", text=text, anchor=(side, 1), offset=(x, -70), size=34, color=(1, 1, 1, 1))
    if center:
        s.entity("Center")
        s.component("HudText", text=center, anchor=(0.5, 1), offset=(0, -40), size=16, color=(1, 1, 1, 0.85))

def models(folder):
    return os.path.join(ROOT, folder, "Assets", "Models")

# ---------------------------------------------------------------- Driving
def material_file(folder, name, color=(1, 1, 1, 1), metallic=0.0, roughness=0.6, emissive=(0, 0, 0), blend=False, double_sided=False, textures=None):
    """A material with everything a .material file can say: its pictures too. Returns its name in Assets."""
    lines = ["molten-material 1", "shader lit", "baseColor " + n(*color), "metallic " + n(metallic), "roughness " + n(roughness), "emissive " + n(*emissive)]
    if blend: lines.append("alpha blend 0.5")
    if double_sided: lines.append("doubleSided 1")
    for key, picture in (textures or {}).items():
        lines.append("%s %s" % (key, q(picture)))
    path = "Materials/%s.material" % name
    write(folder + "/Assets/" + path, "\n".join(lines) + "\n")
    return path

def effect(s, **settings):
    """A Particle System on the entity just added, every setting by name (see Rendering/Particles.hpp)."""
    s.lines.append("  particles 20 0.6 1.2 1 2 0 1 0 0.436 0 0 -2 0 0.5")
    s.lines.append("  particleLook 0.15 0 1 1 1 1 1 1 1 0 1 1 512 0 0")
    for key, value in settings.items():
        if isinstance(value, bool): value = (1 if value else 0,)
        elif not isinstance(value, (tuple, list)): value = (value,)
        s.lines.append("  particleSet %s %s" % (key, n(*value)))
    return s

# A late afternoon: the sun low and warm, the sky going gold toward the horizon, a little haze.
EVENING = ["skyZenith 0.07 0.16 0.42", "skyHorizon 0.86 0.56 0.34", "skyGround 0.13 0.12 0.12", "skyIntensity 1", "environmentIntensity 1",
           "shadows 1 110", "ambientOcclusion 1 0.7 1", "bloom 1 0.11 0.9", "exposure 1.05", "toneMapper 0", "vignette 0.28", "clouds 0.35 1",
           "fog 0.86 0.62 0.44 0.0016", "fogShape 0.02 6"]

def driving():
    import json, make_models
    folder = "Driving"
    fiat = os.path.join(models(folder), "Fiat500")
    if not os.path.exists(os.path.join(fiat, "layout.json")):
        print("Driving: its car is not there. Make it first:  python3 Tools/import_fiat500.py <the model's folder> " + fiat)
        return
    layout = json.load(open(os.path.join(fiat, "layout.json")))
    project(folder, "Driving", "A little circuit and a Fiat 500 with a rocket where its engine was: springs and dampers, tires that slide and smoke, "
            "seven engines behind a gearbox, lamps, lap times, and controls for a touch screen.",
            scripts=("DriveControls.cpp", "Motor.hpp", "Motor.cpp", "Follow.cpp", "LapTimer.cpp", "CarLights.cpp", "Exhaust.cpp", "TyreSmoke.cpp"))
    write(folder + "/CREDITS.txt", "The car: " + layout["credit"] + "\nMade smaller and rearranged for this game by Tools/import_fiat500.py.\n")
    sounds(folder)
    # What the older version of this game was made of is not used any more.
    for old in ("car_body", "car_glass", "car_headlights", "car_taillights", "car_trim", "wheel_tire", "wheel_rim", "track_road", "track_curb_red",
                "track_curb_white", "track_line"):
        stale = os.path.join(models(folder), old + ".obj")
        if os.path.exists(stale): os.remove(stale)
    track = make_models.circuit(models(folder))
    points = track["points"]
    def near_road(x, z):
        return any((x - px) ** 2 + (z - pz) ** 2 < 17 ** 2 for px, pz in points[::3]) or (-75 < x < 75 and -40 < z < -12) or (-34 < x < 34 and 14 < z < 34)
    make_models.trees(models(folder) + "/trees.obj", 230, 0, 260, near_road)
    make_models.hills(models(folder) + "/hills.obj", 1500, 330)

    s = Scene(folder, EVENING)
    grass = s.material("Grass", (0.12, 0.2, 0.07, 1), 0, 0.95)
    asphalt = s.material("Asphalt", (0.045, 0.047, 0.052, 1), 0, 0.82)
    line = s.material("Line", (0.82, 0.82, 0.78, 1), 0, 0.6)
    red = s.material("Curb Red", (0.72, 0.05, 0.04, 1), 0, 0.55)
    white = s.material("Curb White", (0.86, 0.86, 0.83, 1), 0, 0.55)
    wall_red = s.material("Wall Red", (0.62, 0.05, 0.04, 1), 0.1, 0.5)
    wall_white = s.material("Wall White", (0.82, 0.82, 0.8, 1), 0.1, 0.5)
    concrete = s.material("Concrete", (0.42, 0.42, 0.41, 1), 0, 0.9)
    dark = s.material("Dark Steel", (0.05, 0.055, 0.065, 1), 0.7, 0.45)
    roof = s.material("Roof", (0.5, 0.11, 0.07, 1), 0.3, 0.5)
    seats = s.material("Seats", (0.08, 0.2, 0.45, 1), 0, 0.7)
    lamp = s.material("Lamp", (1, 0.9, 0.7, 1), 0, 0.3, (6.0, 4.4, 2.4))
    signal = s.material("Signal", (0.6, 0.02, 0.02, 1), 0, 0.3, (5.0, 0.15, 0.1))
    bark = s.material("Bark", (0.17, 0.11, 0.07, 1), 0, 0.95)
    leaves = s.material("Leaves", (0.07, 0.17, 0.05, 1), 0, 0.9)
    hill = s.material("Hills", (0.16, 0.2, 0.12, 1), 0, 0.95)
    rubber = s.material("Tyre Wall", (0.02, 0.02, 0.022, 1), 0, 0.9)

    # The sun low in the west, behind the first corner: long shadows across the straight.
    s.entity("Sun", (0, 40, 0), then(yaw(118), pitch(-19))).light("directional", (1.0, 0.78, 0.55), 4.6, 10, True)
    # The grass is drawn a little below where it is solid, and the road exactly there: surfaces that
    # lie on one another flicker, and wheels should stand on the road they are seen on.
    s.entity("Ground", (0, -0.08, 40), scale=(900, 1, 900)).mesh("plane", grass).box((450, 0.5, 450), (0, -0.42, 0))
    s.entity("Hills", (0, -3, 40)).mesh("Models/hills.obj", hill, False)
    s.entity("Circuit", (0, -0.03, 0))
    circuit_index = s.index()
    for name, mesh, material in (("Road", "circuit_road", asphalt), ("Edge Lines", "circuit_lines", line), ("Curbs Red", "circuit_curb_red", red),
                                 ("Curbs White", "circuit_curb_white", white), ("Start Line", "circuit_start", line)):
        s.entity(name).mesh("Models/%s.obj" % mesh, material, False).under(circuit_index)
    # The walls: solid, so that a corner taken too fast ends against them and not in the next county.
    s.entity("Walls")
    walls_index = s.index()
    for x, z, turn, length, other in track["walls"]:
        s.entity("Wall", (x, 0.4, z), yaw(turn), (length, 0.8, 0.45)).mesh("cube", wall_red if other else wall_white).box((length * 0.5, 0.4, 0.225)).under(walls_index)
    # The gates of a lap, for the LapTimer: the first is the line.
    s.entity("Gates")
    gates_index = s.index()
    for number, (x, z) in enumerate(track["gates"]):
        s.entity("Gate %d" % (number + 1), (x, 0, z)).under(gates_index)
    # The middle of the road, a point every six meters: for whoever wants to drive it by
    # itself (an opponent, a ghost, the tests that lap it).
    s.entity("Racing Line")
    racing_index = s.index()
    for number, (x, z) in enumerate(points[::3]):
        s.entity("Waypoint %d" % (number + 1), (x, 0, z)).under(racing_index)
    s.entity("Trees")
    trees_index = s.index()
    s.entity("Trunks").mesh("Models/trees_trunks.obj", bark).under(trees_index)
    s.entity("Leaves").mesh("Models/trees_leaves.obj", leaves).under(trees_index)

    # Beside the straight: the pits on the outside, a stand on the inside, a gantry over the line.
    s.entity("Paddock")
    paddock = s.index()
    def block(name, position, size, material, solid=True, turn=(0, 0, 0, 1)):
        s.entity(name, position, turn, size).mesh("cube", material).under(paddock)
        if solid: s.box((size[0] * 0.5, size[1] * 0.5, size[2] * 0.5))
    block("Pit Building", (0, 2.6, -24), (110, 5.2, 9), concrete)
    block("Pit Roof", (0, 5.5, -22.6), (114, 0.4, 13), roof, False)
    for k in range(11):
        block("Garage Door", (-50 + k * 10, 1.7, -19.45), (6.5, 3.2, 0.2), dark, False)
    block("Pit Wall", (0, 0.55, -12.6), (120, 1.1, 0.4), concrete)
    for row in range(6):
        block("Stand Step", (0, 0.5 + row * 0.8, 17.5 + row * 1.5), (56, 1.0 + row * 1.6, 1.5), concrete if row % 2 else seats, row == 0)
    block("Stand Roof", (0, 7.6, 22), (60, 0.35, 12), roof, False)
    for x in (-28, 28):
        block("Stand Post", (x, 3.8, 27), (0.5, 7.6, 0.5), dark)
    for side in (-1, 1):
        block("Gantry Post", (0, 3.3, side * 10.4), (0.45, 6.6, 0.45), dark)
    block("Gantry Beam", (0, 6.4, 0), (0.6, 0.7, 21.2), dark, False)
    for k in range(5):
        block("Start Light", (-0.36, 6.4, -3.2 + k * 1.6), (0.12, 0.42, 0.42), signal, False)
    # Lamps along the straight, coming on as the light goes.
    for k, x in enumerate((-48, -16, 16, 48)):
        block("Lamp Post", (x, 3.5, -11.2), (0.22, 7.0, 0.22), dark)
        block("Lamp Arm", (x, 6.9, -9.9), (0.16, 0.16, 2.6), dark, False)
        block("Lamp Head", (x, 6.78, -8.7), (0.5, 0.14, 0.9), lamp, False)
        s.entity("Lamp Light %d" % (k + 1), (x, 6.5, -8.7)).light("point", (1.0, 0.82, 0.55), 9.0, 22).under(paddock)
    # Stacks of tires where the road turns hardest.
    turns = sorted(range(len(points)), key=lambda k: -abs(_bend(points, k)))
    placed = []
    for k in turns:
        if len(placed) >= 5: break
        if any(abs(k - other) < 30 for other in placed): continue
        placed.append(k)
        side = -1 if _bend(points, k) > 0 else 1                     # on the outside of the bend
        (x0, z0), (x1, z1) = points[k - 1], points[(k + 1) % len(points)]
        dx, dz = (x1 - x0) / math.hypot(x1 - x0, z1 - z0), (z1 - z0) / math.hypot(x1 - x0, z1 - z0)
        for j in range(-3, 4):
            x = points[k][0] - dz * side * 8.6 + dx * j * 1.15
            z = points[k][1] + dx * side * 8.6 + dz * j * 1.15
            block("Tire Stack", (x, 0.5, z), (1.05, 1.0, 1.05), rubber, False)

    # ---------------- the car
    front, rear, radius = layout["frontAxle"], layout["rearAxle"], layout["wheelRadius"]
    half_track, wheelbase = abs(front[0]), rear[2] - front[2]
    ride = 0.32                                 # how high the car's own middle (and its weight) is above the road when it stands on its springs
    start = track["start"]
    s.entity("Car", (start[0], ride + 0.03, start[1]), yaw(-90))
    car = s.index()
    s.component("VehicleBody", mass=820, size=(1.6, 1.25, 3.05), dragArea=0.62)
    s.component("Motor", engine="i4", tractionControl=True, soundFolder="Audio")
    s.component("DriveControls")
    s.component("LapTimer", gates=len(track["gates"]), gateRadius=11)
    s.component("CarLights")
    # Its model, part by part: each a piece of Models/Fiat500/fiat500.glb with a material of its own.
    paint = {}
    def texture_names(look):
        return {key: "Models/Fiat500/Textures/" + look[key] for key in ("baseColorTexture", "metallicRoughnessTexture", "normalTexture") if key in look}
    special = {
        "Glass": dict(color=(0.03, 0.045, 0.05, 0.42), metallic=0.0, roughness=0.05, blend=True),
        "Headlamp_Glass": dict(color=(1.0, 0.97, 0.9, 0.5), metallic=0.0, roughness=0.05, emissive=(2.6, 2.4, 1.9), blend=True),
        "Headlamp_Refl": dict(color=(0.9, 0.9, 0.9, 1), metallic=1.0, roughness=0.12, emissive=(1.2, 1.1, 0.9)),
        "Indiactor_Front": dict(color=(0.9, 0.35, 0.04, 1), metallic=0.0, roughness=0.2, emissive=(0.5, 0.16, 0.0)),
        "RearLight": dict(color=(1, 1, 1, 1), metallic=0.0, roughness=0.15, emissive=(0.6, 0.04, 0.02)),
        "SpoilerLight": dict(color=(1, 1, 1, 1), metallic=0.0, roughness=0.2, emissive=(0.5, 0.03, 0.02)),
        "Fire": dict(color=(0.05, 0.02, 0.01, 1), metallic=0.0, roughness=0.6, emissive=(2.2, 0.7, 0.12)),
        "Chrome": dict(color=(0.82, 0.82, 0.84, 1), metallic=1.0, roughness=0.08),
    }
    for name, look in layout["materials"].items():
        settings = dict(color=look["baseColor"], metallic=look["metallic"], roughness=max(look["roughness"], 0.05))
        settings.update(special.get(name, {}))
        paint[name] = material_file(folder, "Fiat " + name.replace("_", " "), textures=texture_names(look), **settings)
    s.entity("Model", (0, -ride, 0)).under(car)
    model = s.index()
    wheel_parts = []
    for number, part in enumerate(layout["parts"]):
        mesh = "Models/Fiat500/fiat500.glb#%d" % number
        if part["name"].startswith("Wheel "):
            wheel_parts.append((part["name"], mesh, paint[part["material"]]))
            continue
        shown = "Rear Lights" if part["material"] == "RearLight" else part["name"].replace("Body ", "").replace("_", " ")
        s.entity(shown).mesh(mesh, paint[part["material"]], part["material"] not in ("Glass", "Headlamp_Glass")).under(model)
    # Its lamps: two beams ahead, and two red glows behind that the script brightens under braking.
    lamps, tail, pipe = layout["headlamps"], layout["rearLights"], layout["exhaust"]
    lamp_height = (lamps[0][1] + lamps[1][1]) * 0.5 - ride
    for name, x in (("Headlight L", lamps[0][0] + 0.12), ("Headlight R", lamps[1][0] - 0.12)):
        s.entity(name, (x, lamp_height, lamps[0][2] - 0.12), pitch(-5)).light("spot", (1.0, 0.93, 0.8), 16, 55).under(car)
    tail_height = (tail[0][1] + tail[1][1]) * 0.5 - ride
    for name, x in (("Brake Light L", tail[0][0] + 0.1), ("Brake Light R", tail[1][0] - 0.1)):
        s.entity(name, (x, tail_height, tail[1][2] + 0.25)).light("point", (1.0, 0.06, 0.03), 0.4, 5).under(car)
    # What comes out of the rocket where the engine was: smoke always, fire with the throttle down.
    pipe_at = ((pipe[0][0] + pipe[1][0]) * 0.5, (pipe[0][1] + pipe[1][1]) * 0.5 - ride, pipe[1][2] + 0.22)
    s.entity("Exhaust Smoke", pipe_at).under(car)
    effect(s, rate=10, lifetimeMin=0.9, lifetimeMax=1.9, speedMin=0.8, speedMax=1.6, direction=(0, 0.18, 1), spread=math.radians(14), spawnRadius=0.07,
           gravity=(0, 0.55, 0), drag=1.4, turbulence=0.8, turbulenceSize=0.6, windInfluence=0.5, isAdditive=False, hasMiddle=True, middleAt=0.25,
           colorStart=(0.5, 0.5, 0.52, 0.0), colorMiddle=(0.46, 0.46, 0.48, 0.34), colorEnd=(0.6, 0.6, 0.62, 0.0),
           sizeStart=0.12, sizeMiddle=0.5, sizeEnd=1.3, maxParticles=260)
    s.component("Exhaust", idleRate=9, fullRate=85)
    s.entity("Exhaust Flame", (pipe_at[0], pipe_at[1], pipe_at[2] - 0.08)).under(car)
    effect(s, rate=120, lifetimeMin=0.09, lifetimeMax=0.2, speedMin=5, speedMax=10, direction=(0, 0, 1), spread=math.radians(7), spawnRadius=0.09,
           gravity=(0, 0.6, 0), drag=3.0, isAdditive=True, isEmitting=False, hasMiddle=True, middleAt=0.3,
           colorStart=(2.4, 3.2, 6.0, 0.9), colorMiddle=(6.0, 2.6, 0.5, 0.85), colorEnd=(1.6, 0.25, 0.03, 0.0),
           sizeStart=0.2, sizeMiddle=0.26, sizeEnd=0.06, maxParticles=160)
    s.component("Exhaust", fullRate=160, isFlame=True)

    half_turn = yaw(180)
    for name, x, z in (("Wheel FL", -half_track, front[2]), ("Wheel FR", half_track, front[2]), ("Wheel RL", -half_track, rear[2]), ("Wheel RR", half_track, rear[2])):
        is_front = z < 0
        s.entity(name, (x, -0.041, z)).under(car)
        wheel = s.index()
        # The engine is behind, where the rocket is now, and drives the rear wheels: as the little car always did.
        s.component("Wheel", radius=radius, grip=1.28, steerAngle=33 if is_front else 0, steersWith="steer", isDriven=not is_front,
                    brakeTorque=1600 if is_front else 700, handbrakeTorque=0 if is_front else 3200)
        s.component("Suspension", travel=0.15, springRate=30000, damping=2500, antiRoll=7000)
        s.component("DeformableTire", pressure=2.0)
        s.entity("Spin").under(wheel)
        spin = s.index()
        # The model's wheel is a left one: on the right it is turned around.
        s.entity("Rim and Tire", rotation=(0, 0, 0, 1) if x < 0 else half_turn).under(spin)
        holder = s.index()
        for part_name, mesh, material in wheel_parts:
            s.entity(part_name.replace("Wheel ", "").replace("Wheels", "Rim")).mesh(mesh, material).under(holder)
        if not is_front:
            s.entity("Tyre Smoke", (0, -radius + 0.05, 0.1)).under(wheel)
            effect(s, rate=0, lifetimeMin=0.7, lifetimeMax=1.5, speedMin=0.4, speedMax=1.4, direction=(0, 0.5, 1), spread=math.radians(50), spawnRadius=0.12,
                   gravity=(0, 0.5, 0), drag=1.6, turbulence=0.6, turbulenceSize=0.8, windInfluence=0.4, isAdditive=False, isEmitting=False, hasMiddle=True,
                   middleAt=0.2, colorStart=(0.8, 0.8, 0.8, 0.0), colorMiddle=(0.78, 0.78, 0.78, 0.3), colorEnd=(0.85, 0.85, 0.85, 0.0),
                   sizeStart=0.25, sizeMiddle=0.8, sizeEnd=1.8, maxParticles=220)
            s.component("TyreSmoke", most=90)

    s.entity("Camera", (start[0] - 6, 2.2, start[1]), yaw(-90)).camera(64, True, 3000, near=0.25)
    s.component("Follow", target="Car", offset=(0, 1.55, 4.7), smoothness=7, looksAtTarget=True, turnsWithTarget=True)

    # ---------------- the interface: each piece an entity, laid out in the Game view
    s.entity("Interface")
    ui = s.index()
    def piece(name, component, **fields):
        s.entity(name).under(ui)
        s.component(component, **fields)
    glass_dark, rim = (0.04, 0.045, 0.055, 0.62), (1, 1, 1, 0.16)
    # The dashboard, bottom middle: speed, gear, revs.
    piece("Dash", "UIPanel", anchor=(0.5, 1), offset=(0, -62), size=(296, 92), color=glass_dark, border=1.5, borderColor=rim)
    piece("Speed", "HudText", text="{speed}", anchor=(0.5, 1), offset=(-52, -103), size=50, color=(1, 1, 1, 1))
    piece("Speed Unit", "HudText", text="km/h", anchor=(0.5, 1), offset=(-52, -48), size=13, color=(1, 1, 1, 0.55))
    piece("Gear Box", "UIPanel", anchor=(0.5, 1), offset=(84, -70), size=(64, 60), color=(0.95, 0.47, 0.18, 0.95))
    piece("Gear", "HudText", text="{gearIndex:R|1|2|3|4|5|6|7}", anchor=(0.5, 1), offset=(84, -96), size=40, color=(0.08, 0.05, 0.03, 1))
    piece("Revs", "UIBar", value="revs", most=1, anchor=(0.5, 1), offset=(0, -24), size=(272, 8), color=(1.0, 0.5, 0.18, 1), backColor=(1, 1, 1, 0.12))
    piece("Engine", "HudText", text="{engine:I4|I5|I6|V8|V12|DIESEL|ELECTRIC}   {abs:ABS OFF|ABS}", anchor=(0.5, 1), offset=(0, -128), size=11,
          color=(1, 1, 1, 0.55))
    # The laps, top middle.
    piece("Lap Panel", "UIPanel", anchor=(0.5, 0), offset=(0, 44), size=(330, 60), color=glass_dark, border=1.5, borderColor=rim)
    piece("Lap", "HudText", text="LAP {lap}", anchor=(0.5, 0), offset=(-112, 34), size=17, color=(1.0, 0.6, 0.3, 1))
    piece("Lap Time", "HudText", text="{lapTime:time}", anchor=(0.5, 0), offset=(0, 25), size=32, color=(1, 1, 1, 1))
    piece("Best", "HudText", text="BEST", anchor=(0.5, 0), offset=(112, 24), size=10, color=(1, 1, 1, 0.5))
    piece("Best Time", "HudText", text="{bestLap:time}", anchor=(0.5, 0), offset=(112, 37), size=17, color=(0.6, 0.9, 0.65, 1))
    # What the keys do: for a keyboard. DriveControls hides it once the screen is touched.
    piece("Help", "HudText", text="W A S D  drive      Space  handbrake", anchor=(0, 1), offset=(18, -40), size=11, color=(1, 1, 1, 0.55))
    piece("Help 2", "HudText", text="1-7  engine      B  ABS      L  lamps      T  touch", anchor=(0, 1), offset=(18, -24), size=11, color=(1, 1, 1, 0.55))
    # For a touch screen: a stick to steer with on the left, the pedals and the handbrake on the right.
    piece("Steering Stick", "TouchStick", horizontal="moveX", vertical="", anchor=(0, 1), offset=(120, -118), radius=66, onlyOnTouchScreens=True, color=(1, 1, 1, 0.55))
    piece("Gas", "TouchButton", action="gas", label="GAS", anchor=(1, 1), offset=(-96, -118), radius=58, onlyOnTouchScreens=True, color=(0.55, 1.0, 0.6, 0.6))
    piece("Brake", "TouchButton", action="brake", label="BRAKE", anchor=(1, 1), offset=(-226, -84), radius=46, onlyOnTouchScreens=True, color=(1.0, 0.45, 0.4, 0.6))
    piece("Handbrake", "TouchButton", action="jump", label="HB", anchor=(1, 1), offset=(-212, -196), radius=30, onlyOnTouchScreens=True, color=(1, 1, 1, 0.5))
    write(folder + "/Assets/Scenes/Main.scene", s.text())

def then(first, second):
    """Two rotations as one: `second` in the frame `first` makes (quaternions x y z w)."""
    ax, ay, az, aw = first
    bx, by, bz, bw = second
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)

def _bend(points, k):
    """How sharply a closed line of points turns at one of them, signed."""
    count = len(points)
    def direction(i):
        (x0, z0), (x1, z1) = points[(i - 1) % count], points[(i + 1) % count]
        length = math.hypot(x1 - x0, z1 - z0)
        return ((x1 - x0) / length, (z1 - z0) / length)
    (ax, az), (bx, bz) = direction(k - 2), direction(k + 2)
    return math.atan2(ax * bz - az * bx, ax * bx + az * bz) / 8.0

# ---------------------------------------------------------------- Flying
if __name__ == "__main__":
    driving()
    print("wrote the example projects to", os.path.normpath(ROOT))
    if "--zip" in sys.argv:
        import zipfile
        target = os.path.normpath(ROOT) + ".zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
            for folder, folders, files in os.walk(ROOT):
                folders[:] = sorted(d for d in folders if d not in ("Library", "Build", "build"))
                for name in sorted(files):
                    if name == ".DS_Store":
                        continue
                    full = os.path.join(folder, name)
                    archive.write(full, os.path.join("MoltenProjects", os.path.relpath(full, ROOT)))
        print("and", target)
