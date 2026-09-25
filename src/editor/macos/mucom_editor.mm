#import <AppKit/AppKit.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "editor/application_services.h"
#include "editor/document_service.h"
#include "editor/editor_command.h"
#include "editor/recovery_service.h"

namespace {

NSString *StringFromUtf8(const std::string &text)
{
    NSString *result = [[NSString alloc]
        initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return result != nil ? result : @"";
}

void ApplyEditorTextAppearance(NSTextView *textView)
{
    textView.drawsBackground = YES;
    textView.backgroundColor = NSColor.textBackgroundColor;
    textView.textColor = NSColor.labelColor;
    textView.insertionPointColor = NSColor.labelColor;
    const NSRange allText = NSMakeRange(0, textView.string.length);
    if (allText.length > 0) {
        [textView.textStorage addAttribute:NSForegroundColorAttributeName
            value:NSColor.labelColor range:allText];
    }
}

void SetError(NSError **output, NSInteger code, NSString *message)
{
    if (output == nullptr) return;
    *output = [NSError errorWithDomain:@"org.mucom88.editor" code:code
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

std::string ApplicationSupportRoot()
{
    NSString *base = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    if (base == nil) return {};
    return (std::filesystem::path(base.fileSystemRepresentation) /
        "org.mucom88.editor").string();
}

std::string RecoveryRoot()
{
    return (std::filesystem::path(ApplicationSupportRoot()) / "Recovery").string();
}

void CreateBackup(NSURL *url, mucom88::DocumentId documentId)
{
    if (url == nil || !url.isFileURL ||
        ![[NSFileManager defaultManager] fileExistsAtPath:url.path]) return;
    const std::filesystem::path directory =
        std::filesystem::path(ApplicationSupportRoot()) / "Backups" /
        ("document-" + std::to_string(documentId));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path destination = directory /
        (std::to_string(timestamp) + "-" +
            std::filesystem::path(url.fileSystemRepresentation).filename().string());
    std::filesystem::copy_file(url.fileSystemRepresentation, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) return;
    std::vector<std::filesystem::directory_entry> backups;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        if (!error && entry.is_regular_file()) backups.push_back(entry);
    }
    std::sort(backups.begin(), backups.end(), [](const auto &left, const auto &right) {
        return left.last_write_time() > right.last_write_time();
    });
    for (std::size_t index = 10; index < backups.size(); ++index) {
        std::filesystem::remove(backups[index].path(), error);
        error.clear();
    }
}

} // namespace

static BOOL OpenDroppedFiles(id<NSDraggingInfo> sender)
{
    NSArray<NSURL *> *urls = [sender.draggingPasteboard
        readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (urls.count == 0) return NO;
    for (NSURL *url in urls) {
        [NSDocumentController.sharedDocumentController
            openDocumentWithContentsOfURL:url display:YES
            completionHandler:^(NSDocument *document, BOOL wasOpen, NSError *error) {
                (void)document;
                (void)wasOpen;
                if (error != nil) [NSApp presentError:error];
            }];
    }
    return YES;
}

@interface LineNumberRulerView : NSRulerView {
    __weak NSTextView *_textView;
    id _boundsObserver;
    std::vector<NSUInteger> _lineStarts;
}
- (instancetype)initWithScrollView:(NSScrollView *)scrollView
                          textView:(NSTextView *)textView;
- (void)reloadLineNumbers;
@end

@implementation LineNumberRulerView

- (void)setFrameSize:(NSSize)newSize
{
    // AppKit may propose the full scroll-view width for a programmatic ruler.
    // Clamp it here so the ruler cannot cover the editor document view.
    newSize.width = self.ruleThickness > 0.0 ? self.ruleThickness : 46.0;
    [super setFrameSize:newSize];
}

- (instancetype)initWithScrollView:(NSScrollView *)scrollView
                          textView:(NSTextView *)textView
{
    self = [super initWithScrollView:scrollView orientation:NSVerticalRuler];
    if (self != nil) {
        _textView = textView;
        self.clientView = textView;
        self.ruleThickness = 46.0;
        self.frame = NSMakeRect(0.0, 0.0, self.ruleThickness,
            NSHeight(scrollView.bounds));
        self.autoresizingMask = NSViewHeightSizable;
        scrollView.contentView.postsBoundsChangedNotifications = YES;
        __weak LineNumberRulerView *weakSelf = self;
        _boundsObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSViewBoundsDidChangeNotification
                        object:scrollView.contentView
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *note) {
                        (void)note;
                        [weakSelf setNeedsDisplay:YES];
                    }];
        [self reloadLineNumbers];
    }
    return self;
}

- (void)reloadLineNumbers
{
    NSString *text = _textView.string ?: @"";
    _lineStarts.clear();
    _lineStarts.push_back(0);
    NSUInteger position = 0;
    while (position < text.length) {
        NSRange newline = [text rangeOfString:@"\n" options:0
            range:NSMakeRange(position, text.length - position)];
        if (newline.location == NSNotFound) break;
        position = NSMaxRange(newline);
        _lineStarts.push_back(position);
    }
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    if (_boundsObserver != nil) {
        [[NSNotificationCenter defaultCenter] removeObserver:_boundsObserver];
    }
}

- (void)drawHashMarksAndLabelsInRect:(NSRect)rect
{
    [[NSColor controlBackgroundColor] setFill];
    NSRect background = NSIntersectionRect(rect,
        NSMakeRect(NSMinX(self.bounds), NSMinY(self.bounds),
            self.ruleThickness, NSHeight(self.bounds)));
    NSRectFill(background);
    NSTextView *textView = _textView;
    if (textView == nil) return;
    NSString *text = textView.string ?: @"";
    NSLayoutManager *layout = textView.layoutManager;
    NSTextContainer *container = textView.textContainer;
    if (layout == nil || container == nil) return;

    NSFont *normalFont = [NSFont monospacedDigitSystemFontOfSize:10.0
        weight:NSFontWeightRegular];
    NSFont *selectedFont = [NSFont monospacedDigitSystemFontOfSize:10.0
        weight:NSFontWeightSemibold];
    NSDictionary *normal = @{
        NSFontAttributeName: normalFont,
        NSForegroundColorAttributeName: NSColor.secondaryLabelColor
    };
    NSDictionary *selected = @{
        NSFontAttributeName: selectedFont,
        NSForegroundColorAttributeName: NSColor.labelColor
    };
    NSRect textVisibleRect = textView.visibleRect;
    NSRect rulerViewport = [self convertRect:textVisibleRect fromView:textView];
    rulerViewport.origin.x = NSMinX(self.bounds);
    rulerViewport.size.width = self.ruleThickness;
    rulerViewport = NSIntersectionRect(rulerViewport, self.bounds);
    if (NSIsEmptyRect(rulerViewport)) return;

    const NSUInteger selectedLocation = textView.selectedRange.location;
    NSRange visibleCharacters = NSMakeRange(0, text.length);
    if (layout.numberOfGlyphs > 0) {
        const NSPoint containerOrigin = textView.textContainerOrigin;
        textVisibleRect.origin.x -= containerOrigin.x;
        textVisibleRect.origin.y -= containerOrigin.y;
        NSRange visibleGlyphs = [layout glyphRangeForBoundingRect:textVisibleRect
            inTextContainer:container];
        visibleCharacters = [layout characterRangeForGlyphRange:visibleGlyphs
            actualGlyphRange:nullptr];
    }
    auto first = std::upper_bound(_lineStarts.begin(), _lineStarts.end(),
        visibleCharacters.location);
    std::size_t lineIndex = first == _lineStarts.begin() ? 0 :
        static_cast<std::size_t>(first - _lineStarts.begin() - 1);
    const NSUInteger visibleEnd = NSMaxRange(visibleCharacters);
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(rulerViewport);
    for (; lineIndex < _lineStarts.size(); ++lineIndex) {
        const NSUInteger lineStart = _lineStarts[lineIndex];
        if (lineStart > visibleEnd && lineIndex > 0) break;
        NSUInteger start = 0;
        NSUInteger end = 0;
        NSUInteger contentsEnd = 0;
        [text getLineStart:&start end:&end contentsEnd:&contentsEnd
                  forRange:NSMakeRange(lineStart, 0)];
        NSRect fragment = NSZeroRect;
        CGFloat baselineInContainer = 0.0;
        if (lineStart < text.length && layout.numberOfGlyphs > 0) {
            const NSUInteger glyph = [layout glyphIndexForCharacterAtIndex:lineStart];
            fragment = [layout lineFragmentRectForGlyphAtIndex:glyph
                                                effectiveRange:nullptr];
            baselineInContainer = NSMinY(fragment) +
                [layout locationForGlyphAtIndex:glyph].y;
        } else {
            fragment = layout.extraLineFragmentRect;
            NSFont *editorFont = textView.font ?: [NSFont systemFontOfSize:13.0];
            baselineInContainer = NSMinY(fragment) +
                [layout defaultBaselineOffsetForFont:editorFont];
        }
        const NSPoint containerOrigin = textView.textContainerOrigin;
        NSRect fragmentInTextView = NSOffsetRect(fragment,
            containerOrigin.x, containerOrigin.y);
        NSRect fragmentInRuler = [self convertRect:fragmentInTextView
                                          fromView:textView];
        fragmentInRuler.origin.x = NSMinX(self.bounds);
        fragmentInRuler.size.width = self.ruleThickness;
        if (NSIntersectsRect(fragmentInRuler, rulerViewport)) {
            const BOOL current = selectedLocation >= start &&
                selectedLocation <= std::max(start, contentsEnd);
            NSString *label = [NSString stringWithFormat:@"%lu",
                static_cast<unsigned long>(lineIndex + 1)];
            NSDictionary *attributes = current ? selected : normal;
            NSFont *labelFont = current ? selectedFont : normalFont;
            const NSSize size = [label sizeWithAttributes:attributes];
            const NSPoint baselineInTextView = NSMakePoint(0.0,
                baselineInContainer + containerOrigin.y);
            const CGFloat baselineInRuler = [self
                convertPoint:baselineInTextView fromView:textView].y;
            const CGFloat labelY = baselineInRuler -
                [layout defaultBaselineOffsetForFont:labelFont];
            [label drawAtPoint:NSMakePoint(
                self.ruleThickness - size.width - 6.0, labelY)
                withAttributes:attributes];
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

@end

@interface MucomDropView : NSView
@end

@implementation MucomDropView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    return self;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    return [sender.draggingPasteboard canReadObjectForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}]
        ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    return OpenDroppedFiles(sender);
}

@end

@interface MucomTextView : NSTextView
@end

@implementation MucomTextView

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    if ([sender.draggingPasteboard canReadObjectForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}]) {
        return NSDragOperationCopy;
    }
    return [super draggingEntered:sender];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    if (OpenDroppedFiles(sender)) return YES;
    return [super performDragOperation:sender];
}

