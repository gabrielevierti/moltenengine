#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "Platform.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cctype>
#include <cstdlib>
#include <memory>

// ---------------------------------------------------------------- the Mac's own menu bar
//
// The menus are made again each time one opens (menuNeedsUpdate:), from what the editor says
// it holds now. A chosen row does not run there, inside the Mac's handling of the menu: what
// it does is kept, and done at the editor's next frame (takeChosenMenuActions), as a click in
// the window would be.
//
// The shortcuts are shown in the menus, but the keys are not taken by them: the menus say they
// have no key equivalent (menuHasKeyEquivalent:), the keys reach the window, and the editor's
// own shortcuts (which the Preferences change, and which leave a text field its Cmd+C) do the
// work. Only the program's own menu keeps its keys, for Hide and Quit to work as everywhere.

namespace {
using RowsOf = std::function<std::vector<molten::editor::platform::NativeMenuRow>(const std::string&)>;
RowsOf gRowsOf;
std::vector<std::function<void()>> gChosen;
NSString* gProgramMenuTitle = nil;
}  // namespace

@interface MMMenuAction : NSObject
@property(nonatomic) std::shared_ptr<std::function<void()>> action;
@end
@implementation MMMenuAction
@end

@interface MMMenus : NSObject <NSMenuDelegate>
@property(nonatomic, copy) NSString* menuName;
@property(nonatomic) BOOL keepsKeys;             // the program's menu: its keys are its own (Hide, Quit)
- (void)chosen:(NSMenuItem*)item;
@end

static MMMenus* gMenusTarget = nil;
static NSMutableArray<MMMenus*>* gMenuDelegates = nil;

// "Shift+Cmd+S" -> the key and the modifiers a menu row shows.
static void applyKeys(NSMenuItem* item, const std::string& keys) {
    if (keys.empty()) return;
    NSEventModifierFlags flags = 0;
    std::string key = keys;
    size_t plus;
    while ((plus = key.find('+')) != std::string::npos && plus + 1 < key.size()) {
        std::string part = key.substr(0, plus);
        if (part == "Cmd" || part == "Command") flags |= NSEventModifierFlagCommand;
        else if (part == "Shift") flags |= NSEventModifierFlagShift;
        else if (part == "Alt" || part == "Option") flags |= NSEventModifierFlagOption;
        else if (part == "Ctrl" || part == "Control") flags |= NSEventModifierFlagControl;
        key = key.substr(plus + 1);
    }
    NSString* equivalent = nil;
    if (key.size() == 1) {
        equivalent = [[NSString stringWithUTF8String:key.c_str()] lowercaseString];
    } else if (key.size() >= 2 && key.size() <= 3 && key[0] == 'F' && std::isdigit(static_cast<unsigned char>(key[1]))) {
        unichar function = unichar(NSF1FunctionKey + (std::atoi(key.c_str() + 1) - 1));
        equivalent = [NSString stringWithCharacters:&function length:1];
    } else {
        static const std::pair<const char*, unichar> named[] = {{"Comma", ','}, {"Period", '.'}, {"Slash", '/'}, {"Minus", '-'}, {"Equal", '='},
                                                               {"Backspace", 0x08}, {"Delete", 0x7F}, {"Space", ' '}, {"Enter", '\r'}, {"Return", '\r'},
                                                               {"Escape", 0x1B}, {"Tab", '\t'}, {"Left", NSLeftArrowFunctionKey}, {"Right", NSRightArrowFunctionKey},
                                                               {"Up", NSUpArrowFunctionKey}, {"Down", NSDownArrowFunctionKey}};
        for (const auto& [name, character] : named) {
            if (key == name) equivalent = [NSString stringWithCharacters:&character length:1];
        }
    }
    if (!equivalent) return;
    item.keyEquivalent = equivalent;
    item.keyEquivalentModifierMask = flags;
}

static void fillMenu(NSMenu* menu, const std::vector<molten::editor::platform::NativeMenuRow>& rows) {
    [menu removeAllItems];
    menu.autoenablesItems = NO;
    bool lastWasLine = true;
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& row = rows[i];
        if (row.title.empty()) {
            if (!lastWasLine && i + 1 < rows.size()) [menu addItem:[NSMenuItem separatorItem]];
            lastWasLine = true;
            continue;
        }
        lastWasLine = false;
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:row.title.c_str()] ?: @"" action:nil keyEquivalent:@""];
        item.enabled = row.isEnabled;
        item.state = row.isChecked ? NSControlStateValueOn : NSControlStateValueOff;
        if (!row.children.empty()) {
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:item.title];
            submenu.delegate = gMenusTarget;              // (keys are left to the window here too)
            fillMenu(submenu, row.children);
            item.submenu = submenu;
        } else {
            applyKeys(item, row.keys);
            if (row.run) {
                MMMenuAction* action = [MMMenuAction new];
                action.action = std::make_shared<std::function<void()>>(row.run);
                item.representedObject = action;
                item.target = gMenusTarget;
                item.action = @selector(chosen:);
            }
        }
        [menu addItem:item];
    }
    // A line left at the end, after what was taken out.
    while (menu.numberOfItems > 0 && menu.itemArray.lastObject.isSeparatorItem) [menu removeItemAtIndex:menu.numberOfItems - 1];
}

