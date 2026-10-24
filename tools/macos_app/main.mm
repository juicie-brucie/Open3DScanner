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
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// Quit-path instrumentation (row #79). Always-on, append-only, low volume:
// every step of the terminate path lands in one file both the app and the
// worker append to. Purely diagnostic — it does not change the path.
static void QuitTrace(const char* who, const char* fmt, ...) {
    const char* path = std::getenv("OPENSCANNER_QUIT_LOG");
    if (path == nullptr || path[0] == '\0') path = "/tmp/openscanner-quit.log";
    FILE* f = std::fopen(path, "a");
    if (f == nullptr) return;
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    std::fprintf(f, "%ld.%06d [%s pid %d] ", (long)tv.tv_sec, (int)tv.tv_usec, who, getpid());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fprintf(f, "\n");
    std::fclose(f);
}

@interface OpenScannerAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, WKNavigationDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@property(nonatomic, strong) NSTask* worker;
@property(nonatomic, assign) BOOL quitting;
@property(nonatomic, assign) BOOL terminationReplySent;
@property(nonatomic, assign) BOOL restarting;
@property(nonatomic, assign) BOOL reloadAfterRestart;
@property(nonatomic, assign) NSInteger retryCount;
@property(nonatomic, copy) NSURL* pageURL;
@property(nonatomic, assign) NSInteger port;
@property(nonatomic, assign) NSInteger sessionSeconds;
@end

@implementation OpenScannerAppDelegate

// The explicit override, or nil when unset. The missing-SDK dialog names
// only what was actually probed.
- (NSString*)libraryOverridePath {
    const char* env = std::getenv("OPENSCANNER_LIBRARY");
    return (env != nullptr && env[0] != '\0') ? [NSString stringWithUTF8String:env] : nil;
}

- (NSString*)installedLibraryPath {
    return @"/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib";
}

- (NSString*)resolveLibraryPath {
    NSString* override = [self libraryOverridePath];
    if (override != nil) {
        // An explicit override is authoritative: set-but-missing is fatal
        // (the missing-SDK dialog names it), never a silent fall-through.
        return [[NSFileManager defaultManager] fileExistsAtPath:override] ? override : @"";
    }
    NSString* installed = [self installedLibraryPath];
    return [[NSFileManager defaultManager] fileExistsAtPath:installed] ? installed : @"";
}