@end

@interface MucomDocument : NSDocument <NSTextViewDelegate> {
    std::shared_ptr<mucom88::DocumentService> _model;
    std::shared_ptr<mucom88::ApplicationServices> _services;
    mucom88::OperationHandle _compileOperation;
    std::optional<mucom88::SavePlan> _pendingSavePlan;
    std::shared_ptr<std::atomic<bool>> _recoveryCancelled;
    NSTextView *_editorView;
    NSTextView *_messageView;
    NSTextField *_statusLabel;
    LineNumberRulerView *_lineRuler;
    NSTimer *_recoveryTimer;
    NSString *_restoredRecoveryPath;
    BOOL _updatingEditor;
}
- (IBAction)compileDocument:(id)sender;
- (IBAction)goToLine:(id)sender;
- (IBAction)changeEncoding:(id)sender;
- (BOOL)restoreRecoveryAtPath:(NSString *)path error:(NSError **)error;
@end

@implementation MucomDocument

- (instancetype)init
{
    self = [super init];
    if (self != nil) {
        _model = std::make_shared<mucom88::DocumentService>();
        _model->NewDocument();
        _services = mucom88::SharedApplicationServices();
        _recoveryCancelled = std::make_shared<std::atomic<bool>>(false);
    }
    return self;
}

