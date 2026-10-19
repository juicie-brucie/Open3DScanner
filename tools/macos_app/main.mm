// Programmatic macOS shell for the OpenScanner viewer (rows A2–A4).
// No storyboard, no asset catalog: an NSWindow + WKWebView pointed at the
// worker's origin (A1 serves ui/ on the same port as the WebSocket).
// The app spawns and supervises the capture worker, and quitting sends it
// SIGTERM so the worker runs its graceful pause/stop/destroy teardown.
// The vendor dylib is deliberately NOT bundled; the worker resolves the
// installed scanner app itself and reports a missing SDK.

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

@interface OpenScannerAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, WKNavigationDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@property(nonatomic, strong) NSTask* worker;
@property(nonatomic, assign) BOOL quitting;
@property(nonatomic, assign) BOOL restarting;
@property(nonatomic, assign) BOOL reloadAfterRestart;
@property(nonatomic, assign) NSInteger retryCount;
@property(nonatomic, copy) NSURL* pageURL;
@property(nonatomic, assign) NSInteger port;
@property(nonatomic, assign) NSInteger sessionSeconds;
@end

@implementation OpenScannerAppDelegate

- (NSArray<NSString*>*)librarySearchPaths {
    const char* env = std::getenv("OPENSCANNER_LIBRARY");
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    if (env != nullptr && env[0] != '\0') [paths addObject:[NSString stringWithUTF8String:env]];
    [paths addObject:@"/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib"];
    return paths;
}

- (NSString*)resolveLibraryPath {
    for (NSString* p in [self librarySearchPaths]) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:p]) return p;
    }
    return @"";
}

- (NSString*)resolveWorkerPath {
    NSString* bundled = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"scan_server"];
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:bundled]) return bundled;
    const char* env = std::getenv("OPENSCANNER_SCAN_SERVER");
    if (env != nullptr && env[0] != '\0') return [NSString stringWithUTF8String:env];
    return @"";
}

// The worker's listener is only up after SDK/device bring-up (seconds), so a
// cold launch's first load is refused. Retry with backoff until the origin
// answers instead of leaving WebKit's error page up.
- (void)retryPageLoadSoon {
    if (self.pageURL == nil) return;
    if (self.retryCount >= 40) return;  // ~2 min of retries
    NSInteger attempt = self.retryCount++;
    double delay = MIN(0.25 * (attempt + 1), 2.0);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [self.webView loadRequest:[NSURLRequest requestWithURL:self.pageURL]];
    });
}

- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView; (void)navigation; (void)error;
    [self retryPageLoadSoon];
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    (void)webView; (void)navigation;
    self.retryCount = 0;
}

// Bounded pre-flight: is something already listening on the configured
// port? If so the worker's server.Start() would fail; report it with the
// port number instead of letting the worker exit opaquely.
- (BOOL)isPortInUse:(NSInteger)port {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NO;
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    struct timeval tv = {};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    BOOL in_use = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    close(fd);
    return in_use;
}

- (void)startWorker {
    NSString* workerPath = [self resolveWorkerPath];
    if (workerPath.length == 0) {
        [self surfaceError:@"scan_server not found in the app bundle or OPENSCANNER_SCAN_SERVER."];
        return;
    }
    NSString* library = [self resolveLibraryPath];
    if (library.length == 0) {
        NSMutableString* msg = [NSMutableString stringWithString:
            @"CrealityScan's liborbbec_scan.dylib was not found.\n\nSearched:\n"];
        for (NSString* p in [self librarySearchPaths]) [msg appendFormat:@"  • %@\n", p];
        [msg appendString:@"\nInstall CrealityScan, or set OPENSCANNER_LIBRARY to the dylib."];
        [self surfaceError:msg];
        return;
    }
    if ([self isPortInUse:self.port]) {
        [self surfaceError:[NSString stringWithFormat:
            @"Port %ld is already in use by another process.\n\nUse Settings (Cmd-,) to choose a "
            @"different port, or quit the other program.", (long)self.port]];
        return;
    }
    NSString* port = [NSString stringWithFormat:@"%ld", (long)self.port];
    NSString* seconds = [NSString stringWithFormat:@"%ld", (long)self.sessionSeconds];

    self.worker = [[NSTask alloc] init];
    self.worker.launchPath = workerPath;
    self.worker.arguments = @[@"--live-worker", port, library, seconds];
    __weak OpenScannerAppDelegate* weakSelf = self;
    self.worker.terminationHandler = ^(NSTask* task) {
        // NSTask fires this off the main thread; every AppKit call below
        // must hop back to the main queue.
        int status = task.terminationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            OpenScannerAppDelegate* self = weakSelf;
            if (self == nil) return;
            if (self.quitting) {
                [NSApp replyToApplicationShouldTerminate:YES];
            } else if (self.restarting) {
                self.restarting = NO;
                [self startWorker];
                [self reloadPageIfNeeded];
            } else if (status == 0) {
                // A designed session end (bounded session length) is a clean
                // exit, not a crash.
                self.window.title = [NSString stringWithFormat:@"OpenScanner — session ended (%ld s)",
                                                               (long)self.sessionSeconds];
            } else {
                [self surfaceError:[NSString stringWithFormat:@"Capture worker exited unexpectedly (status %d).",
                                                              status]];
            }
        });
    };
    @try {
        [self.worker launch];
    } @catch (NSException* e) {
        [self surfaceError:[NSString stringWithFormat:@"Could not start the capture worker: %@", e.reason]];
    }
}