- (NSString*)resolveWorkerPath {
    NSString* bundled = [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent];
    bundled = [bundled stringByAppendingPathComponent:@"scan_server"];
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
    (void)webView; (void)navigation;
    QuitTrace("app", "didFailProvisionalNavigation: %s", error ? [[error localizedDescription] UTF8String] : "nil");
    [self retryPageLoadSoon];
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    (void)webView; (void)navigation;
    QuitTrace("app", "didFinishNavigation url=%s", [[webView.URL absoluteString] UTF8String]);
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

// Device-free UI mode: OPENSCANNER_SYNTHETIC=1 makes the app drive the
// worker's synthetic (animated-sphere) source instead of the scanner, so the
// whole app UI can be built and tried without a physical device or the SDK.
- (BOOL)syntheticMode {
    const char* env = std::getenv("OPENSCANNER_SYNTHETIC");
    return (env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0);
}

- (void)startWorker {
    NSString* workerPath = [self resolveWorkerPath];
    if (workerPath.length == 0) {
        [self surfaceError:@"scan_server not found in the app bundle or OPENSCANNER_SCAN_SERVER."];
        return;
    }
    NSString* portStr = [NSString stringWithFormat:@"%ld", (long)self.port];
    if ([self syntheticMode]) {
        if ([self isPortInUse:self.port]) {
            [self surfaceError:[NSString stringWithFormat:
                @"Port %ld is already in use by another process.", (long)self.port]];
            return;
        }
        self.worker = [[NSTask alloc] init];
        self.worker.launchPath = workerPath;
        self.worker.arguments = @[@"--synthetic", portStr];
        [self installWorkerTerminationHandler];
        @try {
            [self.worker launch];
            QuitTrace("app", "synthetic worker launched pid=%d", self.worker.processIdentifier);
        } @catch (NSException* e) {
            [self surfaceError:[NSString stringWithFormat:@"Could not start the worker: %@", e.reason]];
        }
        return;
    }
    NSString* library = [self resolveLibraryPath];
    if (library.length == 0) {
        NSString* override = [self libraryOverridePath];
        NSString* msg;
        if (override != nil) {
            // Only the override was probed; do not name a path we did not check.
            msg = [NSString stringWithFormat:
                @"OPENSCANNER_LIBRARY is set but does not exist:\n\n  • %@\n\n"
                 @"Correct it, or unset OPENSCANNER_LIBRARY to use %@.",
                override, [self installedLibraryPath]];
        } else {
            msg = [NSString stringWithFormat:
                @"CrealityScan's liborbbec_scan.dylib was not found.\n\nSearched:\n  • %@\n\n"
                 @"Install CrealityScan, or set OPENSCANNER_LIBRARY to the dylib.",
                [self installedLibraryPath]];
        }
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
    [self installWorkerTerminationHandler];
    @try {
        [self.worker launch];
        QuitTrace("app", "worker launched pid=%d path=%s", self.worker.processIdentifier,
                  [workerPath UTF8String]);
    } @catch (NSException* e) {
        QuitTrace("app", "worker launch threw: %s", [e.reason UTF8String]);
        [self surfaceError:[NSString stringWithFormat:@"Could not start the capture worker: %@", e.reason]];
    }
}

// The worker-exit handler, shared by the live and synthetic launch paths:
// completes a pending quit, restarts on a reconnect, or reports an
// unexpected exit. NSTask fires it off the main thread, so every AppKit
// touch hops back to the main queue.
- (void)installWorkerTerminationHandler {
    __weak OpenScannerAppDelegate* weakSelf = self;
    self.worker.terminationHandler = ^(NSTask* task) {
        int status = task.terminationStatus;
        QuitTrace("app", "worker terminationHandler: pid=%d status=%d reason=%ld", task.processIdentifier, status,
                  (long)task.terminationReason);
        dispatch_async(dispatch_get_main_queue(), ^{
            OpenScannerAppDelegate* self = weakSelf;
            if (self == nil) return;
            QuitTrace("app", "terminationHandler on main queue; quitting=%d restarting=%d", self.quitting,
                      self.restarting);
            if (self.quitting) {
                if (self.terminationReplySent) {
                    QuitTrace("app", "worker exit after termination reply; ignoring");
                } else {
                    self.terminationReplySent = YES;
                    QuitTrace("app", "replying NSTerminateNow (worker exited)");
                    [NSApp replyToApplicationShouldTerminate:YES];
                }
            } else if (self.restarting) {
                self.restarting = NO;
                [self startWorker];
                [self reloadPageIfNeeded];
            } else if (status == 0) {
                // The scan ran for the app-open cycle and the worker exited
                // cleanly (operator "finish", or the stream ending) — a clean
                // exit, not a crash.
                self.window.title = @"OpenScanner — scan ended";
            } else {
                [self surfaceError:[NSString stringWithFormat:@"Capture worker exited unexpectedly (status %d).",
                                                              status]];
            }
        });
    };
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
    // Wait for the restarted worker's listener so the splash stays up rather
    // than flashing an error page during the new bring-up.
    [self loadPageWhenServing:0];
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
                             @"The scan runs for the whole app-open cycle; Quit always runs the full teardown.";
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 28)];
    NSTextField* portLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 4, 80, 20)];
    portLabel.stringValue = @"Port";
    portLabel.editable = NO;
    portLabel.bezeled = NO;
    portLabel.drawsBackground = NO;
    NSTextField* portField = [[NSTextField alloc] initWithFrame:NSMakeRect(90, 2, 160, 24)];
    portField.stringValue = [NSString stringWithFormat:@"%ld", (long)self.port];
    [accessory addSubview:portLabel];
    [accessory addSubview:portField];
    alert.accessoryView = accessory;

    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSInteger oldPort = self.port;
    NSInteger port = portField.stringValue.integerValue;
    if (port < 1 || port > 65535) {
        [self surfaceError:@"Port must be 1–65535."];
        return;
    }
    self.reloadAfterRestart = (oldPort != port);
    self.port = port;
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:port forKey:@"OpenScannerPort"];
    self.pageURL = [NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%ld/", (long)port]];
    self.window.title = [NSString stringWithFormat:@"OpenScanner — port %ld", (long)port];
    [self restartWorker];
}