@implementation MMMenus
- (void)menuNeedsUpdate:(NSMenu*)menu {
    if (!gRowsOf || self.menuName == nil) return;
    std::vector<molten::editor::platform::NativeMenuRow> rows = gRowsOf(self.menuName.UTF8String);
    if (gProgramMenuTitle && [self.menuName isEqualToString:gProgramMenuTitle]) {
        // The program's own menu: what the editor has (About, Preferences, Quit), with the Mac's Hide and Show All before Quit.
        NSMenuItem* last = nil;
        if (!rows.empty() && !rows.back().title.empty()) {
            fillMenu(menu, std::vector<molten::editor::platform::NativeMenuRow>(rows.begin(), rows.end() - 1));
            last = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:rows.back().title.c_str()] action:@selector(chosen:) keyEquivalent:@""];
            applyKeys(last, rows.back().keys);
            MMMenuAction* action = [MMMenuAction new];
            action.action = std::make_shared<std::function<void()>>(rows.back().run);
            last.representedObject = action;
            last.target = self;
        } else {
            fillMenu(menu, rows);
        }
        [menu addItem:[NSMenuItem separatorItem]];
        NSString* name = gProgramMenuTitle;
        [menu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
        NSMenuItem* others = [menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
        others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        [menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
        if (last) {
            [menu addItem:[NSMenuItem separatorItem]];
            [menu addItem:last];
        }
        return;
    }
    fillMenu(menu, rows);
}

// The program's menu answers its keys as menus do: as if this were not here.
- (BOOL)respondsToSelector:(SEL)selector {
    if (self.keepsKeys && selector == @selector(menuHasKeyEquivalent:forEvent:target:action:)) return NO;
    return [super respondsToSelector:selector];
}

// The keys shown in the menus are the editor's: the menus do not take them (see above).
- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu forEvent:(NSEvent*)event target:(id*)target action:(SEL*)action {
    return NO;
}

- (void)chosen:(NSMenuItem*)item {
    MMMenuAction* action = item.representedObject;
    if (action && action.action && *action.action) gChosen.push_back(*action.action);
}
@end

namespace molten::editor::platform {

namespace {

NSString* toNSString(const std::string& text) {
    return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

std::string toString(NSString* text) {
    return text ? std::string(text.UTF8String) : std::string();
}

}  // namespace

std::string chooseFolder(const std::string& title, const std::string& startDirectory) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.message = toNSString(title);
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.canCreateDirectories = YES;
        panel.allowsMultipleSelection = NO;
        if (!startDirectory.empty()) panel.directoryURL = [NSURL fileURLWithPath:toNSString(startDirectory)];
        if ([panel runModal] != NSModalResponseOK || panel.URL == nil) return {};
        return toString(panel.URL.path);
    }
}

std::vector<std::string> chooseFiles(const std::string& title, const std::vector<std::string>& extensions, bool allowMany,
                                     const std::string& startDirectory) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.message = toNSString(title);
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = allowMany ? YES : NO;
        if (!startDirectory.empty()) panel.directoryURL = [NSURL fileURLWithPath:toNSString(startDirectory)];
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (const std::string& extension : extensions) {
            UTType* type = [UTType typeWithFilenameExtension:toNSString(extension)];
            if (type) [types addObject:type];
        }
        if (types.count > 0) panel.allowedContentTypes = types;
        std::vector<std::string> paths;
        if ([panel runModal] != NSModalResponseOK) return paths;
        for (NSURL* url in panel.URLs) paths.push_back(toString(url.path));
        return paths;
    }
}

std::string chooseSaveFile(const std::string& title, const std::string& suggestedName, const std::string& extension) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.message = toNSString(title);
        panel.nameFieldStringValue = toNSString(suggestedName);
        panel.canCreateDirectories = YES;
        if (!extension.empty()) {
            UTType* type = [UTType typeWithFilenameExtension:toNSString(extension)];
            if (type) panel.allowedContentTypes = @[type];
        }
        if ([panel runModal] != NSModalResponseOK) return {};
        return toString(panel.URL.path);
    }
}

bool moveToTrash(const std::string& path) {
    @autoreleasepool {
        NSError* error = nil;
        NSURL* url = [NSURL fileURLWithPath:toNSString(path)];
        return [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:&error] == YES;
    }
}

void revealInFinder(const std::string& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:toNSString(path)];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
    }
}

void openWithDefaultApplication(const std::string& path) {
    @autoreleasepool {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:toNSString(path)]];
    }
}