- (NSString *)windowNibName { return nil; }

- (void)updateStatus
{
    if (_statusLabel == nil) return;
    const mucom88::DocumentSnapshot snapshot = _model->Snapshot();
    NSString *guess = snapshot.encoding_was_guessed ? @" (guessed)" : @"";
    _statusLabel.stringValue = [NSString stringWithFormat:@"%s • %s%@ • %s",
        mucom88::DocumentKindName(snapshot.kind),
        mucom88::TextEncodingName(snapshot.encoding), guess,
        mucom88::NewlineStyleName(snapshot.newline)];
}

- (void)makeWindowControllers
{
    const NSRect initialFrame = NSMakeRect(0, 0, 960, 700);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:initialFrame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
        backing:NSBackingStoreBuffered defer:NO];
    window.minSize = NSMakeSize(640, 420);

    MucomDropView *content = [[MucomDropView alloc] initWithFrame:initialFrame];
    window.contentView = content;

    NSButton *compileButton = [NSButton buttonWithTitle:@"Compile"
        target:self action:@selector(compileDocument:)];
    compileButton.keyEquivalent = @"r";
    compileButton.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    _statusLabel = [NSTextField labelWithString:@""];
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
    _editorView = [[MucomTextView alloc] initWithFrame:NSMakeRect(0, 0, 900, 480)];
    [_editorView registerForDraggedTypes:@[NSPasteboardTypeFileURL,
        NSPasteboardTypeString]];
    _editorView.delegate = self;
    _editorView.font = [NSFont monospacedSystemFontOfSize:13.0
        weight:NSFontWeightRegular];
    _editorView.automaticQuoteSubstitutionEnabled = NO;
    _editorView.automaticDashSubstitutionEnabled = NO;
    _editorView.automaticTextReplacementEnabled = NO;
    _editorView.richText = NO;
    _editorView.allowsUndo = YES;
    _editorView.usesFindBar = YES;
    _editorView.incrementalSearchingEnabled = YES;
    _editorView.minSize = NSMakeSize(0.0, 0.0);
    _editorView.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    _editorView.verticallyResizable = YES;
    _editorView.horizontallyResizable = YES;
    _editorView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _editorView.textContainer.widthTracksTextView = NO;
    _editorView.textContainer.containerSize = NSMakeSize(FLT_MAX, FLT_MAX);
    editorScroll.documentView = _editorView;
    _lineRuler = [[LineNumberRulerView alloc]
        initWithScrollView:editorScroll textView:_editorView];
    editorScroll.verticalRulerView = _lineRuler;
    editorScroll.hasVerticalRuler = YES;
    editorScroll.rulersVisible = YES;

    NSScrollView *messageScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    messageScroll.hasVerticalScroller = YES;
    messageScroll.autohidesScrollers = YES;
    messageScroll.borderType = NSBezelBorder;
    _messageView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 900, 140)];
    _messageView.delegate = self;
    _messageView.editable = NO;
    _messageView.selectable = YES;
    _messageView.richText = YES;
    _messageView.font = [NSFont monospacedSystemFontOfSize:11.0
        weight:NSFontWeightRegular];
    _messageView.drawsBackground = YES;
    _messageView.backgroundColor = NSColor.textBackgroundColor;
    _messageView.textColor = NSColor.labelColor;
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
    _editorView.string = StringFromUtf8(_model->Snapshot().utf8_text);
    ApplyEditorTextAppearance(_editorView);
    _updatingEditor = NO;
    [_lineRuler reloadLineNumbers];
    [self updateStatus];
    [self addWindowController:[[NSWindowController alloc] initWithWindow:window]];
}

