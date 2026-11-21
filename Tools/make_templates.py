#!/usr/bin/env python3
"""Writes the example projects, which are not part of the engine but a download of their
own: into ../MoltenProjects, or the folder given as the first argument (Driving; Showcase too
with --showcase); with --zip, also as MoltenProjects.zip beside that folder. Small, complete games made only of
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

    def inactive(self):
        """The entity just added starts switched off: it and everything under it (a menu the game opens)."""
        self.lines.append("  inactive")
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

    def body(self, kind, mass=1, restitution=0.1, friction=0.6, damping=0.05, gravity=1, rotates=False, turning_damping=0.08):
        """`rotates`: it tumbles when pushed off center (crates, dominoes); off, it only slides and falls."""
        self.lines.append("  body %s %s %d %s" % (kind, n(mass, restitution, friction, damping, gravity), 1 if rotates else 0, n(turning_damping)))
        return self

    def capsule(self, radius, half_height, offset=(0, 0, 0), trigger=False):
        self.lines.append("  collider capsule " + n(radius, half_height, *offset) + (" 1" if trigger else " 0"))
        return self

    def mesh_collider(self, mesh, convex=False):
        self.lines.append("  collider mesh %s 0 0 0 0 %d" % (q(mesh), 1 if convex else 0))
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
    # A project file already there is kept: its name, version and identifier are the game's own.
    if not os.path.exists(os.path.join(ROOT, folder, folder + ".molten")):
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

def sounds(folder, engines=None):
    """`engines`: only the recordings of these ("i4"...): a game with one engine ships one."""
    import make_sounds
    audio = os.path.join(ROOT, folder, "Assets", "Audio")
    if engines is not None and os.path.isdir(audio):
        for name in os.listdir(audio):
            if name.startswith(("engine_", "propeller_", "stall")): os.remove(os.path.join(audio, name))
    make_sounds.write_all(audio, engines)

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

def add_car(s, folder, layout, start, heading, gates=0, far=3000, name="Car", rival=None):
    """The Fiat 500, its camera and its interface, standing at `start` (x, z) and facing `heading` degrees.
    With `gates`, it times laps through that many "Gate N" entities and shows them.
    With `rival` (its colour, skill and line), a car the computer drives instead: no camera, no interface."""
    # ---------------- the car
    front, rear, radius = layout["frontAxle"], layout["rearAxle"], layout["wheelRadius"]
    half_track, wheelbase = abs(front[0]), rear[2] - front[2]
    ride = 0.32                                 # how high the car's own middle (and its weight) is above the road when it stands on its springs
    s.entity(name, (start[0], ride + 0.03, start[1]), yaw(heading))
    car = s.index()
    # Stability control lets it slide up to a drift and catches it there; a sliding tire keeps
    # most of its grip, so that a slide can be held with the throttle and the wheel.
    s.component("VehicleBody", mass=820, size=(1.6, 1.25, 3.05), dragArea=0.62, stability=0.85, driftAngle=16, slideGrip=0.82)
    s.component("Motor", maxBoost=1.1, spoolRate=1.8, tractionControl=True, soundFolder="Audio", isPlayers=rival is None)
    if rival:
        s.component("RacerAI", skill=rival["skill"], topSpeed=160, lineOffset=rival["line"])
    else:
        s.component("DriveControls")
    if gates and not rival:
        s.component("LapTimer", gates=gates, gateRadius=11)
        # Tyres that wear, and the pit stop that changes them.
        s.component("Tyres", compound=1, wearRate=1.0, pitSpeedLimit=60, pitStopSeconds=4)
    s.component("CarLights", answersKeys=rival is None)
    # Near the camera, the model in full; from 35 meters, the far one (Models/Fiat500/fiat500_far.glb).
    s.component("LodGroup", distance=35, far="Far")
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
        textures = texture_names(look)
        if rival and name == "Body_Paint":
            # A rival's own colour: the same paint, wear and decals, another colour (Tools/repaint_fiat.py).
            textures["baseColorTexture"] = "Models/Fiat500/Textures/Body_Paint_baseColor_%s.jpg" % rival["colour"]
            paint[name] = material_file(folder, "Fiat Body Paint " + rival["colour"].title(), textures=textures, **settings)
            continue
        paint[name] = material_file(folder, "Fiat " + name.replace("_", " "), textures=textures, **settings)
    s.entity("Model", (0, -ride, 0)).under(car)
    model = s.index()
    wheel_parts = []
    far_parts = []
    for number, part in enumerate(layout["parts"]):
        mesh = "Models/Fiat500/fiat500.glb#%d" % number
        far_mesh = "Models/Fiat500/fiat500_far.glb#%d" % number
        if part["name"].startswith("Wheel "):
            wheel_parts.append((part["name"], mesh, far_mesh, paint[part["material"]]))
            continue
        shown = "Rear Lights" if part["material"] == "RearLight" else part["name"].replace("Body ", "").replace("_", " ")
        casts = part["material"] not in ("Glass", "Headlamp_Glass")
        s.entity(shown).mesh(mesh, paint[part["material"]], casts).under(model)
        # The car hits the world with its body's own shape: a convex Mesh Collider of the shell,
        # as in Unity. (The VehicleBody's box is then not used.)
        if part["material"] == "Body_Paint":
            s.mesh_collider(mesh, convex=True)
        far_parts.append((shown, far_mesh, paint[part["material"]], casts))
    # The far version, part by part: drawn instead of the parts above from 35 meters.
    s.entity("Far").under(model)
    far_model = s.index()
    for shown, far_mesh, material, casts in far_parts:
        s.entity(shown).mesh(far_mesh, material, casts).under(far_model)
    # Its lamps: two beams ahead, and two red glows behind that the script brightens under braking.
    lamps, tail, pipe = layout["headlamps"], layout["rearLights"], layout["exhaust"]
    lamp_height = (lamps[0][1] + lamps[1][1]) * 0.5 - ride
    for name, x in (("Headlight L", lamps[0][0] + 0.12), ("Headlight R", lamps[1][0] - 0.12)):
        s.entity(name, (x, lamp_height, lamps[0][2] - 0.12), pitch(-5)).light("spot", (1.0, 0.93, 0.8), 16, 55).under(car)
    tail_height = (tail[0][1] + tail[1][1]) * 0.5 - ride
    for name, x in (("Brake Light L", tail[0][0] + 0.1), ("Brake Light R", tail[1][0] - 0.1)):
        s.entity(name, (x, tail_height, tail[1][2] + 0.25)).light("point", (1.0, 0.06, 0.03), 0.4, 5).under(car)
    # What comes out of the rocket where the engine was: a little smoke, more with the throttle down.
    pipe_at = ((pipe[0][0] + pipe[1][0]) * 0.5, (pipe[0][1] + pipe[1][1]) * 0.5 - ride, pipe[1][2] + 0.22)
    s.entity("Exhaust Smoke", pipe_at).under(car)
    effect(s, rate=10, lifetimeMin=0.9, lifetimeMax=1.9, speedMin=0.8, speedMax=1.6, direction=(0, 0.18, 1), spread=math.radians(14), spawnRadius=0.07,
           gravity=(0, 0.55, 0), drag=1.4, turbulence=0.8, turbulenceSize=0.6, windInfluence=0.5, isAdditive=False, hasMiddle=True, middleAt=0.25,
           colorStart=(0.5, 0.5, 0.52, 0.0), colorMiddle=(0.46, 0.46, 0.48, 0.34), colorEnd=(0.6, 0.6, 0.62, 0.0),
           sizeStart=0.12, sizeMiddle=0.5, sizeEnd=1.3, maxParticles=260)
    s.component("Exhaust", idleRate=9, fullRate=85)

    half_turn = yaw(180)
    for name, x, z in (("Wheel FL", -half_track, front[2]), ("Wheel FR", half_track, front[2]), ("Wheel RL", -half_track, rear[2]), ("Wheel RR", half_track, rear[2])):
        is_front = z < 0
        s.entity(name, (x, -0.041, z)).under(car)
        wheel = s.index()
        # The engine is behind, where the rocket is now, and drives the rear wheels: as the little car always did.
        s.component("Wheel", radius=radius, grip=1.24 if is_front else 1.42, steerAngle=33 if is_front else 0, steersWith="steer", isDriven=not is_front,
                    brakeTorque=1600 if is_front else 700, handbrakeTorque=0 if is_front else 3200)
        s.component("Suspension", travel=0.15, springRate=30000, damping=2500, antiRoll=7000)
        s.component("DeformableTire", pressure=2.0)
        s.entity("Spin").under(wheel)
        spin = s.index()
        # The model's wheel is a left one: on the right it is turned around.
        s.entity("Rim and Tire", rotation=(0, 0, 0, 1) if x < 0 else half_turn).under(spin)
        holder = s.index()
        for part_name, mesh, far_mesh, material in wheel_parts:
            s.entity(part_name.replace("Wheel ", "").replace("Wheels", "Rim")).mesh(mesh, material).under(holder)
        s.entity("Far").under(holder)
        far_wheel = s.index()
        for part_name, mesh, far_mesh, material in wheel_parts:
            s.entity(part_name.replace("Wheel ", "").replace("Wheels", "Rim")).mesh(far_mesh, material).under(far_wheel)
        # Smoke where the tire meets the road, when it slides: it rolls along the ground and spreads.
        s.entity("Tyre Smoke", (0, -radius + 0.08, 0.15)).under(wheel)
        effect(s, rate=0, lifetimeMin=0.9, lifetimeMax=2.0, speedMin=0.3, speedMax=1.2, direction=(0, 0.25, 1), spread=math.radians(60), spawnRadius=0.15,
               gravity=(0, 0.25, 0), drag=1.4, turbulence=0.7, turbulenceSize=0.9, windInfluence=0.5, isAdditive=False, isEmitting=False, hasMiddle=True,
               middleAt=0.18, colorStart=(0.82, 0.82, 0.82, 0.0), colorMiddle=(0.8, 0.8, 0.8, 0.38), colorEnd=(0.86, 0.86, 0.86, 0.0),
               sizeStart=0.3, sizeMiddle=1.0, sizeEnd=2.4, maxParticles=260)
        s.component("TyreSmoke", most=110 if not is_front else 70)

    # The black marks the tires leave where they slide.
    marks = material_file(folder, "Skid Marks", (0.015, 0.015, 0.015, 0.62), 0.0, 0.9, blend=True, double_sided=True)
    s.entity("Skid Marks" if not rival else "Skid Marks " + name).mesh("plane", marks, False)
    s.component("SkidMarks", car=name, segmentsPerWheel=400 if not rival else 160, width=round(layout.get("wheelWidth", 0.19), 3), spacing=0.22)
    if rival:
        return

    s.entity("Camera", (start[0] - 6 * math.sin(math.radians(-heading)), 2.2, start[1] + 6 * math.cos(math.radians(-heading))), yaw(heading)).camera(64, True, far, near=0.25)
    s.component("Follow", target="Car", offset=(0, 1.55, 4.7), smoothness=7, looksAtTarget=True, turnsWithTarget=True)
    camera = s.index()
    if gates:
        # The rain: streaks falling in a box around the camera, as many as Weather says.
        s.entity("Rain", (0, 6, -9)).under(camera)
        effect(s, rate=0, isEmitting=False, lifetimeMin=0.75, lifetimeMax=0.95, speedMin=15, speedMax=18, direction=(0, -1, 0), spread=math.radians(4),
               gravity=(0, -4, 0), drag=0.0, windInfluence=0.35, isAdditive=True, colorStart=(0.55, 0.58, 0.64, 0.42), colorEnd=(0.55, 0.58, 0.64, 0.3),
               sizeStart=0.022, sizeEnd=0.022, maxParticles=2600, shape=1, boxSize=(30, 2, 30), stretch=0.045)

    # ---------------- the interface: each piece an entity, laid out in the Game view
    s.entity("Interface")
    ui = s.index()
    def piece(name, component, **fields):
        s.entity(name).under(ui)
        s.component(component, **fields)
    glass_dark, rim = (0.04, 0.045, 0.055, 0.62), (1, 1, 1, 0.16)
    # The dashboard, bottom middle: a speedometer, a rev counter with the gear in it, and the boost.
    piece("Speedometer", "UIGauge", value="speed", most=200, numbersEvery=40, numbersScale=1, redFrom=0, label="km/h", showsValue=True,
          anchor=(0.5, 1), offset=(-92, -96), radius=80, thickness=7, color=(1.0, 0.5, 0.18, 1), backColor=(0.04, 0.045, 0.055, 0.72))
    piece("Rev Counter", "UIGauge", value="rpm", most=7000, numbersEvery=1000, numbersScale=0.001, redFrom=0.9, label="rpm x 1000", showsValue=False,
          anchor=(0.5, 1), offset=(78, -84), radius=66, thickness=6, color=(0.95, 0.95, 0.97, 1), backColor=(0.04, 0.045, 0.055, 0.72))
    piece("Gear", "HudText", text="{gearIndex:R|1|2|3|4|5|6|7}", anchor=(0.5, 1), offset=(78, -122), size=30, color=(1.0, 0.6, 0.3, 1))
    piece("Boost", "UIGauge", value="boost", most=1.2, numbersEvery=0.4, numbersScale=1, redFrom=0, label="boost", showsValue=False,
          anchor=(0.5, 1), offset=(184, -62), radius=40, thickness=5, color=(0.45, 0.8, 1.0, 1), backColor=(0.04, 0.045, 0.055, 0.72))
    piece("ABS", "HudText", text="{abs:ABS OFF|ABS}", anchor=(0.5, 1), offset=(-92, -196), size=11, color=(1, 1, 1, 0.55))
    if gates:
        # The laps, top middle.
        piece("Lap Panel", "UIPanel", anchor=(0.5, 0), offset=(0, 44), size=(330, 60), color=glass_dark, border=1.5, borderColor=rim)
        piece("Lap", "HudText", text="LAP {lap}", anchor=(0.5, 0), offset=(-112, 34), size=17, color=(1.0, 0.6, 0.3, 1))
        piece("Lap Time", "HudText", text="{lapTime:time}", anchor=(0.5, 0), offset=(0, 25), size=32, color=(1, 1, 1, 1))
        piece("Best", "HudText", text="BEST", anchor=(0.5, 0), offset=(112, 24), size=10, color=(1, 1, 1, 0.5))
        piece("Best Time", "HudText", text="{bestLap:time}", anchor=(0.5, 0), offset=(112, 37), size=17, color=(0.6, 0.9, 0.65, 1))
        # All four wheels on the grass: the lap still runs, but it does not count.
        piece("Lap Invalid", "HudText", text="{lapValid:LAP INVALID  -  TRACK LIMITS|}", anchor=(0.5, 0), offset=(0, 82), size=15, color=(1.0, 0.35, 0.3, 1))
        # The tyres, top left beside the pause button; the weather, top right.
        piece("Tyre Panel", "UIPanel", anchor=(0, 0), offset=(186, 44), size=(170, 60), color=glass_dark, border=1.5, borderColor=rim)
        piece("Tyre Compound", "HudText", text="{tyreCompound:SOFT|MEDIUM|HARD|WET}", anchor=(0, 0), offset=(112, 22), size=17,
              color=(1.0, 0.85, 0.35, 1))
        piece("Tyre Wear", "HudText", text="{tyreWear}% WORN", anchor=(0, 0), offset=(112, 46), size=12, color=(1, 1, 1, 0.7))
        piece("Weather Now", "HudText", text="{weather:SUN|CLOUDS|RAIN}", anchor=(1, 0), offset=(-28, 30), size=14, color=(1, 1, 1, 0.7))
        piece("Pause Button", "UIButton", label="II", isRound=True, radius=26, anchor=(0, 0), offset=(46, 44), color=(1, 1, 1, 0.45))
        # In the pit lane: the limiter, the stop, and the tyres for it.
        s.entity("Pit Panel").under(ui).inactive()               # shown by Tyres in the pit lane
        pit_panel = s.index()
        def pit_piece(name, component, **fields):
            s.entity(name).under(pit_panel)
            s.component(component, **fields)
        pit_piece("Pit Backdrop", "UIPanel", anchor=(0.5, 0), offset=(0, 168), size=(470, 112), color=glass_dark, border=1.5, borderColor=rim)
        pit_piece("Pit Limiter", "HudText", text="PIT LANE   60 KM/H", anchor=(0.5, 0), offset=(0, 120), size=15, color=(1.0, 0.85, 0.35, 1))
        pit_piece("Pit Status", "HudText", text="{pitStop:STOP IN YOUR BOX FOR NEW TYRES|CHANGING TYRES  1|CHANGING TYRES  2|CHANGING TYRES  3|CHANGING TYRES  4}",
                  anchor=(0.5, 0), offset=(0, 142), size=12, color=(1, 1, 1, 0.8))
        for k, (name, label, color) in enumerate((("Tyre Soft", "SOFT", (0.95, 0.3, 0.3, 0.9)), ("Tyre Medium", "MEDIUM", (0.95, 0.8, 0.25, 0.9)),
                                                  ("Tyre Hard", "HARD", (0.85, 0.85, 0.88, 0.9)), ("Tyre Wet", "WET", (0.3, 0.6, 1.0, 0.9)))):
            pit_piece(name, "UIButton", label=label, anchor=(0.5, 0), offset=(-165 + k * 110, 194), size=(100, 40), color=color, textColor=(0.05, 0.05, 0.06, 1))
    # What the keys do: for a keyboard. DriveControls hides it once the screen is touched.
    piece("Help", "HudText", text="W A S D  drive      Space  handbrake: drift", anchor=(0, 1), offset=(18, -40), size=11, color=(1, 1, 1, 0.55))
    piece("Help 2", "HudText", text="B  ABS      L  lamps      R  back on the wheels      T  touch controls      Esc  pause", anchor=(0, 1), offset=(18, -24), size=11, color=(1, 1, 1, 0.55))
    # For a touch screen: a stick to steer with on the left, the pedals and the handbrake on the right.
    # Steering: two buttons under the left thumb, as the pedals are under the right one.
    # Thumb-sized: on a phone the interface is drawn at about 60%, so these are larger than they look here.
    piece("Steer Left", "UIButton", label="\u2190", action="steerLeft", isRound=True, radius=78, anchor=(0, 1), offset=(104, -112), onlyOnTouchScreens=True, color=(1, 1, 1, 0.55))
    piece("Steer Right", "UIButton", label="\u2192", action="steerRight", isRound=True, radius=78, anchor=(0, 1), offset=(296, -112), onlyOnTouchScreens=True, color=(1, 1, 1, 0.55))
    piece("Gas", "UIButton", label="GAS", action="gas", isRound=True, radius=86, anchor=(1, 1), offset=(-114, -124), onlyOnTouchScreens=True, color=(0.55, 1.0, 0.6, 0.6))
    piece("Brake", "UIButton", label="BRAKE", action="brake", isRound=True, radius=70, anchor=(1, 1), offset=(-306, -94), onlyOnTouchScreens=True, color=(1.0, 0.45, 0.4, 0.6))
    piece("Handbrake", "UIButton", label="HB", action="jump", isRound=True, radius=46, anchor=(1, 1), offset=(-286, -258), onlyOnTouchScreens=True, color=(1, 1, 1, 0.5))
    return ui

def add_race_interface(s, ui):
    """The race's own pieces: the place and the lap, the start lights, and the results (RaceDirector)."""
    def piece(parent, name, component, **fields):
        s.entity(name).under(parent)
        s.component(component, **fields)
    s.entity("Race Panel").under(ui)
    panel = s.index()
    piece(panel, "Position", "HudText", text="P{position}", anchor=(1, 0), offset=(-28, 56), size=40, color=(1, 1, 1, 1))
    piece(panel, "Cars", "HudText", text="OF {cars}", anchor=(1, 0), offset=(-28, 102), size=12, color=(1, 1, 1, 0.6))
    piece(panel, "Race Lap", "HudText", text="LAP {raceLap} / {raceLaps}", anchor=(1, 0), offset=(-28, 122), size=15, color=(1.0, 0.6, 0.3, 1))
    s.entity("Start Lights").under(ui)
    lights = s.index()
    piece(lights, "Start Lights Back", "UIPanel", anchor=(0.5, 0), offset=(0, 160), size=(372, 84), color=(0.03, 0.03, 0.035, 0.8))
    for k in range(5):
        piece(lights, "Light %d" % (k + 1), "UIPanel", anchor=(0.5, 0), offset=(-144 + k * 72, 160), size=(56, 56), color=(0.12, 0.12, 0.14, 0.85))
    # The results: off until the player finishes.
    s.entity("Results").inactive()
    results = s.index()
    piece(results, "Results Backdrop", "UIPanel", anchor=(0.5, 0.5), offset=(0, 0), size=(5000, 5000), color=(0.02, 0.022, 0.03, 0.7))
    piece(results, "Results Title", "HudText", text="RESULTS", anchor=(0.5, 0.5), offset=(0, -250), size=40, color=(1, 1, 1, 1))
    for k in range(5):
        y = -160 + k * 52
        piece(results, "Result %d Place" % (k + 1), "HudText", text="%d" % (k + 1), anchor=(0.3, 0.5), offset=(40, y), size=24, color=(1, 1, 1, 0.5))
        piece(results, "Result %d" % (k + 1), "HudText", text="", anchor=(0.3, 0.5), offset=(90, y), size=24, color=(1, 1, 1, 0.9))
        piece(results, "Result %d Time" % (k + 1), "HudText", text="", anchor=(0.7, 0.5), offset=(-40, y), size=24, color=(1, 1, 1, 0.9))
    orange = (1.0, 0.5, 0.18, 0.95)
    for name, label, y, colour in (("Race Again", "RACE AGAIN", 140, orange), ("Leave Race", "MAIN MENU", 212, (1, 1, 1, 0.9))):
        piece(results, name, "UIButton", label=label, anchor=(0.5, 0.5), offset=(0, y), size=(320, 54), color=colour, textColor=(0.05, 0.05, 0.06, 1))