// "Connect / Refresh scanner": tear the worker down and start it again, which
// re-runs device discovery and SDK bring-up. This is the honest way to
// re-acquire a scanner that was absent or unplugged at launch; the graceful
// SIGTERM teardown runs first so the SDK is never left mid-session.
- (void)reconnectScanner:(id)sender {
    (void)sender;
    self.reloadAfterRestart = YES;
    self.window.title = @"OpenScanner — reconnecting…";
    [self.webView loadHTMLString:[self startingSplashHtml] baseURL:nil];
    [self restartWorker];
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    (void)notification;
    QuitTrace("app", "applicationWillFinishLaunching");
    // Install the menu before launch completes so Quit exists even if a
    // later startup path is slow or shows an error.
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Settings…" action:@selector(showSettings:) keyEquivalent:@","];
    // Reconnect = re-run the worker's device bring-up (the "connect / refresh
    // scanner" action). The web UI also has a button for it, but the real
    // device re-open is the app's job since it supervises the worker process.
    [appMenu addItemWithTitle:@"Reconnect Scanner" action:@selector(reconnectScanner:) keyEquivalent:@"r"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit OpenScanner" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [NSApp setMainMenu:menuBar];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    QuitTrace("app", "applicationDidFinishLaunching");
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSInteger savedPort = [defaults integerForKey:@"OpenScannerPort"];
    self.port = (savedPort >= 1 && savedPort <= 65535) ? savedPort : 8787;
    // 0 = unbounded: the worker streams for the whole app-open cycle and only
    // stops on operator "finish" or on Quit (graceful SIGTERM teardown).
    self.sessionSeconds = 0;

    NSRect frame = NSMakeRect(0, 0, 1200, 800);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"OpenScanner";
    self.window.delegate = self;

    // Dark background everywhere so the window is never a bare white panel —
    // not before the first paint, and not if a page load fails mid-retry. The
    // page's own background is #111; match it here. drawsBackground=NO lets the
    // dark window show through the WebKit view during loads (the private KVC
    // key is the long-standing way to do this on macOS).
    NSColor* dark = [NSColor colorWithCalibratedRed:0.067 green:0.067 blue:0.071 alpha:1.0];
    self.window.backgroundColor = dark;

    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    self.webView = [[WKWebView alloc] initWithFrame:frame configuration:config];
    self.webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.webView.navigationDelegate = self;
    @try { [self.webView setValue:@NO forKey:@"drawsBackground"]; } @catch (NSException* e) { (void)e; }
    if ([self.webView respondsToSelector:@selector(setUnderPageBackgroundColor:)]) {
        self.webView.underPageBackgroundColor = dark;
    }
    self.window.contentView = self.webView;

    self.pageURL = [NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%ld/", (long)self.port]];
    self.retryCount = 0;
    self.window.title = [NSString stringWithFormat:@"OpenScanner — port %ld", (long)self.port];

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // A dark "starting" splash shown immediately, so the operator sees branded
    // feedback during the seconds of scanner/SDK bring-up before the worker's
    // page is served — rather than a blank window.
    [self.webView loadHTMLString:[self startingSplashHtml] baseURL:nil];

    // Spawn after launch has completed, then load the page only once the
    // worker is actually listening — the splash stays up during bring-up
    // instead of flashing WebKit's error page on each early retry.
    dispatch_async(dispatch_get_main_queue(), ^{
        [self startWorker];
        [self loadPageWhenServing:0];
    });
}

// Poll the loopback until the worker's listener answers, then load the page.
// Keeps the dark splash visible meanwhile. Bounded (~2 min) like the WebKit
// retry path; if the worker died, startWorker has already surfaced an error.
- (void)loadPageWhenServing:(NSInteger)attempt {
    if (self.pageURL == nil) return;
    if ([self isPortInUse:self.port]) {
        QuitTrace("app", "worker is serving (after %ld polls); loading page", (long)attempt);
        self.retryCount = 0;
        [self.webView loadRequest:[NSURLRequest requestWithURL:self.pageURL]];
        return;
    }
    if (attempt >= 240) return;  // ~2 min at 0.5 s
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [self loadPageWhenServing:attempt + 1];
    });
}