- (BOOL)syncModelFromEditor:(NSError **)error
{
    if (_editorView == nil) return YES;
    NSData *data = [_editorView.string dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil) {
        SetError(error, 1, @"The document cannot be converted to UTF-8.");
        return NO;
    }
    const auto result = _model->ReplaceText(std::string(
        static_cast<const char *>(data.bytes), data.length));
    if (!result.Succeeded()) {
        SetError(error, 2, StringFromUtf8(result.error.message));
        return NO;
    }
    return YES;
}

- (NSData *)dataOfType:(NSString *)typeName error:(NSError **)error
{
    (void)typeName;
    if (_pendingSavePlan.has_value()) {
        const std::string &bytes = _pendingSavePlan->bytes;
        return [NSData dataWithBytes:bytes.data() length:bytes.size()];
    }
    if (![self syncModelFromEditor:error]) return nil;
    const auto encoded = _model->EncodedData();
    if (!encoded.Succeeded()) {
        SetError(error, 3, StringFromUtf8(encoded.error.message));
        return nil;
    }
    return [NSData dataWithBytes:encoded.value.data() length:encoded.value.size()];
}

- (BOOL)readFromURL:(NSURL *)url ofType:(NSString *)typeName error:(NSError **)error
{
    (void)typeName;
    NSData *data = [NSData dataWithContentsOfURL:url options:0 error:error];
    if (data == nil) return NO;
    const auto opened = _model->OpenData(std::string(
        static_cast<const char *>(data.bytes), data.length),
        url.fileSystemRepresentation);
    if (!opened.Succeeded()) {
        SetError(error, 4, StringFromUtf8(opened.error.message));
        return NO;
    }
    if (_editorView != nil) {
        _updatingEditor = YES;
        _editorView.string = StringFromUtf8(opened.value.utf8_text);
        ApplyEditorTextAppearance(_editorView);
        _updatingEditor = NO;
        [_lineRuler reloadLineNumbers];
        [self updateStatus];
    }
    return YES;
}

