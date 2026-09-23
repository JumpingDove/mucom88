#import <AppKit/AppKit.h>

#include <filesystem>
#include <memory>
#include <string>

#include "editor/mml_document.h"
#include "editor/mucom_compile_service.h"

namespace {

NSString *StringFromUtf8(const std::string &text)
{
    NSString *result = [[NSString alloc]
        initWithBytes:text.data()
               length:text.size()
             encoding:NSUTF8StringEncoding];
    if (result != nil) return result;
    return [[NSString alloc]
        initWithBytes:text.data()
               length:text.size()
             encoding:NSISOLatin1StringEncoding];
}

void SetError(NSError **output, NSInteger code, NSString *message)
{
    if (output == nullptr) return;
    *output = [NSError errorWithDomain:@"org.mucom88.editor"
                                  code:code
                              userInfo:@{NSLocalizedDescriptionKey: message}];
}

NSMenuItem *AddMenuItem(NSMenu *menu, NSString *title, SEL action,
    NSString *keyEquivalent)
{
    NSMenuItem *item = [[NSMenuItem alloc]
        initWithTitle:title action:action keyEquivalent:keyEquivalent];
    [menu addItem:item];
    return item;
}

} // namespace

@interface MucomDocument : NSDocument <NSTextViewDelegate> {
    std::unique_ptr<mucom88::MmlDocument> _model;
    std::unique_ptr<mucom88::MucomCompileService> _compiler;
    NSTextView *_editorView;
    NSTextView *_messageView;
    NSTextField *_statusLabel;
    BOOL _updatingEditor;
}
@end

@implementation MucomDocument

- (instancetype)init
{
    self = [super init];
    if (self != nil) {
        _model = std::make_unique<mucom88::MmlDocument>();
        _compiler = std::make_unique<mucom88::MucomCompileService>();
        _updatingEditor = NO;
    }
    return self;
}

- (NSString *)windowNibName
{
    return nil;
}

- (void)makeWindowControllers
{
    const NSRect initialFrame = NSMakeRect(0, 0, 960, 700);
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:initialFrame
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskResizable |
                             NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.minSize = NSMakeSize(640, 420);
    window.title = self.displayName;

    NSView *content = [[NSView alloc] initWithFrame:initialFrame];
    window.contentView = content;

    NSButton *compileButton = [NSButton buttonWithTitle:@"Compile"
                                                target:self
                                                action:@selector(compileDocument:)];
    compileButton.keyEquivalent = @"r";
    compileButton.keyEquivalentModifierMask = NSEventModifierFlagCommand;

    _statusLabel = [NSTextField labelWithString:
        _compiler->IsReady() ? @"Ready" : @"MUCOM88 initialization failed"];
    _statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;

    NSStackView *controlBar = [NSStackView stackViewWithViews:
        @[compileButton, _statusLabel]];
    controlBar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    controlBar.alignment = NSLayoutAttributeCenterY;
    controlBar.spacing = 10.0;
    [_statusLabel setContentHuggingPriority:NSLayoutPriorityDefaultLow
                            forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSScrollView *editorScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    editorScroll.hasVerticalScroller = YES;
    editorScroll.hasHorizontalScroller = YES;
    editorScroll.autohidesScrollers = YES;
    editorScroll.borderType = NSBezelBorder;

    _editorView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 900, 480)];
    _editorView.delegate = self;
    _editorView.font = [NSFont monospacedSystemFontOfSize:13.0
                                                   weight:NSFontWeightRegular];
    _editorView.automaticQuoteSubstitutionEnabled = NO;
    _editorView.automaticDashSubstitutionEnabled = NO;
    _editorView.automaticTextReplacementEnabled = NO;
    _editorView.richText = NO;
    _editorView.allowsUndo = YES;
    _editorView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    editorScroll.documentView = _editorView;

    NSScrollView *messageScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    messageScroll.hasVerticalScroller = YES;
    messageScroll.autohidesScrollers = YES;
    messageScroll.borderType = NSBezelBorder;

    _messageView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 900, 140)];
    _messageView.editable = NO;
    _messageView.selectable = YES;
    _messageView.richText = NO;
    _messageView.font = [NSFont monospacedSystemFontOfSize:11.0
                                                    weight:NSFontWeightRegular];
    _messageView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    messageScroll.documentView = _messageView;

    NSStackView *layout = [NSStackView stackViewWithViews:
        @[controlBar, editorScroll, messageScroll]];
    layout.translatesAutoresizingMaskIntoConstraints = NO;
    layout.orientation = NSUserInterfaceLayoutOrientationVertical;
    layout.alignment = NSLayoutAttributeLeading;
    layout.spacing = 8.0;
    [content addSubview:layout];

    [NSLayoutConstraint activateConstraints:@[
        [layout.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12.0],
        [layout.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12.0],
        [layout.topAnchor constraintEqualToAnchor:content.topAnchor constant:12.0],
        [layout.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12.0],
        [controlBar.widthAnchor constraintEqualToAnchor:layout.widthAnchor],
        [editorScroll.widthAnchor constraintEqualToAnchor:layout.widthAnchor],
        [messageScroll.widthAnchor constraintEqualToAnchor:layout.widthAnchor],
        [messageScroll.heightAnchor constraintEqualToConstant:150.0]
    ]];

    _updatingEditor = YES;
    _editorView.string = StringFromUtf8(_model->Text());
    _updatingEditor = NO;

    NSWindowController *controller = [[NSWindowController alloc]
        initWithWindow:window];
    [self addWindowController:controller];
}