- (void)surfaceError:(NSString*)message {
    self.window.title = @"OpenScanner — worker not running";
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"OpenScanner";
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    // A sheet, not runModal: a modal session blocks the app's own Quit
    // (the observed "worker already dead, quit does nothing" case).
    if (self.window != nil) {
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
    } else {
        [alert runModal];
    }
}

- (void)reloadPageIfNeeded {
    if (!self.reloadAfterRestart) return;
    self.reloadAfterRestart = NO;
    self.retryCount = 0;
    [self.webView loadRequest:[NSURLRequest requestWithURL:self.pageURL]];
}

- (void)restartWorker {
    if (self.worker != nil && self.worker.running) {
        self.restarting = YES;
        kill(self.worker.processIdentifier, SIGTERM);
    } else {
        [self startWorker];
        [self reloadPageIfNeeded];
    }
}

- (void)showSettings:(id)sender {
    (void)sender;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"OpenScanner Settings";
    alert.informativeText = @"Applied on OK; the worker restarts and the page reloads for a port change. "
                             @"Quit always runs the full teardown.";
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 56)];
    NSTextField* portLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 32, 80, 20)];
    portLabel.stringValue = @"Port";
    portLabel.editable = NO;
    portLabel.bezeled = NO;
    portLabel.drawsBackground = NO;
    NSTextField* portField = [[NSTextField alloc] initWithFrame:NSMakeRect(90, 30, 160, 24)];
    portField.stringValue = [NSString stringWithFormat:@"%ld", (long)self.port];
    NSTextField* secsLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 4, 80, 20)];
    secsLabel.stringValue = @"Session (s)";
    secsLabel.editable = NO;
    secsLabel.bezeled = NO;
    secsLabel.drawsBackground = NO;
    NSTextField* secsField = [[NSTextField alloc] initWithFrame:NSMakeRect(90, 2, 160, 24)];
    secsField.stringValue = [NSString stringWithFormat:@"%ld", (long)self.sessionSeconds];
    [accessory addSubview:portLabel];
    [accessory addSubview:portField];
    [accessory addSubview:secsLabel];
    [accessory addSubview:secsField];
    alert.accessoryView = accessory;

    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSInteger oldPort = self.port;
    NSInteger port = portField.stringValue.integerValue;
    NSInteger secs = secsField.stringValue.integerValue;
    if (port < 1 || port > 65535 || secs < 1 || secs > 3600) {
        [self surfaceError:@"Port must be 1–65535 and session length 1–3600 seconds."];
        return;
    }
    self.reloadAfterRestart = (oldPort != port);
    self.port = port;
    self.sessionSeconds = secs;
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:port forKey:@"OpenScannerPort"];
    [defaults setInteger:secs forKey:@"OpenScannerSessionSeconds"];
    self.pageURL = [NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%ld/", (long)port]];
    self.window.title = [NSString stringWithFormat:@"OpenScanner — port %ld, session %ld s",
                                                   (long)port, (long)secs];
    [self restartWorker];
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    (void)notification;
    // Install the menu before launch completes so Quit exists even if a
    // later startup path is slow or shows an error.
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Settings…" action:@selector(showSettings:) keyEquivalent:@","];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit OpenScanner" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [NSApp setMainMenu:menuBar];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSInteger savedPort = [defaults integerForKey:@"OpenScannerPort"];
    self.port = (savedPort >= 1 && savedPort <= 65535) ? savedPort : 8787;
    NSInteger savedSecs = [defaults integerForKey:@"OpenScannerSessionSeconds"];
    self.sessionSeconds = (savedSecs >= 1 && savedSecs <= 3600) ? savedSecs : 300;

    NSRect frame = NSMakeRect(0, 0, 1200, 800);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"OpenScanner";
    self.window.delegate = self;

    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    self.webView = [[WKWebView alloc] initWithFrame:frame configuration:config];
    self.webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.webView.navigationDelegate = self;
    self.window.contentView = self.webView;

    self.pageURL = [NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%ld/", (long)self.port]];
    self.retryCount = 0;
    self.window.title = [NSString stringWithFormat:@"OpenScanner — port %ld, session %ld s",
                                                   (long)self.port, (long)self.sessionSeconds];

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // Spawn after launch has completed; the navigation retries until the
    // listener answers. Any error dialog then happens in a fully launched
    // app (menu and Quit already present) rather than blocking the launch.
    dispatch_async(dispatch_get_main_queue(), ^{
        [self startWorker];
        [self.webView loadRequest:[NSURLRequest requestWithURL:self.pageURL]];
    });
}

// Diagnosis status (row #73): the room's hang could not be reproduced
// outside its launch. Controlled local runs (app launched directly with a
// fake worker, then `osascript -e 'tell application "OpenScanner" to quit'`)
// showed applicationShouldTerminate reached, kill(SIGTERM) delivered, and
// the app exiting once the worker did — for both a responsive worker and an
// already-dead one. The record's decisive clue (the app absent from Force
// Quit's own list) points at a launch/session without a window-server
// connection, which this environment cannot reproduce; the #65 rerun
// re-tests the original case. Hardening below is independent of that
// diagnosis: the menu exists from willFinishLaunching, startup errors are
// non-modal sheets, and the worker has an orphan guard.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    // Quit = graceful teardown: SIGTERM makes the worker break its loop and
    // run pause -> mesh -> stop -> session_destroy -> context_destroy.
    if (self.worker != nil && self.worker.running) {
        self.quitting = YES;
        kill(self.worker.processIdentifier, SIGTERM);
        return NSTerminateLater;
    }
    return NSTerminateNow;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [NSApp terminate:nil];
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        OpenScannerAppDelegate* delegate = [[OpenScannerAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