- (BOOL)readFromData:(NSData *)data ofType:(NSString *)typeName
                error:(NSError **)error
{
    (void)typeName;
    const std::string path = self.fileURL == nil ? std::string() :
        self.fileURL.fileSystemRepresentation;
    const auto opened = _model->OpenData(std::string(
        static_cast<const char *>(data.bytes), data.length), path);
    if (!opened.Succeeded()) {
        SetError(error, 5, StringFromUtf8(opened.error.message));
        return NO;
    }
    return YES;
}

- (BOOL)writeSafelyToURL:(NSURL *)url ofType:(NSString *)typeName
         forSaveOperation:(NSSaveOperationType)operation error:(NSError **)error
{
    if (![self syncModelFromEditor:error]) return NO;
    const auto snapshot = _model->Snapshot();
    const auto plan = _model->PrepareSave(
        url.fileSystemRepresentation, snapshot.encoding);
    if (!plan.Succeeded()) {
        SetError(error, 6, StringFromUtf8(plan.error.message));
        return NO;
    }
    CreateBackup(url, snapshot.document_id);
    _pendingSavePlan = plan.value;
    const BOOL succeeded = [super writeSafelyToURL:url ofType:typeName
        forSaveOperation:operation error:error];
    _pendingSavePlan.reset();
    if (!succeeded) return NO;
    const auto acknowledged = _model->AcknowledgeSave(plan.value);
    if (!acknowledged.Succeeded()) {
        SetError(error, 7, StringFromUtf8(acknowledged.error.message));
        return NO;
    }
    mucom88::RecoveryService recovery(RecoveryRoot());
    recovery.RemoveDocument(snapshot.document_id);
    if (_restoredRecoveryPath != nil) {
        recovery.RemoveEntry(_restoredRecoveryPath.fileSystemRepresentation);
        _restoredRecoveryPath = nil;
    }
    [self updateStatus];
    return YES;
}

- (void)scheduleRecovery
{
    [_recoveryTimer invalidate];
    __weak MucomDocument *weakSelf = self;
    _recoveryTimer = [NSTimer scheduledTimerWithTimeInterval:5.0 repeats:NO
        block:^(NSTimer *timer) {
            (void)timer;
            MucomDocument *document = weakSelf;
            if (document == nil || !document->_model->Snapshot().IsModified()) return;
            auto model = document->_model;
            auto cancelled = document->_recoveryCancelled;
            const std::string root = RecoveryRoot();
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                if (cancelled->load(std::memory_order_acquire)) return;
                mucom88::RecoveryService recovery(root);
                const auto saved = recovery.SaveSnapshot(*model);
                if (cancelled->load(std::memory_order_acquire)) {
                    recovery.RemoveDocument(model->Snapshot().document_id);
                    return;
                }
                if (!saved.Succeeded()) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        MucomDocument *current = weakSelf;
                        if (current != nil) current->_statusLabel.stringValue =
                            StringFromUtf8(saved.error.message);
                    });
                }
            });
        }];
}

- (void)textDidChange:(NSNotification *)notification
{
    if (_updatingEditor || notification.object != _editorView) return;
    NSError *error = nil;
    if ([self syncModelFromEditor:&error]) {
        NSDocumentChangeType change = NSChangeDone;
        if (self.undoManager.isUndoing) change = NSChangeUndone;
        else if (self.undoManager.isRedoing) change = NSChangeRedone;
        [self updateChangeCount:change];
        if (!_model->Snapshot().IsModified()) [self updateChangeCount:NSChangeCleared];
        [_lineRuler reloadLineNumbers];
        [self updateStatus];
        [self scheduleRecovery];
    } else {
        _statusLabel.stringValue = error.localizedDescription;
    }
}