- (BOOL)syncModelFromEditor:(NSError **)error
{
    if (_editorView == nil) return YES;
    NSData *data = [_editorView.string dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil) {
        SetError(error, 1, @"The document cannot be converted to UTF-8.");
        return NO;
    }
    std::string text(static_cast<const char *>(data.bytes), data.length);
    std::string message;
    if (!_model->ReplaceText(std::move(text), &message)) {
        SetError(error, 2, StringFromUtf8(message));
        return NO;
    }
    return YES;
}

- (NSData *)dataOfType:(NSString *)typeName error:(NSError **)error
{
    (void)typeName;
    if (![self syncModelFromEditor:error]) return nil;
    const std::string &text = _model->Text();
    return [NSData dataWithBytes:text.data() length:text.size()];
}

- (BOOL)readFromData:(NSData *)data
               ofType:(NSString *)typeName
                error:(NSError **)error
{
    (void)typeName;
    NSString *decoded = [[NSString alloc]
        initWithData:data encoding:NSUTF8StringEncoding];
    if (decoded == nil) {
        SetError(error, 3,
            @"This initial editor version accepts UTF-8 MML documents only.");
        return NO;
    }

    const char *utf8 = decoded.UTF8String;
    std::string message;
    if (!_model->ReplaceText(utf8 != nullptr ? utf8 : "", &message)) {
        SetError(error, 4, StringFromUtf8(message));
        return NO;
    }

    if (_editorView != nil) {
        _updatingEditor = YES;
        _editorView.string = decoded;
        _updatingEditor = NO;
    }
    return YES;
}

- (void)textDidChange:(NSNotification *)notification
{
    if (_updatingEditor || notification.object != _editorView) return;
    NSError *error = nil;
    if ([self syncModelFromEditor:&error]) {
        [self updateChangeCount:NSChangeDone];
    } else {
        _statusLabel.stringValue = error.localizedDescription;
    }
}