- (NSString*)startingSplashHtml {
    return @"<!doctype html><html><head><meta charset='utf-8'><style>"
            "html,body{margin:0;height:100%;background:#111;color:#eee;"
            "font-family:-apple-system,Segoe UI,Helvetica,Arial,sans-serif;"
            "display:flex;align-items:center;justify-content:center;}"
            ".box{text-align:center;opacity:.85}.t{font-size:15px;font-weight:600}"
            ".s{font-size:12px;color:#8aa;margin-top:6px}</style></head><body>"
            "<div class='box'><div class='t'>Starting OpenScanner…</div>"
            "<div class='s'>connecting to the scanner</div></div></body></html>";
}

// Instrumented rerun (row #73b): the graceful path starts correctly
// (applicationShouldTerminate -> kill(SIGTERM) -> NSTerminateLater) but
// windowWillClose fired ~8 s later and re-entered terminate:, bypassing the
// wait and exiting before the worker finished. Guard the window-close path
// so termination is idempotent, and bound a worker that never exits (the
// #89 context_destroy hang) with a generous deadline that keeps normal
// 11-19 s tails intact.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    QuitTrace("app", "applicationShouldTerminate entered; worker=%p running=%d quitting=%d",
              (__bridge void*)self.worker, (self.worker != nil && self.worker.running), self.quitting);
    if (self.quitting) {
        // Re-entered from windowWillClose or a second quit: do not start a
        // second wait or re-kill.
        QuitTrace("app", "termination already in progress; returning NSTerminateLater (idempotent)");
        return NSTerminateLater;
    }
    // Quit = graceful teardown: SIGTERM makes the worker break its loop and
    // run pause -> mesh -> stop -> session_destroy -> context_destroy.
    if (self.worker != nil && self.worker.running) {
        self.quitting = YES;
        int rc = kill(self.worker.processIdentifier, SIGTERM);
        QuitTrace("app", "kill(SIGTERM, pid=%d) -> %d errno=%d", self.worker.processIdentifier, rc, errno);
        // Last-resort deadline: normal teardown is ~11-19 s; a worker stuck
        // in context_destroy (#89) would otherwise hang the app forever.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(120 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            if (!self.quitting || self.terminationReplySent) return;
            if (self.worker != nil && self.worker.running) {
                QuitTrace("app", "termination deadline reached; SIGKILL pid=%d", self.worker.processIdentifier);
                kill(self.worker.processIdentifier, SIGKILL);
            }
            self.terminationReplySent = YES;
            QuitTrace("app", "deadline: replying NSTerminateNow");
            [NSApp replyToApplicationShouldTerminate:YES];
        });
        QuitTrace("app", "returning NSTerminateLater");
        return NSTerminateLater;
    }
    QuitTrace("app", "returning NSTerminateNow (no running worker)");
    return NSTerminateNow;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    QuitTrace("app", "applicationWillTerminate (process about to exit)");
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (self.quitting) {
        // Termination is already waiting on the worker; closing the window
        // must not re-enter terminate: (the 8 s bypass in the instrumented
        // rerun). The wait/terminationHandler owns the exit.
        QuitTrace("app", "windowWillClose during termination; ignoring (idempotent)");
        return;
    }
    QuitTrace("app", "windowWillClose -> terminate:");
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
        QuitTrace("app", "main: [NSApp run] returned; process about to exit");
    }
    return 0;
}