- (void)textViewDidChangeSelection:(NSNotification *)notification
{
    if (notification.object == _editorView) [_lineRuler setNeedsDisplay:YES];
}

- (void)selectLine:(NSInteger)line
{
    if (line <= 0 || _editorView == nil) return;
    NSString *text = _editorView.string;
    NSUInteger position = 0;
    for (NSInteger current = 1; current < line; ++current) {
        if (position >= text.length) return;
        NSRange newline = [text rangeOfString:@"\n" options:0
            range:NSMakeRange(position, text.length - position)];
        if (newline.location == NSNotFound) return;
        position = NSMaxRange(newline);
    }
    NSRange newline = [text rangeOfString:@"\n" options:0
        range:NSMakeRange(position, text.length - position)];
    const NSUInteger length = newline.location == NSNotFound
        ? text.length - position : newline.location - position;
    const NSRange range = NSMakeRange(position, length);
    [_editorView setSelectedRange:range];
    [_editorView scrollRangeToVisible:range];
    [_editorView showFindIndicatorForRange:range];
    [_editorView.window makeFirstResponder:_editorView];
}

- (IBAction)goToLine:(id)sender
{
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Go to Line";
    alert.informativeText = @"Enter a one-based line number.";
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 220, 24)];
    field.stringValue = @"1";
    alert.accessoryView = field;
    [alert addButtonWithTitle:@"Go"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) [self selectLine:field.integerValue];
}

- (IBAction)changeEncoding:(id)sender
{
    NSMenuItem *item = [sender isKindOfClass:NSMenuItem.class] ? sender : nil;
    if (item == nil) return;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) {
        [NSApp presentError:error];
        return;
    }
    const auto changed = _model->SetEncoding(
        static_cast<mucom88::TextEncoding>(item.tag));
    if (!changed.Succeeded()) {
        SetError(&error, 8, StringFromUtf8(changed.error.message));
        [NSApp presentError:error];
        return;
    }
    _updatingEditor = YES;
    _editorView.string = StringFromUtf8(changed.value.utf8_text);
    ApplyEditorTextAppearance(_editorView);
    _updatingEditor = NO;
    [_lineRuler reloadLineNumbers];
    if (changed.value.IsModified()) [self updateChangeCount:NSChangeDone];
    else [self updateChangeCount:NSChangeCleared];
    [self updateStatus];
}

- (BOOL)textView:(NSTextView *)textView clickedOnLink:(id)link
          atIndex:(NSUInteger)charIndex
{
    (void)charIndex;
    if (textView != _messageView || ![link isKindOfClass:NSString.class]) return NO;
    NSString *value = link;
    if (![value hasPrefix:@"mucom-line:"]) return NO;
    [self selectLine:[[value substringFromIndex:@"mucom-line:".length] integerValue]];
    return YES;
}

