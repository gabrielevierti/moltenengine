// Icons.hpp
// The editor's icons: letters of Assets/Fonts/Icons.ttf (Lucide, ISC license), as UTF-8 text.
// Made by Tools/make_icon_font.py; do not change by hand: add the icon there and run it again.
//
// Put one in a label ("Lighting" with its icon before it: std::string(icon::Sun) + "  Lighting"),
// or draw one at any size with ui::drawIcon (Panels/Widgets.hpp).
#pragma once

#include <string_view>

namespace molten::editor::icon {

inline constexpr const char* Activity = "\xee\x80\xb8";
inline constexpr const char* Anchor = "\xee\x80\xbf";
inline constexpr const char* Aperture = "\xee\x81\x80";
inline constexpr const char* AppWindow = "\xee\x90\xa6";
inline constexpr const char* Apple = "\xee\x8d\x8e";
inline constexpr const char* ArrowDown = "\xee\x81\x82";
inline constexpr const char* ArrowLeft = "\xee\x81\x88";
inline constexpr const char* ArrowRight = "\xee\x81\x89";
inline constexpr const char* ArrowUp = "\xee\x81\x8a";
inline constexpr const char* ArrowUpDown = "\xee\x8d\xbd";
inline constexpr const char* Atom = "\xee\x8f\x97";
inline constexpr const char* AudioLines = "\xee\x95\x9a";
inline constexpr const char* BadgeCheck = "\xee\x89\x81";
inline constexpr const char* Bell = "\xee\x81\x99";
inline constexpr const char* Blocks = "\xee\x93\xba";
inline constexpr const char* Bolt = "\xee\x96\x8c";
inline constexpr const char* BookOpen = "\xee\x81\x9f";
inline constexpr const char* Box = "\xee\x81\xa1";
inline constexpr const char* BoxSelect = "\xee\x87\x8b";
inline constexpr const char* Boxes = "\xee\x8b\x90";
inline constexpr const char* Braces = "\xee\x8d\xaa";
inline constexpr const char* Brush = "\xee\x87\x93";
inline constexpr const char* Bug = "\xee\x88\x8c";
inline constexpr const char* Building = "\xee\x87\x8c";
inline constexpr const char* Cable = "\xee\x93\xa3";
inline constexpr const char* Camera = "\xee\x81\xa4";
inline constexpr const char* Car = "\xee\x87\x95";
inline constexpr const char* Check = "\xee\x81\xac";
inline constexpr const char* ChevronDown = "\xee\x81\xad";
inline constexpr const char* ChevronLeft = "\xee\x81\xae";
inline constexpr const char* ChevronRight = "\xee\x81\xaf";
inline constexpr const char* ChevronUp = "\xee\x81\xb0";
inline constexpr const char* Circle = "\xee\x81\xb6";
inline constexpr const char* CircleAlert = "\xee\x81\xb7";
inline constexpr const char* CircleCheck = "\xee\x88\xa6";
inline constexpr const char* CircleDashed = "\xee\x92\xb0";
inline constexpr const char* CircleDot = "\xee\x8d\x85";
inline constexpr const char* CircleHelp = "\xee\x82\x82";
inline constexpr const char* CircleX = "\xee\x82\x84";
inline constexpr const char* Clapperboard = "\xee\x8a\x9b";
inline constexpr const char* Clock = "\xee\x82\x87";
inline constexpr const char* Cloud = "\xee\x82\x88";
inline constexpr const char* CloudLightning = "\xee\x82\x8c";
inline constexpr const char* CloudRain = "\xee\x82\x8e";
inline constexpr const char* CloudSun = "\xee\x88\x96";
inline constexpr const char* Code = "\xee\x82\x93";
inline constexpr const char* Coins = "\xee\x82\x97";
inline constexpr const char* Columns2 = "\xee\x82\x98";
inline constexpr const char* Component = "\xee\x8a\xad";
inline constexpr const char* Cone = "\xee\x94\xa3";
inline constexpr const char* Container = "\xee\x93\x95";
inline constexpr const char* Copy = "\xee\x82\x9e";
inline constexpr const char* Cpu = "\xee\x82\xa9";
inline constexpr const char* Crosshair = "\xee\x82\xac";
inline constexpr const char* Cuboid = "\xee\x94\xa4";
inline constexpr const char* Cylinder = "\xee\x94\xa5";
inline constexpr const char* Diamond = "\xee\x8b\x92";
inline constexpr const char* Download = "\xee\x82\xb2";
inline constexpr const char* Droplet = "\xee\x82\xb4";
inline constexpr const char* Droplets = "\xee\x82\xb5";
inline constexpr const char* Ellipsis = "\xee\x82\xb6";
inline constexpr const char* EllipsisVertical = "\xee\x82\xb7";
inline constexpr const char* Eraser = "\xee\x8a\x8f";
inline constexpr const char* Expand = "\xee\x88\x9a";
inline constexpr const char* ExternalLink = "\xee\x82\xb9";
inline constexpr const char* Eye = "\xee\x82\xba";
inline constexpr const char* EyeOff = "\xee\x82\xbb";
inline constexpr const char* File = "\xee\x83\x80";
inline constexpr const char* FileCode = "\xee\x83\x83";
inline constexpr const char* FileCode2 = "\xee\x91\x9e";
inline constexpr const char* Film = "\xee\x83\x90";
inline constexpr const char* Filter = "\xee\x83\x9c";
inline constexpr const char* Flag = "\xee\x83\x91";
inline constexpr const char* Flame = "\xee\x83\x92";
inline constexpr const char* Flashlight = "\xee\x83\x93";
inline constexpr const char* Focus = "\xee\x8a\x9e";
inline constexpr const char* Folder = "\xee\x83\x97";
inline constexpr const char* FolderOpen = "\xee\x89\x87";
inline constexpr const char* FolderTree = "\xee\x8c\xbc";
inline constexpr const char* Footprints = "\xee\x8e\xb9";
inline constexpr const char* Frame = "\xee\x8a\x91";
inline constexpr const char* Gamepad2 = "\xee\x83\x9f";
inline constexpr const char* GanttChart = "\xee\x93\x84";
inline constexpr const char* Gauge = "\xee\x86\xbf";
inline constexpr const char* Gem = "\xee\x89\x82";
inline constexpr const char* GitBranch = "\xee\x83\xa2";
inline constexpr const char* Globe = "\xee\x83\xa8";
inline constexpr const char* Grid3x3 = "\xee\x83\xa9";
inline constexpr const char* Hammer = "\xee\x83\xac";
inline constexpr const char* Hand = "\xee\x87\x97";
inline constexpr const char* HardDrive = "\xee\x83\xad";
inline constexpr const char* Headphones = "\xee\x83\xb1";
inline constexpr const char* Heart = "\xee\x83\xb2";
inline constexpr const char* Hexagon = "\xee\x83\xb3";
inline constexpr const char* History = "\xee\x87\xb5";
inline constexpr const char* House = "\xee\x83\xb5";
inline constexpr const char* Image = "\xee\x83\xb6";
inline constexpr const char* Import = "\xee\x88\xaf";
inline constexpr const char* Info = "\xee\x83\xb9";
inline constexpr const char* Keyboard = "\xee\x8a\x84";
inline constexpr const char* LampCeiling = "\xee\x8b\x99";
inline constexpr const char* Landmark = "\xee\x88\xba";
inline constexpr const char* Laptop = "\xee\x87\x8d";
inline constexpr const char* Layers = "\xee\x94\xa9";
inline constexpr const char* LayoutDashboard = "\xee\x87\x81";
inline constexpr const char* LayoutTemplate = "\xee\x88\x87";
inline constexpr const char* Lightbulb = "\xee\x87\x82";
inline constexpr const char* Link = "\xee\x84\x82";
inline constexpr const char* ListFilter = "\xee\x91\xa0";
inline constexpr const char* ListTree = "\xee\x90\x88";
inline constexpr const char* Lock = "\xee\x84\x8b";
inline constexpr const char* LockOpen = "\xee\x84\x8c";
inline constexpr const char* Magnet = "\xee\x8a\xb5";
inline constexpr const char* Maximize2 = "\xee\x84\x93";
inline constexpr const char* MemoryStick = "\xee\x91\x85";
inline constexpr const char* MessageSquare = "\xee\x84\x97";
inline constexpr const char* Mic = "\xee\x84\x98";
inline constexpr const char* Minimize2 = "\xee\x84\x9b";
inline constexpr const char* Minus = "\xee\x84\x9c";
inline constexpr const char* Monitor = "\xee\x84\x9d";
inline constexpr const char* Moon = "\xee\x84\x9e";
inline constexpr const char* Mountain = "\xee\x88\xb1";
inline constexpr const char* MousePointer2 = "\xee\x87\x83";
inline constexpr const char* Move = "\xee\x84\xa1";
inline constexpr const char* Move3d = "\xee\x8b\xa5";
inline constexpr const char* Music = "\xee\x84\xa2";
inline constexpr const char* Navigation = "\xee\x84\xa3";
inline constexpr const char* Network = "\xee\x84\xa5";
inline constexpr const char* OctagonAlert = "\xee\x84\xa7";
inline constexpr const char* Orbit = "\xee\x8f\xa7";
inline constexpr const char* Package = "\xee\x84\xa9";
inline constexpr const char* Paintbrush = "\xee\x8b\xa7";
inline constexpr const char* Palette = "\xee\x87\x9d";
inline constexpr const char* PanelBottom = "\xee\x90\xac";
inline constexpr const char* PanelLeft = "\xee\x84\xaa";
inline constexpr const char* PanelRight = "\xee\x90\xb1";
inline constexpr const char* PanelsTopLeft = "\xee\x84\xac";
inline constexpr const char* Pause = "\xee\x84\xae";
inline constexpr const char* Pencil = "\xee\x87\xb9";
inline constexpr const char* PersonStanding = "\xee\x88\x9e";
inline constexpr const char* Pipette = "\xee\x84\xbb";
inline constexpr const char* Plane = "\xee\x87\x9e";
inline constexpr const char* Play = "\xee\x84\xbc";
inline constexpr const char* Plus = "\xee\x84\xbd";
inline constexpr const char* Puzzle = "\xee\x8a\x9c";
inline constexpr const char* Pyramid = "\xee\x94\xac";
inline constexpr const char* Radio = "\xee\x85\x82";
inline constexpr const char* RectangleHorizontal = "\xee\x8d\xb6";
inline constexpr const char* Redo2 = "\xee\x8a\xa0";
inline constexpr const char* RefreshCw = "\xee\x85\x85";
inline constexpr const char* Rocket = "\xee\x8a\x86";
inline constexpr const char* Rotate3d = "\xee\x8b\xaa";
inline constexpr const char* RotateCw = "\xee\x85\x89";
inline constexpr const char* Route = "\xee\x94\xbe";
inline constexpr const char* Rows2 = "\xee\x90\xb9";
inline constexpr const char* Sailboat = "\xee\x8d\xbe";
inline constexpr const char* Save = "\xee\x85\x8d";
inline constexpr const char* Scale = "\xee\x88\x92";
inline constexpr const char* Scale3d = "\xee\x8b\xab";
inline constexpr const char* Scaling = "\xee\x8b\xac";
inline constexpr const char* Scan = "\xee\x89\x97";
inline constexpr const char* ScanEye = "\xee\x94\xb6";
inline constexpr const char* Search = "\xee\x85\x91";
inline constexpr const char* Settings = "\xee\x85\x94";
inline constexpr const char* Settings2 = "\xee\x89\x85";
inline constexpr const char* Shapes = "\xee\x92\xb3";
inline constexpr const char* Share2 = "\xee\x85\x96";
inline constexpr const char* Shield = "\xee\x85\x98";
inline constexpr const char* Ship = "\xee\x8e\xba";
inline constexpr const char* Skull = "\xee\x88\xa1";
inline constexpr const char* SlidersHorizontal = "\xee\x8a\x9a";
inline constexpr const char* Smartphone = "\xee\x85\xa3";
inline constexpr const char* Snowflake = "\xee\x85\xa5";
inline constexpr const char* Sparkle = "\xee\x91\xbe";
inline constexpr const char* Sparkles = "\xee\x90\x92";
inline constexpr const char* Speaker = "\xee\x85\xa6";
inline constexpr const char* Spline = "\xee\x8e\x8b";
inline constexpr const char* Sprout = "\xee\x87\xab";
inline constexpr const char* Square = "\xee\x85\xa7";
inline constexpr const char* SquareDashed = "\xee\x87\x8b";
inline constexpr const char* SquareStack = "\xee\x92\xa2";
inline constexpr const char* Star = "\xee\x85\xb6";
inline constexpr const char* StepForward = "\xee\x8f\xaa";
inline constexpr const char* Sticker = "\xee\x8c\x82";
inline constexpr const char* Sun = "\xee\x85\xb8";
inline constexpr const char* SunMedium = "\xee\x8a\xb1";
inline constexpr const char* SunSnow = "\xee\x8d\xb2";
inline constexpr const char* Sunset = "\xee\x85\xba";
inline constexpr const char* Swords = "\xee\x8a\xb4";
inline constexpr const char* Tag = "\xee\x85\xbf";
inline constexpr const char* Target = "\xee\x86\x80";
inline constexpr const char* Tent = "\xee\x88\xa7";
inline constexpr const char* Terminal = "\xee\x86\x81";
inline constexpr const char* TerminalSquare = "\xee\x88\x8a";
inline constexpr const char* Thermometer = "\xee\x86\x86";
inline constexpr const char* Timer = "\xee\x87\xa0";
inline constexpr const char* Torus = "\xee\x94\xaf";
inline constexpr const char* Trash2 = "\xee\x86\x8e";
inline constexpr const char* TreePine = "\xee\x8b\xb4";
inline constexpr const char* Trees = "\xee\x8b\xb5";
inline constexpr const char* Triangle = "\xee\x86\x92";
inline constexpr const char* TriangleAlert = "\xee\x86\x93";
inline constexpr const char* Trophy = "\xee\x8d\xb3";
inline constexpr const char* Type = "\xee\x86\x98";
inline constexpr const char* Undo2 = "\xee\x8a\xa1";
inline constexpr const char* Upload = "\xee\x86\x9e";
inline constexpr const char* User = "\xee\x86\x9f";
inline constexpr const char* Video = "\xee\x86\xa5";
inline constexpr const char* View = "\xee\x86\xa7";
inline constexpr const char* Volume2 = "\xee\x86\xab";
inline constexpr const char* VolumeX = "\xee\x86\xac";
inline constexpr const char* WandSparkles = "\xee\x8d\x97";
inline constexpr const char* Waves = "\xee\x8a\x83";
inline constexpr const char* Weight = "\xee\x94\xb0";
inline constexpr const char* Wifi = "\xee\x86\xae";
inline constexpr const char* Wind = "\xee\x86\xb0";
inline constexpr const char* Workflow = "\xee\x90\xa5";
inline constexpr const char* Wrench = "\xee\x86\xb1";
inline constexpr const char* X = "\xee\x86\xb2";
inline constexpr const char* Zap = "\xee\x86\xb4";

/// An icon by its Lucide name ("grid-3x3"); nullptr if it is not in the font.
inline const char* named(std::string_view name) {
    struct Entry { std::string_view name; const char* glyph; };
    static constexpr Entry all[] = {
        {"activity", Activity},
        {"anchor", Anchor},
        {"aperture", Aperture},
        {"app-window", AppWindow},
        {"apple", Apple},
        {"arrow-down", ArrowDown},
        {"arrow-left", ArrowLeft},
        {"arrow-right", ArrowRight},
        {"arrow-up", ArrowUp},
        {"arrow-up-down", ArrowUpDown},
        {"atom", Atom},
        {"audio-lines", AudioLines},
        {"badge-check", BadgeCheck},
        {"bell", Bell},
        {"blocks", Blocks},
        {"bolt", Bolt},
        {"book-open", BookOpen},
        {"box", Box},
        {"box-select", BoxSelect},
        {"boxes", Boxes},
        {"braces", Braces},
        {"brush", Brush},
        {"bug", Bug},
        {"building", Building},
        {"cable", Cable},
        {"camera", Camera},
        {"car", Car},
        {"check", Check},
        {"chevron-down", ChevronDown},
        {"chevron-left", ChevronLeft},
        {"chevron-right", ChevronRight},
        {"chevron-up", ChevronUp},
        {"circle", Circle},
        {"circle-alert", CircleAlert},
        {"circle-check", CircleCheck},
        {"circle-dashed", CircleDashed},
        {"circle-dot", CircleDot},
        {"circle-help", CircleHelp},
        {"circle-x", CircleX},
        {"clapperboard", Clapperboard},
        {"clock", Clock},
        {"cloud", Cloud},
        {"cloud-lightning", CloudLightning},
        {"cloud-rain", CloudRain},
        {"cloud-sun", CloudSun},
        {"code", Code},
        {"coins", Coins},
        {"columns-2", Columns2},
        {"component", Component},
        {"cone", Cone},
        {"container", Container},
        {"copy", Copy},
        {"cpu", Cpu},
        {"crosshair", Crosshair},
        {"cuboid", Cuboid},
        {"cylinder", Cylinder},
        {"diamond", Diamond},
        {"download", Download},
        {"droplet", Droplet},
        {"droplets", Droplets},
        {"ellipsis", Ellipsis},
        {"ellipsis-vertical", EllipsisVertical},
        {"eraser", Eraser},
        {"expand", Expand},
        {"external-link", ExternalLink},
        {"eye", Eye},
        {"eye-off", EyeOff},
        {"file", File},
        {"file-code", FileCode},
        {"file-code-2", FileCode2},
        {"film", Film},
        {"filter", Filter},
        {"flag", Flag},
        {"flame", Flame},
        {"flashlight", Flashlight},
        {"focus", Focus},
        {"folder", Folder},
        {"folder-open", FolderOpen},
        {"folder-tree", FolderTree},
        {"footprints", Footprints},
        {"frame", Frame},
        {"gamepad-2", Gamepad2},
        {"gantt-chart", GanttChart},
        {"gauge", Gauge},
        {"gem", Gem},
        {"git-branch", GitBranch},
        {"globe", Globe},
        {"grid-3x3", Grid3x3},
        {"hammer", Hammer},
        {"hand", Hand},
        {"hard-drive", HardDrive},
        {"headphones", Headphones},
        {"heart", Heart},
        {"hexagon", Hexagon},
        {"history", History},
        {"house", House},
        {"image", Image},
        {"import", Import},
        {"info", Info},
        {"keyboard", Keyboard},
        {"lamp-ceiling", LampCeiling},
        {"landmark", Landmark},
        {"laptop", Laptop},
        {"layers", Layers},
        {"layout-dashboard", LayoutDashboard},
        {"layout-template", LayoutTemplate},
        {"lightbulb", Lightbulb},
        {"link", Link},
        {"list-filter", ListFilter},
        {"list-tree", ListTree},
        {"lock", Lock},
        {"lock-open", LockOpen},
        {"magnet", Magnet},
        {"maximize-2", Maximize2},
        {"memory-stick", MemoryStick},
        {"message-square", MessageSquare},
        {"mic", Mic},
        {"minimize-2", Minimize2},
        {"minus", Minus},
        {"monitor", Monitor},
        {"moon", Moon},
        {"mountain", Mountain},
        {"mouse-pointer-2", MousePointer2},
        {"move", Move},
        {"move-3d", Move3d},
        {"music", Music},
        {"navigation", Navigation},
        {"network", Network},
        {"octagon-alert", OctagonAlert},
        {"orbit", Orbit},
        {"package", Package},
        {"paintbrush", Paintbrush},
        {"palette", Palette},
        {"panel-bottom", PanelBottom},
        {"panel-left", PanelLeft},
        {"panel-right", PanelRight},
        {"panels-top-left", PanelsTopLeft},
        {"pause", Pause},
        {"pencil", Pencil},
        {"person-standing", PersonStanding},
        {"pipette", Pipette},
        {"plane", Plane},
        {"play", Play},
        {"plus", Plus},
        {"puzzle", Puzzle},
        {"pyramid", Pyramid},
        {"radio", Radio},
        {"rectangle-horizontal", RectangleHorizontal},
        {"redo-2", Redo2},
        {"refresh-cw", RefreshCw},
        {"rocket", Rocket},
        {"rotate-3d", Rotate3d},
        {"rotate-cw", RotateCw},
        {"route", Route},
        {"rows-2", Rows2},
        {"sailboat", Sailboat},
        {"save", Save},
        {"scale", Scale},
        {"scale-3d", Scale3d},
        {"scaling", Scaling},
        {"scan", Scan},
        {"scan-eye", ScanEye},
        {"search", Search},
        {"settings", Settings},
        {"settings-2", Settings2},
        {"shapes", Shapes},
        {"share-2", Share2},
        {"shield", Shield},
        {"ship", Ship},
        {"skull", Skull},
        {"sliders-horizontal", SlidersHorizontal},
        {"smartphone", Smartphone},
        {"snowflake", Snowflake},
        {"sparkle", Sparkle},
        {"sparkles", Sparkles},
        {"speaker", Speaker},
        {"spline", Spline},
        {"sprout", Sprout},
        {"square", Square},
        {"square-dashed", SquareDashed},
        {"square-stack", SquareStack},
        {"star", Star},
        {"step-forward", StepForward},
        {"sticker", Sticker},
        {"sun", Sun},
        {"sun-medium", SunMedium},
        {"sun-snow", SunSnow},
        {"sunset", Sunset},
        {"swords", Swords},
        {"tag", Tag},
        {"target", Target},
        {"tent", Tent},
        {"terminal", Terminal},
        {"terminal-square", TerminalSquare},
        {"thermometer", Thermometer},
        {"timer", Timer},
        {"torus", Torus},
        {"trash-2", Trash2},
        {"tree-pine", TreePine},
        {"trees", Trees},
        {"triangle", Triangle},
        {"triangle-alert", TriangleAlert},
        {"trophy", Trophy},
        {"type", Type},
        {"undo-2", Undo2},
        {"upload", Upload},
        {"user", User},
        {"video", Video},
        {"view", View},
        {"volume-2", Volume2},
        {"volume-x", VolumeX},
        {"wand-sparkles", WandSparkles},
        {"waves", Waves},
        {"weight", Weight},
        {"wifi", Wifi},
        {"wind", Wind},
        {"workflow", Workflow},
        {"wrench", Wrench},
        {"x", X},
        {"zap", Zap},
    };
    for (const Entry& entry : all) {
        if (entry.name == name) return entry.glyph;
    }
    return nullptr;
}

}  // namespace molten::editor::icon