void openWebPage(const std::string& address) {
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:toNSString(address)];
        if (url) [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

bool usesNativeMenuBar() { return NSApp != nil; }

void setNativeMenuBar(const std::vector<std::string>& menuTitles, std::function<std::vector<NativeMenuRow>(const std::string&)> rowsOf) {
    @autoreleasepool {
        if (NSApp == nil || menuTitles.empty()) return;
        gRowsOf = std::move(rowsOf);
        if (!gMenusTarget) gMenusTarget = [MMMenus new];
        gMenuDelegates = [NSMutableArray new];
        NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
        for (size_t i = 0; i < menuTitles.size(); i++) {
            NSString* title = toNSString(menuTitles[i]);
            NSMenuItem* holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
            NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
            MMMenus* delegate = [MMMenus new];
            delegate.menuName = title;
            if (i == 0) gProgramMenuTitle = title;
            delegate.keepsKeys = i == 0;            // the program's menu keeps its keys (Hide, Quit); the others leave theirs to the editor
            menu.delegate = delegate;
            [gMenuDelegates addObject:delegate];       // a menu does not keep its delegate: they are kept here
            holder.submenu = menu;
            [bar addItem:holder];
            // Filled once now, so that the keys of the program's menu work before it is first opened.
            [delegate menuNeedsUpdate:menu];
        }
        NSApp.mainMenu = bar;
        // The Mac's own Window menu is not wanted: the editor's Window menu is about its windows.
        NSApp.windowsMenu = nil;
    }
}

std::vector<std::function<void()>> takeChosenMenuActions() {
    std::vector<std::function<void()>> chosen;
    chosen.swap(gChosen);
    return chosen;
}

void setWindowEdited(void* window, bool isEdited) {
    @autoreleasepool {
        if (!window) return;
        NSWindow* native = glfwGetCocoaWindow(static_cast<GLFWwindow*>(window));
        if (native) native.documentEdited = isEdited;
    }
}

// ---------------------------------------------------------------- full screen, with the menu bar

}  // namespace molten::editor::platform

@interface MMFullScreen : NSObject
@property (weak) NSWindow* window;
@property NSRect before;
@property NSWindowStyleMask styleBefore;
@property BOOL isOn;
- (void)toggle:(id)sender;
- (void)takeGreenButton;
@end

@implementation MMFullScreen
- (void)takeGreenButton {
    NSWindow* window = self.window;
    if (!window) return;
    // Not the Mac's full screen (which hides the menu bar): the green button does this one.
    window.collectionBehavior = (window.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenPrimary) | NSWindowCollectionBehaviorFullScreenNone;
    NSButton* green = [window standardWindowButton:NSWindowZoomButton];
    green.target = self;
    green.action = @selector(toggle:);
    green.enabled = YES;
}
- (void)toggle:(id)sender {
    NSWindow* window = self.window;
    if (!window) return;
    if (!self.isOn) {
        self.before = window.frame;
        self.styleBefore = window.styleMask;
        NSScreen* screen = window.screen ?: NSScreen.mainScreen;
        NSRect whole = screen.frame;
        // Up to the menu bar, which stays: all of the screen below it, where the Dock was too.
        CGFloat top = NSMaxY(screen.visibleFrame);
        NSRect covered = NSMakeRect(whole.origin.x, whole.origin.y, whole.size.width, top - whole.origin.y);
        NSApp.presentationOptions = NSApplicationPresentationAutoHideDock;
        window.styleMask = NSWindowStyleMaskBorderless;
        [window setFrame:covered display:YES animate:NO];
        self.isOn = YES;
    } else {
        window.styleMask = self.styleBefore;
        [window setFrame:self.before display:YES animate:NO];
        NSApp.presentationOptions = NSApplicationPresentationDefault;
        self.isOn = NO;
        [self takeGreenButton];             // the title bar's buttons are made again with it
    }
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:window.contentView];
}
@end

namespace {
MMFullScreen* gFullScreen = nil;

MMFullScreen* fullScreenFor(void* glfwWindow) {
    if (!glfwWindow) return nil;
    NSWindow* native = glfwGetCocoaWindow(static_cast<GLFWwindow*>(glfwWindow));
    if (!native) return nil;
    if (!gFullScreen || gFullScreen.window != native) {
        gFullScreen = [MMFullScreen new];
        gFullScreen.window = native;
        [gFullScreen takeGreenButton];
    }
    return gFullScreen;
}
}  // namespace

namespace molten::editor::platform {

void takeOverFullScreen(void* glfwWindow) {
    @autoreleasepool {
        fullScreenFor(glfwWindow);
    }
}

void toggleFullScreen(void* glfwWindow) {
    @autoreleasepool {
        if (MMFullScreen* full = fullScreenFor(glfwWindow)) [full toggle:nil];
    }
}

bool isFullScreen(void* glfwWindow) {
    @autoreleasepool {
        MMFullScreen* full = fullScreenFor(glfwWindow);
        return full && full.isOn;
    }
}

}  // namespace molten::editor::platform
