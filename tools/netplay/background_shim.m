// Lets melonDS run quietly next to other work (macOS only). play.py builds this into a
// dylib and loads it into each emulator with DYLD_INSERT_LIBRARIES.
//
// - Qt stops painting windows that macOS reports as covered, so a hidden emulator keeps
//   running but its window (and any screenshot of it) freezes. Report every window as
//   visible instead.
// - Freshly launched emulators bring themselves to the front and take the keyboard.
//   Ignore activation requests for the first few seconds; clicking a window later still
//   focuses it as usual.
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#define STARTUP_SECONDS 5.0

static CFAbsoluteTime sLaunchTime;
static IMP sActivateIgnoringOtherApps;
static IMP sActivate;

static BOOL StillStartingUp(void)
{
    return CFAbsoluteTimeGetCurrent() - sLaunchTime < STARTUP_SECONDS;
}

static NSWindowOcclusionState AlwaysVisible(id self, SEL _cmd)
{
    return NSWindowOcclusionStateVisible;
}

static NSApplicationOcclusionState AppAlwaysVisible(id self, SEL _cmd)
{
    return NSApplicationOcclusionStateVisible;
}

static void ActivateIgnoringOtherApps(id self, SEL _cmd, BOOL flag)
{
    if (!StillStartingUp()) {
        ((void (*)(id, SEL, BOOL))sActivateIgnoringOtherApps)(self, _cmd, flag);
    }
}

static void Activate(id self, SEL _cmd)
{
    if (!StillStartingUp()) {
        ((void (*)(id, SEL))sActivate)(self, _cmd);
    }
}

__attribute__((constructor)) static void InstallBackgroundShim(void)
{
    sLaunchTime = CFAbsoluteTimeGetCurrent();

    method_setImplementation(class_getInstanceMethod([NSWindow class], @selector(occlusionState)), (IMP)AlwaysVisible);
    method_setImplementation(class_getInstanceMethod([NSApplication class], @selector(occlusionState)), (IMP)AppAlwaysVisible);

    Method method = class_getInstanceMethod([NSApplication class], @selector(activateIgnoringOtherApps:));
    sActivateIgnoringOtherApps = method_setImplementation(method, (IMP)ActivateIgnoringOtherApps);

    method = class_getInstanceMethod([NSApplication class], NSSelectorFromString(@"activate")); // macOS 14+
    if (method != NULL) {
        sActivate = method_setImplementation(method, (IMP)Activate);
    }
}