- (void)showCompileResult:(const mucom88::CompileResult &)result
{
    NSMutableAttributedString *output = [[NSMutableAttributedString alloc] init];
    NSDictionary *base = @{
        NSFontAttributeName:
            [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: NSColor.labelColor
    };
    for (const auto &diagnostic : result.diagnostics) {
        NSString *line = [NSString stringWithFormat:@"Line %d: %@\n",
            diagnostic.line, StringFromUtf8(diagnostic.message)];
        NSMutableAttributedString *entry = [[NSMutableAttributedString alloc]
            initWithString:line attributes:base];
        [entry addAttribute:NSLinkAttributeName
            value:[NSString stringWithFormat:@"mucom-line:%d", diagnostic.line]
            range:NSMakeRange(0, line.length)];
        [output appendAttributedString:entry];
    }
    if (!result.diagnostics.empty() && !result.messages.empty()) {
        [output appendAttributedString:[[NSAttributedString alloc]
            initWithString:@"\n" attributes:base]];
    }
    [output appendAttributedString:[[NSAttributedString alloc]
        initWithString:StringFromUtf8(result.messages) attributes:base]];
    [_messageView.textStorage setAttributedString:output];
}

- (IBAction)compileDocument:(id)sender
{
    (void)sender;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) {
        _statusLabel.stringValue = error.localizedDescription;
        return;
    }
    if (!_services->compiler->IsReady()) {
        _statusLabel.stringValue = @"MUCOM88 initialization failed";
        return;
    }
    mucom88::CompileRequest request = _model->MakeCompileRequest();
    _statusLabel.stringValue = @"Compiling…";
    _compileOperation.Cancel();
    __weak MucomDocument *weakSelf = self;
    _compileOperation = _services->compiler->CompileAsync(std::move(request),
        [weakSelf](mucom88::CompileResult result) mutable {
            MucomDocument *document = weakSelf;
            if (document == nil) return;
            const auto current = document->_model->Snapshot();
            if (result.document_id != current.document_id ||
                result.revision != current.revision) return;
            [document showCompileResult:result];
            if (result.Succeeded()) {
                document->_statusLabel.stringValue = [NSString stringWithFormat:
                    @"Compile succeeded (driver %d)", static_cast<int>(result.driver)];
            } else if (result.error.code != mucom88::ServiceErrorCode::Cancelled) {
                document->_statusLabel.stringValue = @"Compile failed";
                if (!result.diagnostics.empty()) {
                    [document selectLine:result.diagnostics.front().line];
                }
            }
        });
}

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item
{
    mucom88::EditorCommandState state;
    state.has_document = YES;
    state.has_text_view = _editorView != nil;
    state.compiler_ready = _services != nullptr && _services->compiler->IsReady();
    if (item.action == @selector(compileDocument:)) {
        return mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::Compile, state);
    }
    if (item.action == @selector(goToLine:)) {
        return mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::GoToLine, state);
    }
    if (item.action == @selector(changeEncoding:)) return state.has_text_view;
    return [super validateUserInterfaceItem:item];
}

- (BOOL)restoreRecoveryAtPath:(NSString *)path error:(NSError **)error
{
    mucom88::RecoveryService recovery(RecoveryRoot());
    const auto restored = recovery.Restore(*_model, path.fileSystemRepresentation);
    if (!restored.Succeeded()) {
        SetError(error, 9, StringFromUtf8(restored.error.message));
        return NO;
    }
    _restoredRecoveryPath = [path copy];
    if (_editorView != nil) {
        _updatingEditor = YES;
        _editorView.string = StringFromUtf8(restored.value.utf8_text);
        ApplyEditorTextAppearance(_editorView);
        _updatingEditor = NO;
        [_lineRuler reloadLineNumbers];
    }
    [self updateChangeCount:NSChangeDone];
    [self updateStatus];
    return YES;
}

- (void)close
{
    [_recoveryTimer invalidate];
    _recoveryCancelled->store(true, std::memory_order_release);
    mucom88::RecoveryService recovery(RecoveryRoot());
    recovery.RemoveDocument(_model->Snapshot().document_id);
    if (_restoredRecoveryPath != nil) {
        recovery.RemoveEntry(_restoredRecoveryPath.fileSystemRepresentation);
    }
    [super close];
}

- (void)dealloc
{
    [_recoveryTimer invalidate];
    _compileOperation.Cancel();
}

@end

