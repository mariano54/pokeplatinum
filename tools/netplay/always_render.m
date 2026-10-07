// Keeps melonDS drawing while its window is covered by other windows (macOS only).
//
// Qt stops painting windows that macOS reports as occluded, so a hidden emulator keeps
// running but its window (and any screenshot of it) freezes. play.py --always-render
// builds this into a dylib and loads it into melonDS with DYLD_INSERT_LIBRARIES.
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

static NSWindowOcclusionState AlwaysVisible(id self, SEL _cmd)
{
    return NSWindowOcclusionStateVisible;
}

static NSApplicationOcclusionState AppAlwaysVisible(id self, SEL _cmd)
{
    return NSApplicationOcclusionStateVisible;
}

__attribute__((constructor)) static void InstallAlwaysVisible(void)
{
    method_setImplementation(class_getInstanceMethod([NSWindow class], @selector(occlusionState)), (IMP)AlwaysVisible);
    method_setImplementation(class_getInstanceMethod([NSApplication class], @selector(occlusionState)), (IMP)AppAlwaysVisible);
}
