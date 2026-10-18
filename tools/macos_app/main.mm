// Programmatic macOS shell for the OpenScanner viewer (rows A2 + A3).
// No storyboard, no asset catalog: an NSWindow + WKWebView pointed at the
// worker's origin (A1 serves ui/ on the same port as the WebSocket).
// The app spawns and supervises the capture worker, and quitting sends it
// SIGTERM so the worker runs its graceful pause/stop/destroy teardown.
// The vendor dylib is deliberately NOT bundled; the worker resolves the
// installed scanner app itself and reports a missing SDK.

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <csignal>
#include <cstdlib>
#include <string>

@interface OpenScannerAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@property(nonatomic, strong) NSTask* worker;
@property(nonatomic, assign) BOOL quitting;
@end

@implementation OpenScannerAppDelegate

- (NSString*)resolveLibraryPath {
    const char* env = std::getenv("OPENSCANNER_LIBRARY");
    if (env != nullptr && env[0] != '\0') return [NSString stringWithUTF8String:env];
    NSArray<NSString*>* candidates = @[
        @"/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib",
    ];
    for (NSString* c in candidates) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:c]) return c;
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

- (void)startWorker {
    NSString* workerPath = [self resolveWorkerPath];
    if (workerPath.length == 0) {
        [self surfaceError:@"scan_server not found in the app bundle or OPENSCANNER_SCAN_SERVER."];
        return;
    }
    NSString* library = [self resolveLibraryPath];
    if (library.length == 0) {
        [self surfaceError:@"CrealityScan's liborbbec_scan.dylib was not found. Install CrealityScan "
                           @"or set OPENSCANNER_LIBRARY."];
        return;
    }
    const char* port_env = std::getenv("OPENSCANNER_PORT");
    NSString* port = port_env != nullptr ? [NSString stringWithUTF8String:port_env] : @"8787";
    NSString* seconds = @"300";

    self.worker = [[NSTask alloc] init];
    self.worker.launchPath = workerPath;
    self.worker.arguments = @[@"--live-worker", port, library, seconds];
    __weak OpenScannerAppDelegate* weakSelf = self;
    self.worker.terminationHandler = ^(NSTask* task) {
        OpenScannerAppDelegate* self = weakSelf;
        if (self == nil) return;
        if (self.quitting) {
            [NSApp replyToApplicationShouldTerminate:YES];
        } else {
            [self surfaceError:[NSString stringWithFormat:@"Capture worker exited unexpectedly (status %d).",
                                                          task.terminationStatus]];
        }
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
    [alert runModal];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
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
    self.window.contentView = self.webView;

    const char* port_env = std::getenv("OPENSCANNER_PORT");
    NSString* port = port_env != nullptr ? [NSString stringWithUTF8String:port_env] : @"8787";
    NSURL* url = [NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%@/", port]];
    [self.webView loadRequest:[NSURLRequest requestWithURL:url]];

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    [self startWorker];
}

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