@interface MucomAppDelegate : NSObject <NSApplicationDelegate> {
    std::shared_ptr<mucom88::ApplicationServices> _services;
}
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
    AddMenuItem(applicationMenu, @"About MUCOM88 Editor",
        @selector(orderFrontStandardAboutPanel:), @"").target = NSApp;
    [applicationMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(applicationMenu, @"Quit MUCOM88 Editor",
        @selector(terminate:), @"q").target = NSApp;

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
    NSMenuItem *controlSave = AddMenuItem(fileMenu,
        @"Save (Windows Shortcut)", @selector(saveDocument:), @"s");
    controlSave.keyEquivalentModifierMask = NSEventModifierFlagControl;

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
    [editMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *find = AddMenuItem(editMenu, @"Find…",
        @selector(performTextFinderAction:), @"f");
    find.tag = NSTextFinderActionShowFindInterface;
    NSMenuItem *findNext = AddMenuItem(editMenu, @"Find Next",
        @selector(performTextFinderAction:), @"g");
    findNext.tag = NSTextFinderActionNextMatch;
    NSMenuItem *findPrevious = AddMenuItem(editMenu, @"Find Previous",
        @selector(performTextFinderAction:), @"G");
    findPrevious.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagShift;
    findPrevious.tag = NSTextFinderActionPreviousMatch;
    AddMenuItem(editMenu, @"Go to Line…", @selector(goToLine:), @"l");

    NSMenuItem *formatItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:formatItem];
    NSMenu *formatMenu = [[NSMenu alloc] initWithTitle:@"Format"];
    formatItem.submenu = formatMenu;
    NSMenuItem *encodingItem = AddMenuItem(formatMenu, @"Text Encoding", nil, @"");
    NSMenu *encodingMenu = [[NSMenu alloc] initWithTitle:@"Text Encoding"];
    encodingItem.submenu = encodingMenu;
    const std::pair<NSString *, mucom88::TextEncoding> encodings[] = {
        {@"UTF-8", mucom88::TextEncoding::Utf8},
        {@"UTF-8 with BOM", mucom88::TextEncoding::Utf8Bom},
        {@"CP932", mucom88::TextEncoding::Cp932},
        {@"Shift_JIS", mucom88::TextEncoding::ShiftJis}
    };
    for (const auto &encoding : encodings) {
        NSMenuItem *item = AddMenuItem(encodingMenu, encoding.first,
            @selector(changeEncoding:), @"");
        item.tag = static_cast<NSInteger>(encoding.second);
    }

    NSMenuItem *buildItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:buildItem];
    NSMenu *buildMenu = [[NSMenu alloc] initWithTitle:@"Build"];
    buildItem.submenu = buildMenu;
    AddMenuItem(buildMenu, @"Compile", @selector(compileDocument:), @"r");

    NSMenuItem *windowItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:windowItem];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    windowItem.submenu = windowMenu;
    AddMenuItem(windowMenu, @"Minimize", @selector(performMiniaturize:), @"m");
    AddMenuItem(windowMenu, @"Zoom", @selector(performZoom:), @"");
    [windowMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(windowMenu, @"Bring All to Front", @selector(arrangeInFront:), @"");
    NSApp.windowsMenu = windowMenu;
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    mucom88::CompletionDispatcher mainDispatcher = [](mucom88::CompletionTask task) {
        dispatch_async(dispatch_get_main_queue(), ^{ if (task) task(); });
    };
    _services = std::make_shared<mucom88::ApplicationServices>(
        std::move(mainDispatcher));
    mucom88::InstallApplicationServices(_services);
    [self installMainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    mucom88::RecoveryService recovery(RecoveryRoot());
    const auto scanned = recovery.Scan();
    if (!scanned.Succeeded() || scanned.value.empty()) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Recovered Documents Found";
    alert.informativeText = @"MUCOM88 Editor found unsaved recovery snapshots.";
    [alert addButtonWithTitle:@"Restore Latest Documents"];
    [alert addButtonWithTitle:@"Later"];
    [alert addButtonWithTitle:@"Discard All"];
    const NSModalResponse response = [alert runModal];
    if (response == NSAlertThirdButtonReturn) {
        for (const auto &entry : scanned.value) recovery.RemoveEntry(entry.path);
        return;
    }
    if (response != NSAlertFirstButtonReturn) return;
    std::set<std::string> restoredDirectories;
    for (const auto &entry : scanned.value) {
        const std::string directory =
            std::filesystem::path(entry.path).parent_path().string();
        if (!restoredDirectories.insert(directory).second) continue;
        NSError *error = nil;
        NSDocument *base = [NSDocumentController.sharedDocumentController
            makeUntitledDocumentOfType:@"org.mucom88.muc" error:&error];
        if (base == nil || ![base isKindOfClass:MucomDocument.class]) {
            if (error != nil) [NSApp presentError:error];
            continue;
        }
        MucomDocument *document = (MucomDocument *)base;
        if (![document restoreRecoveryAtPath:StringFromUtf8(entry.path) error:&error]) {
            if (error != nil) [NSApp presentError:error];
            continue;
        }
        [NSDocumentController.sharedDocumentController addDocument:document];
        [document makeWindowControllers];
        [document showWindows];
    }
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
    (void)notification;
    mucom88::InstallApplicationServices(nullptr);
    _services.reset();
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
