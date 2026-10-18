// Programmatic macOS shell for the OpenScanner viewer (row A2).
// No storyboard, no asset catalog: an NSWindow + WKWebView pointed at the
// scan_server origin (A1 serves ui/ on the same port as the WebSocket).
// The vendor dylib is deliberately NOT bundled; the server resolves the
// installed scanner app itself and reports a missing SDK via its own
// dialog/status path.

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <cstdlib>

@interface OpenScannerAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@end

@implementation OpenScannerAppDelegate

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