def driving():
    import json, make_models
    folder = "Driving"
    fiat = os.path.join(models(folder), "Fiat500")
    if not os.path.exists(os.path.join(fiat, "layout.json")):
        print("Driving: its car is not there. Make it first:  python3 Tools/import_fiat500.py <the model's folder> " + fiat)
        return
    layout = json.load(open(os.path.join(fiat, "layout.json")))
    # The car seen from far away (a few percent of its triangles), and the rivals' colours: made once, from the car itself.
    import subprocess
    tools = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(os.path.join(fiat, "fiat500_far.glb")):
        subprocess.run([sys.executable, os.path.join(tools, "make_far_model.py"), os.path.join(fiat, "fiat500.glb"), os.path.join(fiat, "fiat500_far.glb")], check=True)
    if not os.path.exists(os.path.join(fiat, "Textures", "Body_Paint_baseColor_red.jpg")):
        subprocess.run([sys.executable, os.path.join(tools, "repaint_fiat.py"), os.path.join(fiat, "Textures")], check=True)
    project(folder, "Driving", "A little circuit and a Fiat 500 with a rocket where its engine was: springs and dampers, tires that slide and smoke, "
            "a turbocharged engine, four rivals to race from a grid and five start lights, or a time trial alone, tyres that wear and a pit lane to change them, sun or rain, lap times, a menu, and controls for a touch screen.",
            scripts=("DriveControls.cpp", "Motor.hpp", "Motor.cpp", "Follow.cpp", "LapTimer.cpp", "CarLights.cpp", "Exhaust.cpp", "TyreSmoke.cpp", "SkidMarks.cpp",
                     "Tyres.cpp", "Weather.cpp", "GameMenu.cpp", "RacerAI.cpp", "RaceDirector.cpp"))
    write(folder + "/CREDITS.txt", "The car: " + layout["credit"] + "\nMade smaller and rearranged for this game by Tools/import_fiat500.py.\n")
    sounds(folder, ("i4",))
    # What the older version of this game was made of is not used any more.
    for old in ("car_body", "car_glass", "car_headlights", "car_taillights", "car_trim", "wheel_tire", "wheel_rim", "track_road", "track_curb_red",
                "track_curb_white", "track_line"):
        stale = os.path.join(models(folder), old + ".obj")
        if os.path.exists(stale): os.remove(stale)
    track = make_models.circuit(models(folder))
    points = track["points"]
    pit = track["pit"]
    def near_road(x, z):
        return (any((x - px) ** 2 + (z - pz) ** 2 < 17 ** 2 for px, pz in points[::3]) or any((x - px) ** 2 + (z - pz) ** 2 < 12 ** 2 for px, pz in pit[::3])
                or (-75 < x < 75 and -40 < z < -12) or (-34 < x < 34 and 14 < z < 34) or not (-150 < x < 150 and -60 < z < 130))
    # A small place: the circuit and what is beside it, a few trees around it, nothing beyond.
    make_models.trees(models(folder) + "/trees.obj", 26, 0, 150, near_road)
    if os.path.exists(models(folder) + "/hills.obj"): os.remove(models(folder) + "/hills.obj")

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
    rubber = s.material("Tyre Wall", (0.02, 0.02, 0.022, 1), 0, 0.9)

    # The sun low in the west, behind the first corner: long shadows across the straight.
    s.entity("Sun", (0, 40, 0), then(yaw(118), pitch(-19))).light("directional", (1.0, 0.78, 0.55), 4.6, 10, True)
    # The grass is drawn a little below where it is solid, and the road exactly there: surfaces that
    # lie on one another flicker, and wheels should stand on the road they are seen on.
    # Only as much ground as there is circuit: the mist hides where it ends.
    s.entity("Ground", (0, -0.08, 35), scale=(340, 1, 230)).mesh("plane", grass).box((170, 0.5, 115), (0, -0.42, 0))
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
    # The pit lane: where it is (for the limiter), the box where tyres are changed, the line
    # through it (for the track limits) and its gate (it crosses the finish line too).
    s.entity("Pit Lane", (0, 0, track["pit"][len(pit) // 2][1]), scale=(132, 1, track["pit_half"] * 2 + 0.8))
    bx, bz = track["pit_box"]
    s.entity("Pit Box", (bx, 0, bz), scale=(5.2, 1, 2.8))
    s.entity("Pit Gate", (0, 0, pit[len(pit) // 2][1]))
    s.entity("Pit Line")
    pit_index = s.index()
    for number, (x, z) in enumerate(pit[::2]):
        s.entity("Pit Point %d" % (number + 1), (x, 0, z)).under(pit_index)
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
        block("Start Light %d" % (k + 1), (-0.36, 6.4, -3.2 + k * 1.6), (0.12, 0.42, 0.42), signal, False)
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

    # The grid: five places behind the line, from the front. The rivals take the first four, the player the last.
    s.entity("Grid")
    grid_index = s.index()
    for number, (x, z) in enumerate(track["grid"]):
        s.entity("Grid %d" % (number + 1), (x, 0, z), yaw(-90)).under(grid_index)
    # The four rivals: each its own colour, a little faster or slower, on its own side of the line.
    rivals = (("Rival 1", "red", 0.93, -0.6), ("Rival 2", "blue", 0.9, 0.5), ("Rival 3", "white", 0.87, -0.3), ("Rival 4", "yellow", 0.84, 0.4))
    # The player's car first: what is found by name ("Wheel FL") is its own.
    ui = add_car(s, folder, layout, track["grid"][4], -90, gates=len(track["gates"]), far=700)
    for (name, colour, skill, offset), (x, z) in zip(rivals, track["grid"]):
        add_car(s, folder, layout, (x, z), -90, far=700, name=name, rival=dict(colour=colour, skill=skill, line=offset))
    # The race: the start lights, the order, the results (RaceDirector); "mode" and "laps" in the settings.
    # Not called "Race": that is the title screen's button, which GameMenu looks for by name.
    s.entity("Race Director").component("RaceDirector", lightsEvery=1.0, drivers="ROSSI,BIANCHI,FERRARI,ESPOSITO")
    add_race_interface(s, ui)
    # The weather: sun, clouds or rain, as the settings say; the rain falls around the camera.
    s.entity("Weather").component("Weather", weather=0, changeEvery=90)
    add_menus(s)
    write(folder + "/Assets/Scenes/Main.scene", s.text())
    # The game in Italian: each text as the game writes it, and as it is shown in Italian.
    italian = {
        "RACE": "GARA", "SETTINGS": "IMPOSTAZIONI", "RESUME": "RIPRENDI", "START OVER": "RICOMINCIA", "BACK ON THE WHEELS": "RIMETTI SULLE RUOTE",
        "MAIN MENU": "MENU PRINCIPALE", "BACK": "INDIETRO", "RACE AGAIN": "RIFAI LA GARA", "PAUSED": "PAUSA", "RESULTS": "RISULTATI",
        "A FIAT 500 ON A LITTLE CIRCUIT": "UNA FIAT 500 SU UN PICCOLO CIRCUITO", "BEST LAP   {bestLap:time}": "GIRO MIGLIORE   {bestLap:time}",
        "LAP {lap}": "GIRO {lap}", "BEST": "MIGLIORE", "LAP {raceLap} / {raceLaps}": "GIRO {raceLap} / {raceLaps}", "OF {cars}": "SU {cars}",
        "LAP INVALID  -  TRACK LIMITS": "GIRO NON VALIDO  -  LIMITI DELLA PISTA", "{tyreWear}% WORN": "USURA {tyreWear}%",
        "PIT LANE   60 KM/H": "CORSIA BOX   60 KM/H", "STOP IN YOUR BOX FOR NEW TYRES": "FERMATI NELLA TUA PIAZZOLA PER LE GOMME NUOVE",
        "CHANGING TYRES  1": "CAMBIO GOMME  1", "CHANGING TYRES  2": "CAMBIO GOMME  2", "CHANGING TYRES  3": "CAMBIO GOMME  3", "CHANGING TYRES  4": "CAMBIO GOMME  4",
        "SOFT": "MORBIDE", "MEDIUM": "MEDIE", "HARD": "DURE", "WET": "BAGNATO", "SUN": "SOLE", "CLOUDS": "NUVOLE", "RAIN": "PIOGGIA", "CHANGING": "VARIABILE",
        "LANGUAGE": "LINGUA", "TIME TRIAL": "GIRO A TEMPO", "LAPS": "GIRI", "GRAPHICS": "GRAFICA", "LOW": "BASSA", "HIGH": "ALTA", "WEATHER": "METEO",
        "TYRES AT THE START": "GOMME ALLA PARTENZA", "TOUCH BUTTONS": "TASTI TOUCH", "SMALL": "PICCOLI", "LARGE": "GRANDI", "SOUND": "AUDIO",
        "OFF": "NO", "ON": "SI", "HALF": "MEZZO", "FULL": "PIENO", "ABS OFF": "ABS SPENTO", "GAS": "GAS", "BRAKE": "FRENO",
        "W A S D  drive      Space  handbrake: drift": "W A S D  guida      Spazio  freno a mano: derapata",
        "B  ABS      L  lamps      R  back on the wheels      T  touch controls      Esc  pause": "B  ABS      L  luci      R  rimetti sulle ruote      T  comandi touch      Esc  pausa",
    }
    lines = ["# Driving in Italian: \"as the game writes it\" = \"as it is shown\". Chosen in Settings, or by the system's language."]
    for written, shown in italian.items():
        lines.append('"%s" = "%s"' % (written, shown))
    write(folder + "/Assets/Localization/it.txt", "\n".join(lines) + "\n")

def add_menus(s):
    """The title, the pause and the settings, as interface pieces in groups that GameMenu shows and hides."""
    s.entity("Game").component("GameMenu", showsTitle=True)
    dim, orange = (0.02, 0.022, 0.03, 0.62), (1.0, 0.5, 0.18, 0.95)
    def group(name):
        # Off in the scene: the race's interface is what is laid out in the Game view; GameMenu
        # switches these on when the game opens them. To lay one out, switch it on in the Inspector.
        s.entity(name).inactive()
        return s.index()
    def piece(parent, name, component, **fields):
        s.entity(name).under(parent)
        s.component(component, **fields)
    def button(parent, name, label, y, color=(1, 1, 1, 0.9), width=320):
        piece(parent, name, "UIButton", label=label, anchor=(0.5, 0.5), offset=(0, y), size=(width, 54), color=color, textColor=(0.05, 0.05, 0.06, 1))

    title = group("Title Screen")
    piece(title, "Title Backdrop", "UIPanel", anchor=(0.5, 0.5), offset=(0, 0), size=(5000, 5000), color=(0.02, 0.022, 0.03, 0.45))
    piece(title, "Title", "HudText", text="DRIVING", anchor=(0.5, 0.5), offset=(0, -230), size=76, color=(1, 1, 1, 1))
    piece(title, "Title Line", "HudText", text="A FIAT 500 ON A LITTLE CIRCUIT", anchor=(0.5, 0.5), offset=(0, -130), size=16, color=(1.0, 0.6, 0.3, 1))
    piece(title, "Title Best", "HudText", text="BEST LAP   {bestLap:time}", anchor=(0.5, 0.5), offset=(0, -92), size=16, color=(1, 1, 1, 0.75))
    button(title, "Race", "RACE", 0, orange)
    button(title, "Title Settings", "SETTINGS", 72)

    pause = group("Pause Screen")
    piece(pause, "Pause Backdrop", "UIPanel", anchor=(0.5, 0.5), offset=(0, 0), size=(5000, 5000), color=dim)
    piece(pause, "Paused", "HudText", text="PAUSED", anchor=(0.5, 0.5), offset=(0, -230), size=44, color=(1, 1, 1, 1))
    for k, (name, label) in enumerate((("Resume", "RESUME"), ("Restart", "START OVER"), ("Reset Car", "BACK ON THE WHEELS"),
                                       ("Pause Settings", "SETTINGS"), ("Main Menu", "MAIN MENU"))):
        button(pause, name, label, -130 + k * 70, orange if k == 0 else (1, 1, 1, 0.9))

    settings = group("Settings Screen")
    piece(settings, "Settings Backdrop", "UIPanel", anchor=(0.5, 0.5), offset=(0, 0), size=(5000, 5000), color=(0.02, 0.022, 0.03, 0.8))
    piece(settings, "Settings Title", "HudText", text="SETTINGS", anchor=(0.5, 0.5), offset=(0, -300), size=34, color=(1, 1, 1, 1))
    rows = (("LANGUAGE", "language", ("ENGLISH", "ITALIANO")),
            ("RACE", "mode", ("RACE", "TIME TRIAL")),
            ("LAPS", "laps", ("3", "5", "10")),
            ("GRAPHICS", "quality", ("LOW", "MEDIUM", "HIGH")),
            ("WEATHER", "weather", ("SUN", "CLOUDS", "RAIN", "CHANGING")),
            ("TYRES AT THE START", "startCompound", ("SOFT", "MEDIUM", "HARD", "WET")),
            ("TOUCH BUTTONS", "controls", ("SMALL", "MEDIUM", "LARGE")),
            ("ABS", "abs", ("OFF", "ON")),
            ("SOUND", "volume", ("OFF", "HALF", "FULL")))
    for r, (label, key, choices) in enumerate(rows):
        y = -232 + r * 54
        piece(settings, "Setting %s" % label.title(), "HudText", text=label, anchor=(0.5, 0.5), offset=(-400, y - 9), size=14, color=(1, 1, 1, 0.7))
        for c, choice in enumerate(choices):
            # The value kept is the button's place in the row, or for laps the number itself.
            value = int(choice) if key == "laps" else c
            piece(settings, "Option %s %d" % (key, value), "UIButton", label=choice, anchor=(0.5, 0.5), offset=(-200 + c * 140, y), size=(128, 48),
                  color=orange, textColor=(0.05, 0.05, 0.06, 1))
    button(settings, "Settings Back", "BACK", 262)

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
# ---------------------------------------------------------------- Showcase
SHOWCASE_SKY = ["skyZenith 0.05 0.16 0.46", "skyHorizon 0.62 0.68 0.76", "skyGround 0.13 0.13 0.14", "skyIntensity 1", "environmentIntensity 1",
                "shadows 1 130", "rayTracedShadows 1 4", "rayTracedLighting 1 4 1 1 1", "ambientOcclusion 1 0.7 1", "bloom 1 0.09 1",
                "exposure 1", "toneMapper 0", "vignette 0.22", "clouds 0.42 1", "volumetrics 1 12", "fog 0.72 0.76 0.82 0.0011",
                "fogShape 0.02 4", "wind 3 0 1.2 0.4"]

STATIONS = [  # name, what it shows, where (x, z)
    ("Materials", "Metal and roughness, glass, glow, textures", (0, -48)),
    ("Shaders", "Surface shaders written by hand", (36, -36)),
    ("Lights", "Spot and point lights, flicker, bloom", (52, 0)),
    ("Effects", "Nine particle presets in the wind", (36, 36)),
    ("Physics", "Stacks, dominoes, joints, hinges, spawners", (0, 52)),
    ("Jumps", "Mesh colliders, shaped as you like", (-36, 36)),
    ("Water", "Waves from an FFT ocean, things that float", (-52, 0)),
    ("Liquids", "Particle liquids, water and honey", (-36, -36)),
]

LAVA_SHADER = """// Lava: a crust that cracks over something molten, moving slowly.
// A surface shader: the engine lights what it returns.

properties {
    float speed = 0.35 range 0 3
    float scale = 1.6 range 0.2 8
    color glow = 4 1.1 0.15 1
    color crust = 0.05 0.035 0.03 1
}

Surface surface(Surface s, Input in, Props props) {
    float2 p = in.position.xz * props.scale;
    float t = in.time * props.speed;
    float w = sin(p.x * 1.7 + t) + sin(p.y * 2.3 - t * 1.3) + sin((p.x + p.y) * 1.1 + t * 0.7) + sin(length(p) * 2.0 - t * 1.7);
    float heat = pow(saturate(0.5 + 0.22 * w), 3.0);
    s.color = mix(props.crust.rgb, props.glow.rgb * 0.2, heat);
    s.emissive = props.glow.rgb * heat * 1.5;
    s.roughness = mix(0.9, 0.35, heat);
    s.metallic = 0.0;
    return s;
}
"""

HOLOGRAM_SHADER = """// Hologram: bands of light that climb the object, and a rim that glows.

properties {
    float speed = 1.2 range 0 10
    float stripes = 9 range 1 40
    color glow = 0.2 1.4 2.6 1
}

Surface surface(Surface s, Input in, Props props) {
    float band = pow(0.5 + 0.5 * sin(in.position.y * props.stripes - in.time * props.speed * 3.0), 6.0);
    float rim = pow(1.0 - saturate(dot(normalize(in.normal), normalize(in.view))), 2.5);
    s.color = s.color * 0.2;
    s.emissive += props.glow.rgb * (band * 1.6 + rim * 1.2 + 0.08);
    s.roughness = 0.2;
    return s;
}
"""

def custom_material(folder, name, shader, values, color=(1, 1, 1, 1)):
    """A material drawn by one of the project's own shaders, with values for its properties."""
    lines = ["molten-material 1", "shader lit", "baseColor " + n(*color), "metallic 0", "roughness 0.5", "emissive 0 0 0",
             "surfaceShader " + q(shader)]
    for key, value in values.items():
        lines.append("shaderValue %s %s" % (key, n(*value)))
    path = "Materials/%s.material" % name
    write(folder + "/Assets/" + path, "\n".join(lines) + "\n")
    return path

def look_at(direction):
    """The rotation that points an entity's -z (where a light or a camera faces) along `direction`."""
    dx, dy, dz = direction
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    dx, dy, dz = dx / length, dy / length, dz / length
    return then(yaw(math.degrees(math.atan2(-dx, -dz))), pitch(math.degrees(math.asin(dy))))

def profile_model(path, profile, width, step=0.5):
    """A ramp, a bump or a table: a height for each distance along x (going toward -x), `width` across z.
    Its top and its sides, as one model: what is drawn is what the car drives on."""
    import make_models
    mesh = make_models.Mesh()
    length = profile[-1][0]
    xs = [k * step for k in range(int(length / step) + 1)]
    if xs[-1] < length: xs.append(length)
    def height(x):
        for (x0, h0), (x1, h1) in zip(profile, profile[1:]):
            if x0 <= x <= x1:
                u = 0 if x1 == x0 else (x - x0) / (x1 - x0)
                u = u * u * (3 - 2 * u)                              # eased: a smooth ride over each change
                return h0 + (h1 - h0) * u
        return 0
    half = width / 2
    for a, b in zip(xs, xs[1:]):
        ha, hb = height(a), height(b)
        mesh.quad((-a, ha, -half), (-a, ha, half), (-b, hb, half), (-b, hb, -half), (0, 1, 0))
        for side in (-half, half):
            if ha > 0.001 or hb > 0.001:
                mesh.quad((-a, 0, side), (-b, 0, side), (-b, hb, side), (-a, ha, side), (0, 0, side))
    for x in (0, length):
        h = height(x)
        if h > 0.001:
            mesh.quad((-x, 0, -half), (-x, 0, half), (-x, h, half), (-x, h, -half), (1 if x == 0 else -1, 0, 0))
    mesh.write(path)

def showcase():
    import json, shutil, make_models
    folder = "Showcase"
    source = os.path.join(models("Driving"), "Fiat500")
    if not os.path.exists(os.path.join(source, "layout.json")):
        print("Showcase: it borrows the Driving project's car, which is not there yet")
        return
    fiat = os.path.join(models(folder), "Fiat500")
    if os.path.exists(fiat): shutil.rmtree(fiat)
    shutil.copytree(source, fiat)
    layout = json.load(open(os.path.join(fiat, "layout.json")))
    project(folder, "Showcase", "A plaza of everything Molten does, to drive around in a Fiat 500: materials, hand-written shaders, lights, "
            "particle effects, physics and joints, mesh colliders, ocean waves, liquids, a visual script, an interface for touch screens.",
            scripts=("DriveControls.cpp", "Motor.hpp", "Motor.cpp", "Follow.cpp", "CarLights.cpp", "Exhaust.cpp", "TyreSmoke.cpp", "SkidMarks.cpp", "Stations.cpp"))
    write(folder + "/CREDITS.txt", "The car: " + layout["credit"] + "\nMade smaller and rearranged by Tools/import_fiat500.py.\n")
    sounds(folder, ("i4",))
    write(folder + "/Assets/Shaders/Lava.shader", LAVA_SHADER)
    write(folder + "/Assets/Shaders/Hologram.shader", HOLOGRAM_SHADER)
    write(folder + "/Assets/Scripts/Star.graph", "\n".join([
        "molten-graph 1",
        'node 1 "OnUpdate" 60 60',
        'node 2 "Rotate" 320 60', '  value "axis" "0 1 0"', '  value "degrees" "120"',
        'node 3 "Branch" 580 60',
        'node 4 "Less" 320 260', '  value "b" "2.6"',
        'node 5 "Distance" 60 260',
        'node 6 "GetPosition" 60 420',
        'node 7 "PositionOf" 60 520', '  value "name" "Car"',
        'node 8 "AddValue" 840 60', '  value "name" "stars"', '  value "amount" "1"',
        'node 9 "DestroySelf" 1100 60',
        'link 1 "then" 2 "in"', 'link 2 "then" 3 "in"',
        'link 7 "position" 5 "a"', 'link 6 "position" 5 "b"', 'link 5 "distance" 4 "a"', 'link 4 "result" 3 "condition"',
        'link 3 "true" 8 "in"', 'link 8 "then" 9 "in"', ""]))
    stations = {name: at for name, _, at in STATIONS}
    def plaza_clear(x, z): return x * x + z * z < 82 ** 2
    make_models.trees(models(folder) + "/trees.obj", 260, 0, 190, plaza_clear, seed=11)
    make_models.hills(models(folder) + "/hills.obj", 1600, 260, seed=8)

    s = Scene(folder, SHOWCASE_SKY)
    grass = s.material("Grass", (0.13, 0.21, 0.08, 1), 0, 0.95)
    paving = s.material("Paving", (0.33, 0.32, 0.31, 1), 0, 0.8)
    pad = s.material("Station Pad", (0.24, 0.235, 0.23, 1), 0, 0.85)
    stone = s.material("Stone", (0.55, 0.53, 0.5, 1), 0, 0.75)
    dark = s.material("Dark Steel", (0.05, 0.055, 0.065, 1), 0.8, 0.4)
    chrome = s.material("Chrome", (0.95, 0.95, 0.96, 1), 1.0, 0.04)
    wood = s.material("Wood", (0.42, 0.26, 0.14, 1), 0, 0.7)
    crate = s.material("Crate", (0.62, 0.42, 0.22, 1), 0, 0.75)
    white = s.material("White", (0.86, 0.86, 0.85, 1), 0, 0.5)
    hill = s.material("Hills", (0.17, 0.21, 0.12, 1), 0, 0.95)
    bark = s.material("Bark", (0.17, 0.11, 0.07, 1), 0, 0.95)
    leaves = s.material("Leaves", (0.08, 0.18, 0.06, 1), 0, 0.9)
    star = s.material("Star", (0.4, 0.8, 1.0, 1), 0.2, 0.2, (0.6, 1.6, 2.6))

    s.entity("Sun", (0, 40, 0), then(yaw(140), pitch(-38))).light("directional", (1.0, 0.9, 0.78), 5.2, 10, True)
    # Grass to the hills, solid at y = 0; the plaza laid on it.
    # (Surfaces that lie on one another flicker: the grass is drawn well below the plaza, the pads well above it.)
    s.entity("Ground", (0, -0.06, 0), scale=(1000, 1, 1000)).mesh("plane", grass).box((500, 0.5, 500), (0, -0.44, 0))
    s.entity("Hills", (0, -2, 0)).mesh("Models/hills.obj", hill, False)
    s.entity("Plaza", (0, 0, 0), scale=(170, 1, 170)).mesh("plane", paving, False)
    s.entity("Trees")
    trees = s.index()
    s.entity("Trunks").mesh("Models/trees_trunks.obj", bark).under(trees)
    s.entity("Leaves").mesh("Models/trees_leaves.obj", leaves).under(trees)

    # The stations: a darker round of paving each, and a marker the interface names.
    s.entity("Stations")
    markers = s.index()
    for number, (name, _, (x, z)) in enumerate(STATIONS):
        s.entity("Station %d" % (number + 1), (x, 0, z)).under(markers)
        s.entity(name + " Pad", (x, 0.0125, z), scale=(30, 0.025, 30)).mesh("cylinder", pad, False).under(markers)

    # ---------------- the middle: a chrome ball that mirrors everything, in a fountain
    s.entity("Centerpiece")
    middle = s.index()
    s.entity("Basin", (0, 0.3, 0), scale=(9, 0.6, 9)).mesh("cylinder", stone).box((4.2, 0.3, 4.2)).under(middle)
    s.entity("Basin Water", (0, 0.58, 0), scale=(8.2, 0.04, 8.2)).mesh("cylinder", s.material("Pool", (0.05, 0.2, 0.26, 1), 0, 0.05)).under(middle)
    s.entity("Plinth", (0, 1.2, 0), scale=(1.6, 1.2, 1.6)).mesh("cylinder", stone).under(middle)
    s.entity("Mirror Ball", (0, 3.3, 0), scale=(3.0, 3.0, 3.0)).mesh("sphere", chrome).sphere(1.5).under(middle)
    s.entity("Fountain", (0, 4.85, 0)).under(middle)
    effect(s, rate=260, lifetimeMin=1.4, lifetimeMax=1.9, speedMin=5.5, speedMax=7, direction=(0, 1, 0), spread=math.radians(16), spawnRadius=0.1,
           gravity=(0, -9.8, 0), drag=0.1, bounces=True, bounciness=0.15, floorHeight=0.6, isAdditive=False,
           colorStart=(0.75, 0.88, 1.0, 0.7), colorEnd=(0.9, 0.95, 1.0, 0), sizeStart=0.06, sizeEnd=0.12, maxParticles=900, windInfluence=0.3)

    # ---------------- the stars: eight to collect, each turned and picked up by a visual script
    s.entity("Stars")
    stars = s.index()
    for k in range(8):
        angle = math.radians(22.5 + 45 * k)
        s.entity("Star %d" % (k + 1), (math.sin(angle) * 30, 1.3, math.cos(angle) * 30), then(yaw(45 * k), (math.sin(math.radians(27)), 0, 0, math.cos(math.radians(27)))),
                 (0.8, 0.8, 0.8)).mesh("cube", star).under(stars)
        s.component("VisualScript", graph="Scripts/Star.graph")

    # ---------------- 1. Materials: two rows that go from smooth to rough, and a row of special cases
    cx, cz = stations["Materials"]
    s.entity("Materials")
    group = s.index()
    for step in range(3):
        height = 0.45 + step * 0.55
        s.entity("Step %d" % (step + 1), (cx, height / 2, cz - 1.0 - step * 1.7), scale=(10.5, height, 1.7)).mesh("cube", stone).box((5.25, height / 2, 0.85)).under(group)
    rows = [("Plastic", (0.75, 0.08, 0.06, 1), 0.0), ("Gold", (1.0, 0.77, 0.33, 1), 1.0)]
    for row, (kind, color, metal) in enumerate(rows):
        for k in range(6):
            rough = k / 5
            name = "%s %d" % (kind, round(rough * 100))
            material = s.material(name, color, metal, max(rough, 0.03))
            height = 0.45 + row * 0.55
            s.entity(name, (cx - 3.75 + k * 1.5, height + 0.55, cz - 1.0 - row * 1.7), scale=(1.1, 1.1, 1.1)).mesh("sphere", material).under(group)
    fiat_paint = "Models/Fiat500/Textures/Body_Paint_"
    specials = [
        ("Mirror", chrome),
        ("Glass", material_file(folder, "Glass", (0.85, 0.92, 1.0, 0.25), 0.0, 0.03, blend=True)),
        ("Glow", s.material("Glow", (1.0, 0.45, 0.12, 1), 0, 0.4, (6.0, 2.2, 0.5))),
        ("Car Paint", material_file(folder, "Car Paint", (1, 1, 1, 1), 1.0, 1.0, textures={"baseColorTexture": fiat_paint + "baseColor.jpg",
                                    "metallicRoughnessTexture": fiat_paint + "metallicRoughness.jpg", "normalTexture": fiat_paint + "normal.jpg"})),
        ("Lava", custom_material(folder, "Lava", "Shaders/Lava.shader", {"speed": (0.35, 0, 0, 0), "scale": (3.0, 0, 0, 0)})),
        ("Hologram", custom_material(folder, "Hologram", "Shaders/Hologram.shader", {"speed": (1.2, 0, 0, 0), "stripes": (14, 0, 0, 0)})),
    ]
    for k, (name, material) in enumerate(specials):
        s.entity(name + " Ball", (cx - 3.75 + k * 1.5, 1.55 + 0.55, cz - 4.4), scale=(1.1, 1.1, 1.1)).mesh("sphere", material).under(group)

    # ---------------- 2. Shaders: a lake of lava, a hologram on a column
    cx, cz = stations["Shaders"]
    lava = custom_material(folder, "Lava Lake", "Shaders/Lava.shader", {"speed": (0.3, 0, 0, 0), "scale": (0.9, 0, 0, 0)})
    hologram = custom_material(folder, "Hologram Statue", "Shaders/Hologram.shader", {"speed": (1.0, 0, 0, 0), "stripes": (7, 0, 0, 0)})
    s.entity("Shaders")
    group = s.index()
    s.entity("Lava Rim", (cx - 3, 0.2, cz), scale=(12, 0.4, 12)).mesh("cylinder", s.material("Basalt", (0.06, 0.055, 0.055, 1), 0, 0.9)).box((5.6, 0.2, 5.6)).under(group)
    s.entity("Lava Lake", (cx - 3, 0.42, cz), scale=(10.6, 0.04, 10.6)).mesh("cylinder", lava, False).under(group)
    s.entity("Lava Glow", (cx - 3, 2.2, cz)).light("point", (1.0, 0.42, 0.12), 26, 14).under(group)
    s.component("Flicker", amount=0.25, speed=3)
    s.entity("Embers", (cx - 3, 0.5, cz)).under(group)
    effect(s, rate=40, lifetimeMin=1.5, lifetimeMax=3.0, speedMin=0.5, speedMax=1.5, direction=(0, 1, 0), spread=math.radians(30), spawnRadius=4.0,
           gravity=(0, 0.8, 0), drag=0.6, turbulence=0.9, turbulenceSize=1.0, windInfluence=0.6, colorStart=(5.0, 1.6, 0.3, 1),
           colorEnd=(1.5, 0.2, 0.05, 0), sizeStart=0.05, sizeEnd=0.01, maxParticles=200)
    s.entity("Column", (cx + 7, 1.5, cz + 2), scale=(1.2, 3.0, 1.2)).mesh("cylinder", stone).box((0.6, 1.5, 0.6)).under(group)
    s.entity("Hologram", (cx + 7, 4.4, cz + 2), scale=(1.8, 2.6, 1.8)).mesh("sphere", hologram).under(group)
    s.component("Spin", axis=(0, 1, 0), degreesPerSecond=25)

    # ---------------- 3. Lights: a hall, dark inside, open toward the middle of the plaza
    cx, cz = stations["Lights"]
    s.entity("Lights")
    group = s.index()
    hall_floor = s.material("Hall Floor", (0.75, 0.74, 0.72, 1), 0, 0.35)
    hall_wall = s.material("Hall Wall", (0.2, 0.2, 0.22, 1), 0, 0.8)
    def wall(name, center, size, material=hall_wall, solid=True):
        s.entity(name, center, scale=size).mesh("cube", material).under(group)
        if solid: s.box((size[0] / 2, size[1] / 2, size[2] / 2))
    x0 = cx - 6                                         # the open side
    wall("Hall Floor", (x0 + 7, 0.03, cz), (14, 0.06, 16), hall_floor, False)
    wall("Back Wall", (x0 + 14, 3.2, cz), (0.4, 6.4, 16.8))
    wall("Left Wall", (x0 + 7, 3.2, cz - 8.2), (14.4, 6.4, 0.4))
    wall("Right Wall", (x0 + 7, 3.2, cz + 8.2), (14.4, 6.4, 0.4))
    wall("Roof", (x0 + 7, 6.6, cz), (14.8, 0.4, 17.2))
    wall("Lintel", (x0, 5.6, cz), (0.4, 1.6, 16.8))
    focus = (x0 + 8.5, 1.9, cz)
    wall("Statue Plinth", (focus[0], 0.6, focus[2]), (1.6, 1.2, 1.6), stone)
    s.entity("Statue", focus, scale=(1.5, 1.5, 1.5)).mesh("sphere", white).under(group)
    for name, color, at in (("Red Spot", (1.0, 0.12, 0.08), (x0 + 5.5, 5.9, cz - 5)), ("Green Spot", (0.15, 1.0, 0.25), (x0 + 12, 5.9, cz - 4.5)),
                            ("Blue Spot", (0.2, 0.35, 1.0), (x0 + 9, 5.9, cz + 5.5))):
        s.entity(name, at, look_at((focus[0] - at[0], focus[1] - at[1], focus[2] - at[2]))).light("spot", color, 70, 16).under(group)
    neon = [("Neon Cyan", (0.1, 2.4, 3.2), 4.6), ("Neon Magenta", (3.0, 0.2, 2.2), 3.6), ("Neon Amber", (3.4, 1.6, 0.2), 2.6)]
    for name, glow, height in neon:
        wall(name, (x0 + 13.75, height, cz), (0.1, 0.12, 12), s.material(name, (0.1, 0.1, 0.1, 1), 0, 0.3, glow), False)
        s.entity(name + " Light", (x0 + 13.2, height, cz)).light("point", tuple(min(c / 3.4, 1) for c in glow), 6, 7).under(group)
    for side in (-1, 1):
        at = (x0 + 3.2, 0, cz + side * 5.6)
        wall("Brazier", (at[0], 0.5, at[2]), (0.9, 1.0, 0.9), dark)
        s.entity("Brazier Fire", (at[0], 1.05, at[2])).under(group)
        effect(s, rate=70, lifetimeMin=0.5, lifetimeMax=1.1, speedMin=0.8, speedMax=1.8, spread=math.radians(14), spawnRadius=0.18, gravity=(0, 1.2, 0),
               drag=0.8, turbulence=0.7, turbulenceSize=0.5, hasMiddle=True, middleAt=0.35, colorStart=(4.0, 2.6, 0.7, 1),
               colorMiddle=(3.0, 0.9, 0.12, 0.9), colorEnd=(0.4, 0.08, 0.02, 0), sizeStart=0.16, sizeMiddle=0.34, sizeEnd=0.08, maxParticles=400)
        s.entity("Brazier Light", (at[0], 1.9, at[2])).light("point", (1.0, 0.55, 0.2), 12, 9).under(group)
        s.component("Flicker", amount=0.35, speed=9)

    # ---------------- 4. Effects: every preset on a pedestal of its own
    cx, cz = stations["Effects"]
    s.entity("Effects")
    group = s.index()
    presets = {
        "Fire": dict(rate=70, lifetimeMin=0.5, lifetimeMax=1.1, speedMin=0.8, speedMax=1.8, spread=math.radians(14), spawnRadius=0.18, gravity=(0, 1.2, 0),
                     drag=0.8, turbulence=0.7, turbulenceSize=0.5, hasMiddle=True, middleAt=0.35, colorStart=(4.0, 2.6, 0.7, 1), colorMiddle=(3.0, 0.9, 0.12, 0.9),
                     colorEnd=(0.4, 0.08, 0.02, 0), sizeStart=0.16, sizeMiddle=0.34, sizeEnd=0.08, maxParticles=400),
        "Smoke": dict(rate=14, lifetimeMin=2.5, lifetimeMax=4.5, speedMin=0.5, speedMax=1.0, spread=math.radians(12), spawnRadius=0.15, gravity=(0, 0.35, 0),
                      drag=0.5, turbulence=0.5, turbulenceSize=1.2, windInfluence=0.8, isAdditive=False, hasMiddle=True, middleAt=0.2,
                      colorStart=(0.35, 0.35, 0.36, 0), colorMiddle=(0.3, 0.3, 0.31, 0.55), colorEnd=(0.5, 0.5, 0.5, 0), sizeStart=0.25, sizeMiddle=0.8,
                      sizeEnd=2.2, maxParticles=200),
        "Sparks": dict(rate=60, lifetimeMin=0.4, lifetimeMax=1.0, speedMin=3, speedMax=7, spread=math.radians(40), gravity=(0, -9.8, 0), drag=0.3,
                       bounces=True, bounciness=0.35, floorHeight=1.2, colorStart=(5.0, 3.2, 1.0, 1), colorEnd=(2.0, 0.4, 0.05, 0), sizeStart=0.035, sizeEnd=0.01),
        "Explosion": dict(rate=0, burstAtStart=220, lifetimeMin=0.5, lifetimeMax=1.4, speedMin=2, speedMax=11, spread=math.radians(180), spawnRadius=0.2,
                          gravity=(0, -1.5, 0), drag=2.6, turbulence=1.0, duration=0.1, loops=True, hasMiddle=True, middleAt=0.25,
                          colorStart=(6.0, 4.5, 2.0, 1), colorMiddle=(3.0, 0.8, 0.1, 0.9), colorEnd=(0.15, 0.1, 0.1, 0), sizeStart=0.25, sizeMiddle=0.7,
                          sizeEnd=1.1, maxParticles=400),
        "Dust": dict(rate=0, burstAtStart=40, lifetimeMin=0.7, lifetimeMax=1.6, speedMin=0.5, speedMax=2.2, spread=math.radians(75), spawnRadius=0.25,
                     gravity=(0, 0.2, 0), drag=2.4, windInfluence=0.5, isAdditive=False, duration=0.1, loops=True, colorStart=(0.55, 0.48, 0.38, 0.5),
                     colorEnd=(0.6, 0.55, 0.45, 0), sizeStart=0.2, sizeEnd=0.9),
        "Rain": dict(rate=180, lifetimeMin=0.35, lifetimeMax=0.45, speedMin=14, speedMax=17, direction=(0, -1, 0), spread=math.radians(2), shape=1,
                     boxSize=(3, 0.2, 3), gravity=(0, -4, 0), drag=0, windInfluence=0.3, isAdditive=False, colorStart=(0.7, 0.78, 0.9, 0.5),
                     colorEnd=(0.7, 0.78, 0.9, 0.3), sizeStart=0.035, sizeEnd=0.03, maxParticles=400),
        "Snow": dict(rate=40, lifetimeMin=4, lifetimeMax=5, speedMin=0.6, speedMax=1.2, direction=(0, -1, 0), spread=math.radians(10), shape=1,
                     boxSize=(3, 0.2, 3), gravity=(0, -0.2, 0), drag=0.6, turbulence=0.6, turbulenceSize=2, windInfluence=0.7, isAdditive=False,
                     colorStart=(1, 1, 1, 0.9), colorEnd=(1, 1, 1, 0.6), sizeStart=0.05, sizeEnd=0.05, maxParticles=300),
        "Magic": dict(rate=45, lifetimeMin=1, lifetimeMax=2, speedMin=0.2, speedMax=0.7, spread=math.radians(180), shape=2, spawnRadius=0.7,
                      gravity=(0, 0.5, 0), drag=0.4, turbulence=0.8, turbulenceSize=0.6, hasMiddle=True, middleAt=0.5, colorStart=(0.6, 1.2, 4.0, 0),
                      colorMiddle=(2.2, 0.8, 4.0, 1), colorEnd=(4.0, 0.5, 2.0, 0), sizeStart=0.02, sizeMiddle=0.07, sizeEnd=0.0),
        "Fountain": dict(rate=220, lifetimeMin=1.6, lifetimeMax=2.2, speedMin=6, speedMax=7.5, spread=math.radians(9), gravity=(0, -9.8, 0), drag=0.1,
                         bounces=True, bounciness=0.2, floorHeight=1.2, isAdditive=False, colorStart=(0.75, 0.88, 1.0, 0.7), colorEnd=(0.9, 0.95, 1.0, 0),
                         sizeStart=0.06, sizeEnd=0.12, maxParticles=800),
    }
    for k, (name, settings) in enumerate(presets.items()):
        x, z = cx + (k % 3 - 1) * 6, cz + (k // 3 - 1) * 6
        s.entity(name + " Pedestal", (x, 0.6, z), scale=(1.4, 1.2, 1.4)).mesh("cube", stone).box((0.7, 0.6, 0.7)).under(group)
        falls = name in ("Rain", "Snow")
        s.entity(name, (x, 6.5 if falls else 1.3, z)).under(group)
        effect(s, **settings)
        if falls:
            s.entity(name + " Cloud", (x, 6.8, z), scale=(3.4, 0.3, 3.4)).mesh("cube", s.material("Cloud", (0.85, 0.87, 0.9, 1), 0, 0.9)).under(group)

    # ---------------- 5. Physics: things to knock over, swing and break
    cx, cz = stations["Physics"]
    s.entity("Physics")
    group = s.index()
    def thing(name, at, size, material, mass, shape="box", turn=(0, 0, 0, 1)):
        mesh = {"box": "cube", "sphere": "sphere", "capsule": "cylinder"}[shape]
        s.entity(name, at, turn, size).mesh(mesh, material).under(group)
        if shape == "box": s.box((size[0] / 2, size[1] / 2, size[2] / 2))
        elif shape == "sphere": s.sphere(size[0] / 2)
        else: s.capsule(size[0] / 2, size[1] / 2 - size[0] / 2)
        s.body("dynamic", mass, 0.15, 0.6, 0.05, 1, rotates=True)
    # A pyramid of crates.
    for layer, count in enumerate((4, 3, 2, 1)):
        for k in range(count):
            thing("Crate", (cx - 12 + (k - (count - 1) / 2) * 1.02, 0.5 + layer * 1.0, cz + 2), (1, 1, 1), crate, 25)
    # Dominoes on a curve; the first leans on the second.
    domino = s.material("Domino", (0.08, 0.08, 0.09, 1), 0, 0.35)
    for k in range(26):
        angle = math.radians(200 + k * 5.6)
        x, z = cx + 2 + math.cos(angle) * 8, cz + 4 + math.sin(angle) * 8
        facing = yaw(-math.degrees(angle) - 90)            # thin side along the curve
        if k == 0: facing = then(facing, (0, 0, math.sin(math.radians(-9)), math.cos(math.radians(-9))))
        thing("Domino %d" % (k + 1), (x, 0.72, z), (0.22, 1.4, 0.7), domino, 4, turn=facing)
    # Five balls hung in a row: the first swings into the rest.
    # (The posts stand well beyond where the first ball is lifted to: it must swing free.)
    s.entity("Cradle Beam", (cx + 13, 4.4, cz - 6), scale=(8.8, 0.2, 0.2)).mesh("cube", dark).under(group)
    for side in (-1, 1):
        s.entity("Cradle Post", (cx + 13 + side * 4.3, 2.2, cz - 6), scale=(0.2, 4.4, 0.2)).mesh("cube", dark).box((0.1, 2.2, 0.1)).under(group)
    for k in range(5):
        pivot = (cx + 11.6 + k * 0.7, 4.3, cz - 6)
        length = 2.6
        angle = math.radians(45 if k == 0 else 0)
        at = (pivot[0] - math.sin(angle) * length, pivot[1] - math.cos(angle) * length, pivot[2])
        s.entity("Cradle Ball %d" % (k + 1), at, scale=(0.7, 0.7, 0.7)).mesh("sphere", chrome).sphere(0.35).under(group)
        s.body("dynamic", 3, 0.97, 0.0, 0.0, 1, rotates=True, turning_damping=0.0)       # it turns as it swings: the joint is off its middle
        s.component("Joint", connectedTo="", kind="ball", anchor=(pivot[0] - at[0], pivot[1] - at[1], 0))
    # A wrecking ball on a gantry, let go into a wall of crates.
    pivot = (cx + 15, 10, cz + 10)
    for side in (-1, 1):
        s.entity("Gantry Leg", (pivot[0], 5, pivot[2] + side * 3), scale=(0.4, 10, 0.4)).mesh("cube", dark).box((0.2, 5, 0.2)).under(group)
    s.entity("Gantry Top", (pivot[0], 10.1, pivot[2]), scale=(0.4, 0.4, 6.4)).mesh("cube", dark).under(group)
    length, angle = 8.0, math.radians(62)
    at = (pivot[0] - math.sin(angle) * length, pivot[1] - math.cos(angle) * length, pivot[2])
    s.entity("Wrecking Ball", at, scale=(2.2, 2.2, 2.2)).mesh("sphere", dark).sphere(1.1).under(group)
    s.body("dynamic", 900, 0.1, 0.4, 0.0, 1, rotates=True)
    s.component("Joint", connectedTo="", kind="ball", anchor=(pivot[0] - at[0], pivot[1] - at[1], 0))
    for row in range(4):
        for k in range(3):
            thing("Wall Crate", (pivot[0] + 2.0, 0.5 + row, pivot[2] - 1.02 + k * 1.02), (1, 1, 1), crate, 30)
    # Two gates on hinges, to drive through.
    gate = s.material("Gate", (0.12, 0.3, 0.6, 1), 0.2, 0.5)
    for side in (-1, 1):
        hinge_at = (cx - 4 + side * 3.0, 1.25, cz - 12)
        center = (hinge_at[0] - side * 1.45, 1.25, hinge_at[2])
        s.entity("Gate Post", (hinge_at[0] + side * 0.15, 1.4, hinge_at[2]), scale=(0.2, 2.8, 0.2)).mesh("cube", dark).box((0.1, 1.4, 0.1)).under(group)
        s.entity("Gate", center, scale=(2.8, 2.3, 0.12)).mesh("cube", gate).box((1.4, 1.15, 0.06)).under(group)
        s.body("dynamic", 40, 0.1, 0.5, 0.3, 1, rotates=True, turning_damping=1.5)
        s.component("Joint", connectedTo="", kind="hinge", anchor=(side * 1.45, 0, 0), axis=(0, 1, 0))
    # Bowling: ten pins for the car.
    pin = s.material("Pin", (0.92, 0.92, 0.9, 1), 0, 0.3)
    for row in range(4):
        for k in range(row + 1):
            thing("Pin", (cx - 12 + (k - row / 2) * 0.62, 0.55, cz - 8 - row * 0.55), (0.3, 1.1, 0.3), pin, 2, shape="capsule")
    # A chute that never runs out of balls.
    s.entity("Chute", (cx + 4, 3.0, cz + 15), then(yaw(0), pitch(-22)), (2.4, 0.2, 7)).mesh("cube", wood).box((1.2, 0.1, 3.5)).under(group)
    s.entity("Ball Spawner", (cx + 4, 8, cz + 18)).under(group)
    s.component("Spawner", prefab="Prefabs/Ball.prefab", interval=0.9, radius=0.8, limit=0, firstDelay=1)
    b = Scene(folder)
    ball = b.material("Ball", (0.95, 0.3, 0.12, 1), 0.1, 0.35)
    b.entity("Ball", scale=(0.6, 0.6, 0.6)).mesh("sphere", ball).sphere(0.3).body("dynamic", 2, 0.55, 0.5, 0.05, 1, rotates=True)
    b.component("Lifetime", seconds=14)
    write(folder + "/Assets/Prefabs/Ball.prefab", b.text())

    # ---------------- 6. Jumps: shapes the car rides on, solid as they are drawn
    cx, cz = stations["Jumps"]
    s.entity("Jumps", (cx + 12, 0, cz))
    group = s.index()
    concrete = s.material("Ramp", (0.5, 0.48, 0.45, 1), 0, 0.8)
    stripe = s.material("Ramp Stripe", (0.9, 0.55, 0.1, 1), 0, 0.6)
    profile_model(models(folder) + "/table_top.obj", [(0, 0), (8, 1.7), (14, 1.7), (22, 0)], 7)
    profile_model(models(folder) + "/whoops.obj", [(0, 0), (2, 0.55), (4, 0), (6, 0.55), (8, 0), (10, 0.55), (12, 0), (14, 0.55), (16, 0)], 6)
    profile_model(models(folder) + "/kicker.obj", [(0, 0), (6, 1.3), (6.01, 0)], 5)
    for name, mesh, dz in (("Table Top", "table_top", -8), ("Whoops", "whoops", 0), ("Kicker", "kicker", 8)):
        s.entity(name, (0, 0, dz)).mesh("Models/%s.obj" % mesh, stripe if name == "Kicker" else concrete).mesh_collider("Models/%s.obj" % mesh).under(group)

    # ---------------- 7. Water: a basin of ocean, and things that float on it
    cx, cz = stations["Water"]
    s.entity("Water")
    group = s.index()
    level = 0.8
    for name, center, size in (("Basin North", (cx, 0.5, cz - 12), (24.8, 1.0, 0.8)), ("Basin South", (cx, 0.5, cz + 12), (24.8, 1.0, 0.8)),
                               ("Basin West", (cx - 12, 0.5, cz), (0.8, 1.0, 23.2)), ("Basin East", (cx + 12, 0.5, cz), (0.8, 1.0, 23.2))):
        s.entity(name, center, scale=size).mesh("cube", stone).box((size[0] / 2, size[1] / 2, size[2] / 2)).under(group)
    s.entity("Basin Floor", (cx, 0.012, cz), scale=(23.2, 1, 23.2)).mesh("plane", s.material("Basin Floor", (0.06, 0.16, 0.2, 1), 0, 0.6), False).under(group)
    s.entity("Sea", (cx, level, cz)).mesh("plane", s.material("Sea", (0.03, 0.2, 0.27, 1), 0, 0.05, water=(0.35, 1)), False).under(group)
    s.component("Water", size=23, resolution=96, windSpeed=5, patchSize=28, choppiness=1, waveHeight=0.6, foam=1)
    buoy = s.material("Buoy", (0.9, 0.25, 0.1, 1), 0, 0.4)
    for k in range(9):
        x, z = cx - 7 + (k % 3) * 7, cz - 7 + (k // 3) * 7
        is_ball = k % 2 == 1
        s.entity("Buoy" if is_ball else "Floating Crate", (x, level + 1.5 + k * 0.3, z), scale=(1.1, 1.1, 1.1) if is_ball else (1.2, 0.8, 1.2))
        s.mesh("sphere" if is_ball else "cube", buoy if is_ball else crate)
        if is_ball: s.sphere(0.55)
        else: s.box((0.6, 0.4, 0.6))
        s.body("dynamic", 30, 0.1, 0.5, 0.05, 1, rotates=True, turning_damping=0.6).under(group)
        s.component("Buoyancy", waterLevel=level, lift=2.2, depth=0.4, drag=2.5)

    # ---------------- 8. Liquids: two tanks, of water and of honey, filled as the game starts
    cx, cz = stations["Liquids"]
    s.entity("Liquids")
    group = s.index()
    glass = material_file(folder, "Tank Glass", (0.85, 0.95, 1.0, 0.18), 0.0, 0.03, blend=True)
    for name, x, color, viscosity, sticky in (("Water", cx - 3.5, (0.25, 0.55, 1.0, 0.85), 0.02, 0.1), ("Honey", cx + 3.5, (1.0, 0.6, 0.12, 0.9), 0.45, 0.6)):
        inside, height = 2.6, 1.8
        for wall_name, center, size in (("Front", (x, height / 2, cz + inside / 2 + 0.05), (inside + 0.2, height, 0.1)),
                                        ("Back", (x, height / 2, cz - inside / 2 - 0.05), (inside + 0.2, height, 0.1)),
                                        ("Left", (x - inside / 2 - 0.05, height / 2, cz), (0.1, height, inside)),
                                        ("Right", (x + inside / 2 + 0.05, height / 2, cz), (0.1, height, inside))):
            s.entity("%s Tank %s" % (name, wall_name), center, scale=size).mesh("cube", glass, False).box((size[0] / 2, size[1] / 2, size[2] / 2)).under(group)
        s.entity(name + " Tank Base", (x, 0.03, cz), scale=(inside + 0.4, 0.06, inside + 0.4)).mesh("cube", dark).under(group)
        s.entity(name + " Liquid", (x, 1.6, cz)).under(group)
        s.component("Liquid", fill=(2.2, 0.6, 2.2), pourRate=25, pourSpeed=1.5, pourWidth=0.1, maxDrops=1700, dropRadius=0.07, viscosity=viscosity, stickiness=sticky)
        effect(s, rate=0, isEmitting=False, isAdditive=False, colorStart=color, colorEnd=color, sizeStart=0.17, sizeEnd=0.17, maxParticles=1700)

    # ---------------- the car, and the interface around it
    s.entity("Start", (0, 0, 24))
    add_car(s, folder, layout, (0, 24), 0)
    # Which station the car is at, for the panel below.
    s.lines.insert(next(i for i, line in enumerate(s.lines) if line == '  component "DriveControls"') + 1, '  component "Stations"')
    s.entity("Showcase Interface")
    ui = s.index()
    def piece(name, component, **fields):
        s.entity(name).under(ui)
        s.component(component, **fields)
    glass_dark, rim = (0.04, 0.045, 0.055, 0.62), (1, 1, 1, 0.16)
    names = "|".join(["Molten Showcase"] + [name for name, _, _ in STATIONS])
    captions = "|".join(["Drive up to anything to see what it is"] + [caption for _, caption, _ in STATIONS])
    piece("Station Panel", "UIPanel", anchor=(0, 0), offset=(206, 58), size=(380, 76), color=glass_dark, border=1.5, borderColor=rim)
    piece("Station Name", "HudText", text="{station:%s}" % names, anchor=(0, 0), offset=(34, 28), size=26, color=(1, 1, 1, 1))
    piece("Station Caption", "HudText", text="{station:%s}" % captions, anchor=(0, 0), offset=(34, 62), size=13, color=(1, 1, 1, 0.62))
    piece("Stars Panel", "UIPanel", anchor=(1, 0), offset=(-96, 58), size=(150, 76), color=glass_dark, border=1.5, borderColor=rim)
    piece("Stars Label", "HudText", text="STARS", anchor=(1, 0), offset=(-34, 28), size=11, color=(0.55, 0.85, 1.0, 1))
    piece("Stars Count", "HudText", text="{stars} / 8", anchor=(1, 0), offset=(-34, 44), size=28, color=(1, 1, 1, 1))
    write(folder + "/Assets/Scenes/Main.scene", s.text())


if __name__ == "__main__":
    driving()
    # The showcase of everything is made only on asking: the project worked on is Driving.
    if "--showcase" in sys.argv: showcase()
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
