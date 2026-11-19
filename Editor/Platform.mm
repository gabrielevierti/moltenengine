#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "Platform.hpp"

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

}  // namespace molten::editor::platform