- (void)selectLine:(int)line
{
    if (line <= 0 || _editorView == nil) return;
    NSString *text = _editorView.string;
    NSUInteger position = 0;
    for (int current = 1; current < line && position < text.length; ++current) {
        NSRange search = NSMakeRange(position, text.length - position);
        NSRange newline = [text rangeOfString:@"\n" options:0 range:search];
        if (newline.location == NSNotFound) return;
        position = NSMaxRange(newline);
    }
    NSRange tail = NSMakeRange(position, text.length - position);
    NSRange newline = [text rangeOfString:@"\n" options:0 range:tail];
    NSUInteger length = newline.location == NSNotFound
        ? text.length - position : newline.location - position;
    NSRange lineRange = NSMakeRange(position, length);
    [_editorView setSelectedRange:lineRange];
    [_editorView scrollRangeToVisible:lineRange];
    [_editorView.window makeFirstResponder:_editorView];
}

- (IBAction)compileDocument:(id)sender
{
    (void)sender;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) {
        _statusLabel.stringValue = error.localizedDescription;
        return;
    }
    if (!_compiler->IsReady()) {
        _statusLabel.stringValue = @"MUCOM88 initialization failed";
        return;
    }
    if (self.fileURL == nil) {
        _statusLabel.stringValue = @"Save the MML document before compiling";
        return;
    }

    mucom88::CompileRequest request = _model->MakeCompileRequest();
    request.source_path = self.fileURL.fileSystemRepresentation;
    request.resource_directory =
        std::filesystem::path(request.source_path).parent_path().string();

    _statusLabel.stringValue = @"Compiling…";
    mucom88::CompileResult result = _compiler->Compile(request);
    _messageView.string = StringFromUtf8(result.messages);
    if (result.Succeeded()) {
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"Compile succeeded (driver %d)", result.driver];
    } else {
        _statusLabel.stringValue = @"Compile failed";
        if (!result.diagnostics.empty()) {
            [self selectLine:result.diagnostics.front().line];
        }
    }
}

@end

@interface MucomAppDelegate : NSObject <NSApplicationDelegate>
@end


@implementation MucomAppDelegate

- (void)installMainMenu
{
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"Main Menu"];
    NSApp.mainMenu = mainMenu;

    NSMenuItem *applicationItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:applicationItem];
    NSMenu *applicationMenu = [[NSMenu alloc] initWithTitle:@"MUCOM88 Editor"];
    applicationItem.submenu = applicationMenu;
    NSMenuItem *aboutItem = AddMenuItem(applicationMenu,
        @"About MUCOM88 Editor", @selector(orderFrontStandardAboutPanel:), @"");
    aboutItem.target = NSApp;
    [applicationMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *quitItem = AddMenuItem(applicationMenu,
        @"Quit MUCOM88 Editor", @selector(terminate:), @"q");
    quitItem.target = NSApp;

    NSMenuItem *fileItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:fileItem];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    fileItem.submenu = fileMenu;
    AddMenuItem(fileMenu, @"New", @selector(newDocument:), @"n");
    AddMenuItem(fileMenu, @"Open…", @selector(openDocument:), @"o");
    [fileMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(fileMenu, @"Close", @selector(performClose:), @"w");
    AddMenuItem(fileMenu, @"Save", @selector(saveDocument:), @"s");
    NSMenuItem *saveAs = AddMenuItem(fileMenu, @"Save As…",
        @selector(saveDocumentAs:), @"S");
    saveAs.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagShift;

    NSMenuItem *editItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:editItem];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    editItem.submenu = editMenu;
    AddMenuItem(editMenu, @"Undo", @selector(undo:), @"z");
    NSMenuItem *redo = AddMenuItem(editMenu, @"Redo", @selector(redo:), @"Z");
    redo.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [editMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(editMenu, @"Cut", @selector(cut:), @"x");
    AddMenuItem(editMenu, @"Copy", @selector(copy:), @"c");
    AddMenuItem(editMenu, @"Paste", @selector(paste:), @"v");
    AddMenuItem(editMenu, @"Select All", @selector(selectAll:), @"a");
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [self installMainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

@end

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        MucomAppDelegate *delegate = [[MucomAppDelegate alloc] init];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
