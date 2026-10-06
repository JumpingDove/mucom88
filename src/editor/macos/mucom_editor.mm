#import <AppKit/AppKit.h>
#import "editor/macos/phase5_windows.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <climits>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "editor/application_services.h"
#include "editor/document_service.h"
#include "editor/text_transform_service.h"
#include "editor/n88_export_service.h"
#include "editor/voice_append_service.h"
#include "editor/pcm_bank_service.h"
#include "editor/song_metadata.h"
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

NSString *PlaybackStateName(mucom88::PlaybackState state)
{
    switch (state) {
    case mucom88::PlaybackState::Idle: return @"Idle";
    case mucom88::PlaybackState::Preparing: return @"Preparing";
    case mucom88::PlaybackState::Buffering: return @"Buffering";
    case mucom88::PlaybackState::Playing: return @"Playing";
    case mucom88::PlaybackState::Paused: return @"Paused";
    case mucom88::PlaybackState::Draining: return @"Draining";
    case mucom88::PlaybackState::Finished: return @"Finished";
    case mucom88::PlaybackState::Stopping: return @"Stopping";
    case mucom88::PlaybackState::DeviceLost: return @"Device lost";
    case mucom88::PlaybackState::Failed: return @"Failed";
    }
    return @"Unknown";
}

NSString *AudioFormatText(const mucom88::AudioDeviceOpenResult &opened)
{
    const auto &requested = opened.requested;
    const auto &obtained = opened.obtained;
    return [NSString stringWithFormat:
        @"%@ • requested %d Hz/%d-bit/%d ch/%d frames • obtained %d Hz/%d-bit/%d ch/%d frames",
        StringFromUtf8(opened.device.name), requested.sample_rate,
        requested.bits_per_sample, requested.channels,
        requested.frames_per_buffer, obtained.sample_rate,
        obtained.bits_per_sample, obtained.channels,
        obtained.frames_per_buffer];
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
    NSTextField *_playbackLabel;
    NSProgressIndicator *_playbackProgress;
    NSButton *_playButton;
    NSButton *_pauseButton;
    NSButton *_stopButton;
    NSButton *_fastForwardButton;
    NSPopUpButton *_speedPopup;
    NSPopUpButton *_devicePopup;
    NSButton *_reconnectButton;
    NSTextField *_audioFormatLabel;
    NSTextField *_audioDiagnosticsLabel;
    LineNumberRulerView *_lineRuler;
    NSTimer *_recoveryTimer;
    id _presentationObserver;
    std::uint64_t _audioDeviceGeneration;
    NSInteger _fastForwardMultiplier;
    BOOL _fastForwarding;
    NSString *_restoredRecoveryPath;
    BOOL _updatingEditor;
    BOOL _buildingPcm;
}
- (IBAction)compileDocument:(id)sender;
- (IBAction)compileAndPlayDocument:(id)sender;
- (IBAction)pauseResumePlayback:(id)sender;
- (IBAction)stopPlayback:(id)sender;
- (IBAction)toggleFastForward:(id)sender;
- (IBAction)changeFastForwardSpeed:(id)sender;
- (IBAction)beginMomentaryFastForward:(id)sender;
- (IBAction)endMomentaryFastForward:(id)sender;
- (IBAction)chooseDefaultPCM:(id)sender;
- (IBAction)chooseDefaultVoice:(id)sender;
- (IBAction)chooseExternalROMDirectory:(id)sender;
- (IBAction)chooseRhythmDirectory:(id)sender;
- (IBAction)toggleExternalROM:(id)sender;
- (IBAction)clearResourceOverrides:(id)sender;
- (IBAction)changeAudioDevice:(id)sender;
- (IBAction)reconnectAudioDevice:(id)sender;
- (void)updatePlaybackUI;
- (void)updatePlaybackUIWithSnapshot:
    (const mucom88::PlaybackCoordinatorSnapshot &)snapshot;
- (void)refreshAudioDevices:(BOOL)force;
- (mucom88::CompileRequest)configuredCompileRequest;
- (IBAction)removeN88LineNumbers:(id)sender;
- (IBAction)convertGChannel:(id)sender;
- (void)runTextTransform:(mucom88::TextTransformKind)kind
    title:(NSString *)title;
- (IBAction)buildPcmFromData:(id)sender;
- (IBAction)buildPcmFromList:(id)sender;
- (void)buildPcmBankFromDirectory:(BOOL)directory;
- (void)previewPcmBank:(const mucom88::PcmBankArtifact &)artifact;
- (IBAction)appendUsedVoices:(id)sender;
- (void)showToolPreview:(const mucom88::TextTransformPreview &)preview
    source:(const mucom88::DocumentSnapshot &)source title:(NSString *)title;
- (IBAction)exportN88Source:(id)sender;
- (IBAction)addMetadataTags:(id)sender;
- (void)previewTextTransform:(const mucom88::TextTransformRequest &)request
    source:(const mucom88::DocumentSnapshot &)source title:(NSString *)title;
- (void)applyToolPreview:(const mucom88::TextTransformPreview &)preview
    selection:(NSRange)selection actionName:(NSString *)actionName;
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
        _fastForwardMultiplier = 2;
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
    _playButton = [NSButton buttonWithTitle:@"Compile & Play"
        target:self action:@selector(compileAndPlayDocument:)];
    _pauseButton = [NSButton buttonWithTitle:@"Pause"
        target:self action:@selector(pauseResumePlayback:)];
    _stopButton = [NSButton buttonWithTitle:@"Stop"
        target:self action:@selector(stopPlayback:)];
    _fastForwardButton = [NSButton buttonWithTitle:@"Fast"
        target:self action:@selector(toggleFastForward:)];
    _fastForwardButton.buttonType = NSButtonTypeToggle;
    _speedPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    for (NSNumber *speed in @[@2, @4, @6, @8, @10]) {
        [_speedPopup addItemWithTitle:[NSString stringWithFormat:@"x%@", speed]];
        _speedPopup.lastItem.tag = speed.integerValue;
    }
    _speedPopup.target = self;
    _speedPopup.action = @selector(changeFastForwardSpeed:);
    NSStackView *controlBar = [NSStackView stackViewWithViews:
        @[compileButton, _playButton, _pauseButton, _stopButton,
          _fastForwardButton, _speedPopup, _statusLabel]];
    controlBar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    controlBar.alignment = NSLayoutAttributeCenterY;
    controlBar.spacing = 10.0;
    [_statusLabel setContentHuggingPriority:NSLayoutPriorityDefaultLow
        forOrientation:NSLayoutConstraintOrientationHorizontal];

    _playbackProgress = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _playbackProgress.indeterminate = NO;
    _playbackProgress.minValue = 0.0;
    _playbackProgress.maxValue = 1.0;
    _playbackLabel = [NSTextField labelWithString:@"Idle"];
    _playbackLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    NSStackView *playbackBar = [NSStackView stackViewWithViews:
        @[_playbackProgress, _playbackLabel]];
    playbackBar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    playbackBar.alignment = NSLayoutAttributeCenterY;
    playbackBar.spacing = 10.0;
    [_playbackLabel setContentHuggingPriority:NSLayoutPriorityDefaultLow
        forOrientation:NSLayoutConstraintOrientationHorizontal];

    _devicePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _devicePopup.target = self;
    _devicePopup.action = @selector(changeAudioDevice:);
    _reconnectButton = [NSButton buttonWithTitle:@"Reconnect"
        target:self action:@selector(reconnectAudioDevice:)];
    _audioFormatLabel = [NSTextField labelWithString:
        @"Audio: 44100 Hz, signed 16-bit stereo requested"];
    _audioFormatLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
    NSStackView *deviceBar = [NSStackView stackViewWithViews:
        @[_devicePopup, _reconnectButton, _audioFormatLabel]];
    deviceBar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    deviceBar.alignment = NSLayoutAttributeCenterY;
    deviceBar.spacing = 10.0;
    [_audioFormatLabel setContentHuggingPriority:NSLayoutPriorityDefaultLow
        forOrientation:NSLayoutConstraintOrientationHorizontal];

    _audioDiagnosticsLabel = [NSTextField labelWithString:
        @"Audio diagnostics: queued 0 • underruns 0 • dropped 0 • refills 0 • rendered 0"];
    _audioDiagnosticsLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    NSStackView *diagnosticsBar = [NSStackView stackViewWithViews:
        @[_audioDiagnosticsLabel]];
    diagnosticsBar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    diagnosticsBar.alignment = NSLayoutAttributeCenterY;

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
        @[controlBar, deviceBar, playbackBar, diagnosticsBar,
          editorScroll, messageScroll]];
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
        [playbackBar.widthAnchor constraintEqualToAnchor:layout.widthAnchor],
        [deviceBar.widthAnchor constraintEqualToAnchor:layout.widthAnchor],
        [diagnosticsBar.widthAnchor constraintEqualToAnchor:layout.widthAnchor],
        [_devicePopup.widthAnchor constraintGreaterThanOrEqualToConstant:220.0],
        [_playbackProgress.widthAnchor constraintGreaterThanOrEqualToConstant:180.0],
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
    [self refreshAudioDevices:YES];
    __weak MucomDocument *weakSelf = self;
    _presentationObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:MucomPlaybackPresentationDidUpdateNotification
        object:nil queue:NSOperationQueue.mainQueue
        usingBlock:^(NSNotification *notification) {
            MucomDocument *document = weakSelf;
            if (document != nil) {
                [document refreshAudioDevices:NO];
                MucomPlaybackPresentationUpdate *update =
                    (MucomPlaybackPresentationUpdate *)notification.object;
                [document updatePlaybackUIWithSnapshot:
                    [update playbackSnapshot]];
            }
        }];
    [self updatePlaybackUI];
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
        // A compile-and-play request is tied to the captured revision. Editing
        // cancels an in-flight compile so it cannot start stale audio.
        _compileOperation.Cancel();
        _services->playback_coordinator->CancelPendingPlay(
            _model->Snapshot().document_id);
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

- (void)applyToolPreview:(const mucom88::TextTransformPreview &)preview
    selection:(NSRange)selection actionName:(NSString *)actionName
{
    const auto previous = _model->Snapshot();
    const auto applied = mucom88::TextTransformService().Apply(*_model, preview);
    if (!applied.Succeeded()) {
        NSError *error = nil;
        SetError(&error, 3, StringFromUtf8(applied.error.message));
        [self presentError:error];
        return;
    }
    if (previous.content_id == applied.value.content_id) return;
    const NSRange previousSelection = _editorView.selectedRange;
    [self.undoManager registerUndoWithTarget:self handler:^(MucomDocument *target) {
        const auto current = target->_model->Snapshot();
        mucom88::TextTransformPreview inverse{previous.document_id, current.revision,
            previous.utf8_text, previous.line_endings};
        [target applyToolPreview:inverse selection:previousSelection actionName:actionName];
    }];
    [self.undoManager setActionName:actionName];
    _updatingEditor = YES;
    [_editorView.textStorage replaceCharactersInRange:
        NSMakeRange(0, _editorView.string.length) withString:StringFromUtf8(applied.value.utf8_text)];
    ApplyEditorTextAppearance(_editorView);
    selection.location = std::min(selection.location, _editorView.string.length);
    selection.length = std::min(selection.length,
        _editorView.string.length - selection.location);
    _editorView.selectedRange = selection;
    _updatingEditor = NO;
    [_editorView didChangeText];
}

- (IBAction)addMetadataTags:(id)sender
{
    (void)sender;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) {
        [self presentError:error];
        return;
    }
    const auto source = _model->Snapshot();
    // File kind remains N88 after removing numbers; inspect the source itself.
    const auto first = source.utf8_text.find_first_not_of(" \t\n");
    if (first != std::string::npos && source.utf8_text[first] >= '0' &&
        source.utf8_text[first] <= '9') {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Remove N88 Line Numbers First";
        alert.informativeText = @"Use Tools → Remove N88 Line Numbers before adding metadata tags.";
        [alert beginSheetModalForWindow:_editorView.window completionHandler:nil];
        return;
    }
    const auto parsed = mucom88::MetadataService().ParseUtf8(source.utf8_text);
    if (!parsed.Succeeded()) {
        SetError(&error, 4, StringFromUtf8(parsed.error.message));
        [self presentError:error];
        return;
    }
    const std::string keys[] = {"title", "composer", "author", "voice", "pcm", "date", "comment"};
    const std::string values[] = {parsed.value.title, parsed.value.composer,
        parsed.value.author, parsed.value.voice, parsed.value.pcm,
        parsed.value.date, parsed.value.comment};
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Add Metadata Tags";
    alert.informativeText = @"Existing lowercase tags are kept. Leave missing fields empty to omit them.";
    [alert addButtonWithTitle:@"Preview"];
    [alert addButtonWithTitle:@"Cancel"];
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 245)];
    NSMutableArray<NSTextField *> *fields = [NSMutableArray array];
    for (int index = 0; index < 7; ++index) {
        const CGFloat y = (6 - index) * 35;
        NSTextField *label = [NSTextField labelWithString:StringFromUtf8(keys[index])];
        label.frame = NSMakeRect(0, y, 90, 24);
        [container addSubview:label];
        NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(95, y, 420, 24)];
        field.stringValue = StringFromUtf8(values[index]);
        // An empty canonical tag is still an existing tag and must be preserved.
        bool exists = false;
        const std::string prefix = "#" + keys[index];
        for (std::size_t at = 0; at < source.utf8_text.size();) {
            const auto end = source.utf8_text.find('\n', at);
            const auto length = end == std::string::npos ? source.utf8_text.size() - at : end - at;
            if (source.utf8_text.compare(at, prefix.size(), prefix) == 0 &&
                (length == prefix.size() || (length > prefix.size() &&
                    (source.utf8_text[at + prefix.size()] == ' ' ||
                        source.utf8_text[at + prefix.size()] == '\t')))) exists = true;
            at = end == std::string::npos ? source.utf8_text.size() : end + 1;
        }
        field.enabled = !exists;
        if (exists) field.toolTip = @"Existing tag is preserved.";
        [fields addObject:field];
        [container addSubview:field];
    }
    alert.accessoryView = container;
    [alert beginSheetModalForWindow:_editorView.window completionHandler:^(NSModalResponse response) {
        if (response != NSAlertFirstButtonReturn) {
            [self->_editorView.window makeFirstResponder:self->_editorView];
            return;
        }
        mucom88::TextTransformRequest request;
        request.kind = mucom88::TextTransformKind::AddMetadataTags;
        const char *names[] = {"title", "composer", "author", "voice", "pcm", "date", "comment"};
        for (NSUInteger index = 0; index < fields.count; ++index) {
            NSTextField *field = fields[index];
            if (!field.enabled || field.stringValue.length == 0) continue;
            NSData *data = [field.stringValue dataUsingEncoding:NSUTF8StringEncoding];
            if (data == nil) {
                NSError *valueError = nil;
                SetError(&valueError, 5, @"Metadata must be valid UTF-8.");
                [self presentError:valueError];
                return;
            }
            request.metadata_tags.emplace_back(names[index], std::string(
                static_cast<const char *>(data.bytes), data.length));
        }
        // Keep the revision captured before input, so edits cannot cross-apply.
        [self previewTextTransform:request source:source title:@"Add Metadata Tags"];
    }];
}

- (IBAction)removeN88LineNumbers:(id)sender
{
    (void)sender;
    [self runTextTransform:mucom88::TextTransformKind::RemoveN88LineNumbers
        title:@"Remove N88 Line Numbers"];
}

- (IBAction)convertGChannel:(id)sender
{
    (void)sender;
    [self runTextTransform:mucom88::TextTransformKind::ConvertGChannelQ
        title:@"Convert G Channel q to @"];
}

- (void)runTextTransform:(mucom88::TextTransformKind)kind
    title:(NSString *)title
{
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) {
        [self presentError:error];
        return;
    }
    mucom88::TextTransformRequest request;
    request.kind = kind;
    [self previewTextTransform:request source:_model->Snapshot() title:title];
}

- (void)previewTextTransform:(const mucom88::TextTransformRequest &)request
    source:(const mucom88::DocumentSnapshot &)source title:(NSString *)title
{
    const auto result = mucom88::TextTransformService().Preview(source, request);
    if (!result.Succeeded()) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = [@"Cannot " stringByAppendingString:title];
        alert.informativeText = StringFromUtf8(result.error.message);
        [alert beginSheetModalForWindow:_editorView.window completionHandler:nil];
        return;
    }
    [self showToolPreview:result.value source:source title:title];
}

- (void)showToolPreview:(const mucom88::TextTransformPreview &)requestedPreview
    source:(const mucom88::DocumentSnapshot &)source title:(NSString *)title
{
    // The sheet outlives the caller, including asynchronous compiler completion.
    const auto preview = requestedPreview;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = @"Review the original and converted source before applying.";
    [alert addButtonWithTitle:@"Apply"];
    [alert addButtonWithTitle:@"Cancel"];
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 680, 300)];
    const std::string texts[] = {source.utf8_text, preview.utf8_text};
    for (int index = 0; index < 2; ++index) {
        NSTextField *label = [NSTextField labelWithString:
            index == 0 ? @"Original" : @"Preview"];
        label.frame = NSMakeRect(index * 345, 278, 330, 22);
        [container addSubview:label];
        NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:
            NSMakeRect(index * 345, 0, 335, 275)];
        scroll.hasVerticalScroller = YES;
        scroll.hasHorizontalScroller = YES;
        NSTextView *view = [[NSTextView alloc] initWithFrame:scroll.bounds];
        view.editable = NO;
        view.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        view.string = StringFromUtf8(texts[index]);
        scroll.documentView = view;
        [container addSubview:scroll];
    }
    alert.accessoryView = container;
    [alert beginSheetModalForWindow:_editorView.window
        completionHandler:^(NSModalResponse response) {
        [self->_editorView.window makeFirstResponder:self->_editorView];
        if (response != NSAlertFirstButtonReturn) return;
        const auto current = self->_model->Snapshot();
        if (current.document_id == preview.document_id && current.revision == preview.revision &&
            current.utf8_text == preview.utf8_text &&
            (!preview.line_endings || current.line_endings == *preview.line_endings)) return;
        [self->_editorView breakUndoCoalescing];
        [self.undoManager beginUndoGrouping];
        [self applyToolPreview:preview selection:NSMakeRange(0, 0) actionName:title];
        [self.undoManager endUndoGrouping];
        [self->_editorView breakUndoCoalescing];
    }];
}

- (IBAction)buildPcmFromData:(id)sender
{
    (void)sender;
    [self buildPcmBankFromDirectory:YES];
}

- (IBAction)buildPcmFromList:(id)sender
{
    (void)sender;
    [self buildPcmBankFromDirectory:NO];
}

- (void)buildPcmBankFromDirectory:(BOOL)directory
{
    if (_buildingPcm) return;
    NSURL *url = [self chooseResourceWithTitle:directory ? @"Choose DATA / VOICE Directory" : @"Choose PCM Sample List"
        directory:directory];
    if (url == nil) return;
    const std::string input(url.fileSystemRepresentation);
    _buildingPcm = YES;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    NSAlert *busy = [[NSAlert alloc] init];
    busy.messageText = @"Building PCM Bank";
    busy.informativeText = @"Reading samples and converting WAV audio…";
    [busy addButtonWithTitle:@"Cancel"];
    NSProgressIndicator *progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 360, 18)];
    progress.indeterminate = YES;
    [progress startAnimation:nil];
    busy.accessoryView = progress;
    [busy beginSheetModalForWindow:_editorView.window completionHandler:^(NSModalResponse response) {
        if (response == NSAlertFirstButtonReturn) cancelled->store(true);
    }];
    __weak MucomDocument *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        const auto built = directory ? mucom88::PcmBankService().BuildFromDataDirectory(input)
            : mucom88::PcmBankService().BuildFromList(input);
        dispatch_async(dispatch_get_main_queue(), ^{
            MucomDocument *document = weakSelf;
            [NSApp endSheet:busy.window returnCode:NSModalResponseOK];
            if (document == nil) return;
            document->_buildingPcm = NO;
            if (cancelled->load()) return;
            if (!built.Succeeded()) {
                NSAlert *error = [[NSAlert alloc] init];
                error.messageText = @"Cannot Build PCM Bank";
                error.informativeText = StringFromUtf8(built.error.message + "\n" + built.error.path);
                [error beginSheetModalForWindow:document->_editorView.window completionHandler:nil];
                return;
            }
            [document previewPcmBank:built.value];
        });
    });
}

- (void)previewPcmBank:(const mucom88::PcmBankArtifact &)artifact
{
    // Own the bytes and provenance throughout both sheets.
    const auto bank = artifact;
    const auto word = [&](std::size_t at) { return int(bank.bytes[at]) | (int(bank.bytes[at + 1]) << 8); };
    NSMutableString *details = [NSMutableString stringWithString:@"Slot  Name              Bytes     Start\n"];
    NSUInteger count = 0;
    for (int slot = 0; slot < 32; ++slot) {
        const int length = word(slot * 32 + 30) * 4;
        if (!length) continue;
        ++count;
        std::string name(bank.bytes.begin() + slot * 32, bank.bytes.begin() + slot * 32 + 16);
        for (char &c : name) {
            if (c == 0) c = ' ';
            else if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) >= 127) c = '?';
        }
        [details appendFormat:@"%4d  %-16s  %6d  %8d\n", slot + 1, name.c_str(), length, word(slot * 32 + 28) * 4];
    }
    NSAlert *preview = [[NSAlert alloc] init];
    preview.messageText = @"PCM Bank Preview";
    preview.informativeText = [NSString stringWithFormat:@"%lu entries • %lu bytes of audio • %lu bytes total",
        (unsigned long)count, (unsigned long)(bank.bytes.size() - 1024), (unsigned long)bank.bytes.size()];
    [preview addButtonWithTitle:@"Save…"];
    [preview addButtonWithTitle:@"Cancel"];
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 480, 240)];
    scroll.hasVerticalScroller = YES;
    NSTextView *view = [[NSTextView alloc] initWithFrame:scroll.bounds];
    view.editable = NO;
    view.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    view.string = details;
    scroll.documentView = view;
    preview.accessoryView = scroll;
    [preview beginSheetModalForWindow:_editorView.window completionHandler:^(NSModalResponse response) {
        if (response != NSAlertFirstButtonReturn) return;
        NSSavePanel *panel = [NSSavePanel savePanel];
        panel.title = @"Save PCM Bank";
        panel.nameFieldStringValue = @"pcm-bank.bin";
        panel.canCreateDirectories = YES;
        [panel beginSheetModalForWindow:self->_editorView.window completionHandler:^(NSModalResponse choice) {
            if (choice != NSModalResponseOK) return;
            const auto saved = mucom88::PcmBankService().Save(bank, panel.URL.fileSystemRepresentation);
            if (!saved.Succeeded()) {
                NSAlert *error = [[NSAlert alloc] init];
                error.messageText = @"Cannot Save PCM Bank";
                error.informativeText = StringFromUtf8(saved.error.message + "\n" + saved.error.path);
                [error beginSheetModalForWindow:self->_editorView.window completionHandler:nil];
            } else self->_statusLabel.stringValue = @"PCM bank saved";
        }];
    }];
}

- (IBAction)appendUsedVoices:(id)sender
{
    (void)sender;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) { [self presentError:error]; return; }
    const auto source = _model->Snapshot();
    _compileOperation.Cancel();
    _statusLabel.stringValue = @"Compiling for voice append…";
    __weak MucomDocument *weakSelf = self;
    _compileOperation = _services->compiler->CompileAsync([self configuredCompileRequest],
        [weakSelf, source](mucom88::CompileResult result) {
            MucomDocument *document = weakSelf;
            if (document == nil || result.error.code == mucom88::ServiceErrorCode::Cancelled) return;
            const auto current = document->_model->Snapshot();
            if (current.document_id != source.document_id || current.revision != source.revision ||
                document->_editorView.window.attachedSheet != nil) return;
            [document showCompileResult:result];
            if (!result.Succeeded()) { document->_statusLabel.stringValue = @"Compile failed"; return; }
            mucom88::VoiceService voices;
            const auto bank = voices.Load(result.song->resolved_voice_bank_path);
            mucom88::ServiceResult<mucom88::TextTransformPreview> preview;
            if (bank.Succeeded()) preview = mucom88::VoiceAppendService().Preview(source, *result.song, bank.value);
            else preview.error = bank.error;
            document->_statusLabel.stringValue = preview.Succeeded() ? @"Voice preview ready" : @"Voice append failed";
            if (!preview.Succeeded()) {
                NSAlert *alert = [[NSAlert alloc] init];
                alert.messageText = @"Cannot Append Used FM Voices";
                alert.informativeText = StringFromUtf8(preview.error.message);
                [alert beginSheetModalForWindow:document->_editorView.window completionHandler:nil];
                return;
            }
            [document showToolPreview:preview.value source:source title:@"Append Used FM Voices"];
        });
}

- (IBAction)exportN88Source:(id)sender
{
    (void)sender;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) { [self presentError:error]; return; }
    const auto source = _model->Snapshot();
    NSAlert *settings = [[NSAlert alloc] init];
    settings.messageText = @"Export N88-BASIC Source";
    settings.informativeText = @"Save numbered text to a separate file. The editing document stays unchanged.";
    [settings addButtonWithTitle:@"Preview"];
    [settings addButtonWithTitle:@"Cancel"];
    NSView *form = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 110)];
    NSTextField *start = [[NSTextField alloc] initWithFrame:NSMakeRect(170, 75, 220, 24)];
    start.stringValue = @"1000";
    NSTextField *increment = [[NSTextField alloc] initWithFrame:NSMakeRect(170, 40, 220, 24)];
    increment.stringValue = @"10";
    NSPopUpButton *encoding = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(170, 5, 220, 26)];
    [encoding addItemsWithTitles:@[@"UTF-8", @"UTF-8 with BOM", @"CP932", @"Shift_JIS"]];
    [encoding selectItemAtIndex:static_cast<NSInteger>(source.encoding)];
    const NSArray<NSString *> *labels = @[@"First line number", @"Line increment", @"Text encoding"];
    for (NSUInteger index = 0; index < labels.count; ++index) {
        NSTextField *label = [NSTextField labelWithString:labels[index]];
        label.frame = NSMakeRect(0, 75 - index * 35, 165, 24);
        [form addSubview:label];
    }
    [form addSubview:start]; [form addSubview:increment]; [form addSubview:encoding];
    settings.accessoryView = form;
    [settings beginSheetModalForWindow:_editorView.window completionHandler:^(NSModalResponse response) {
        [self->_editorView.window makeFirstResponder:self->_editorView];
        if (response != NSAlertFirstButtonReturn) return;
        const auto parse = [](NSString *value, int *output) {
            NSScanner *scanner = [NSScanner scannerWithString:value];
            long long number = 0;
            if (![scanner scanLongLong:&number] || !scanner.isAtEnd ||
                number < 0 || number > INT_MAX) return false;
            *output = static_cast<int>(number);
            return true;
        };
        int first = 0, step = 0;
        if (!parse(start.stringValue, &first) || !parse(increment.stringValue, &step) || step == 0) {
            NSError *numberError = nil;
            SetError(&numberError, 6, @"Enter a nonnegative integer start and a positive integer increment, each at most 2147483647.");
            [self presentError:numberError]; return;
        }
        const auto result = mucom88::N88ExportService().Preview(source, first, step);
        if (!result.Succeeded()) {
            NSError *previewError = nil;
            SetError(&previewError, 7, StringFromUtf8(result.error.message));
            [self presentError:previewError]; return;
        }
        const auto preview = result.value;
        const auto selectedEncoding = static_cast<mucom88::TextEncoding>(encoding.indexOfSelectedItem);
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"N88-BASIC Source Preview";
        alert.informativeText = @"This is numbered text, not tokenized BASIC. Save creates a separate output file.";
        [alert addButtonWithTitle:@"Save…"]; [alert addButtonWithTitle:@"Cancel"];
        NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 640, 300)];
        scroll.hasVerticalScroller = YES;
        NSTextView *view = [[NSTextView alloc] initWithFrame:scroll.bounds];
        view.editable = NO;
        view.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        view.string = StringFromUtf8(preview.utf8_text);
        scroll.documentView = view; alert.accessoryView = scroll;
        [alert beginSheetModalForWindow:self->_editorView.window completionHandler:^(NSModalResponse choice) {
            [self->_editorView.window makeFirstResponder:self->_editorView];
            if (choice != NSAlertFirstButtonReturn) return;
            NSSavePanel *panel = [NSSavePanel savePanel];
            panel.title = @"Save N88-BASIC Source";
            panel.nameFieldStringValue = source.path.empty() ? @"Untitled.n88" :
                [[StringFromUtf8(source.path).lastPathComponent stringByDeletingPathExtension]
                    stringByAppendingPathExtension:@"n88"];
            [panel beginSheetModalForWindow:self->_editorView.window completionHandler:^(NSModalResponse saved) {
                [self->_editorView.window makeFirstResponder:self->_editorView];
                if (saved != NSModalResponseOK || panel.URL == nil) return;
                const auto result = mucom88::N88ExportService().Save(self->_model->Snapshot(),
                    preview, panel.URL.fileSystemRepresentation, selectedEncoding);
                if (!result.Succeeded()) {
                    NSError *saveError = nil;
                    SetError(&saveError, 8, StringFromUtf8(result.error.message));
                    [self presentError:saveError];
                }
            }];
        }];
    }];
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

- (mucom88::CompileRequest)configuredCompileRequest
{
    mucom88::CompileRequest request = _model->MakeCompileRequest();
    request.resources = _services->resources;
    request.resources.document_directory = request.resource_directory;
    return request;
}

- (void)refreshAudioDevices:(BOOL)force
{
    if (_devicePopup == nil || _services == nullptr) return;
    const std::uint64_t generation = _services->audio->DeviceGeneration();
    if (!force && generation == _audioDeviceGeneration) return;
    const auto devices = _services->playback_coordinator->EnumerateAudioOutputs();
    if (!devices.Succeeded()) {
        _statusLabel.stringValue = StringFromUtf8(devices.error.message);
        return;
    }
    const auto snapshot = _services->playback_coordinator->Snapshot();
    [_devicePopup removeAllItems];
    BOOL selectedFound = NO;
    for (const auto &device : devices.value) {
        [_devicePopup addItemWithTitle:StringFromUtf8(device.name)];
        NSMenuItem *item = _devicePopup.lastItem;
        item.representedObject = StringFromUtf8(device.id);
        if (device.id == snapshot.selected_audio_device_id) {
            [_devicePopup selectItem:item];
            selectedFound = YES;
        }
    }
    if (!selectedFound && !snapshot.selected_audio_device_id.empty()) {
        NSString *title = [NSString stringWithFormat:@"Unavailable: %@",
            StringFromUtf8(snapshot.selected_audio_device_name)];
        [_devicePopup addItemWithTitle:title];
        _devicePopup.lastItem.representedObject =
            StringFromUtf8(snapshot.selected_audio_device_id);
        [_devicePopup selectItem:_devicePopup.lastItem];
    }
    _audioDeviceGeneration = devices.value.empty()
        ? generation : devices.value.front().generation;
}

- (IBAction)changeAudioDevice:(id)sender
{
    (void)sender;
    NSString *identifier = [_devicePopup.selectedItem.representedObject
        isKindOfClass:NSString.class]
        ? _devicePopup.selectedItem.representedObject : @"default";
    const mucom88::ServiceError error =
        _services->playback_coordinator->SelectAudioOutput(
            identifier.UTF8String == nullptr ? "default" : identifier.UTF8String);
    if (error) {
        _statusLabel.stringValue = StringFromUtf8(error.message);
        [self refreshAudioDevices:YES];
    } else {
        _statusLabel.stringValue = [NSString stringWithFormat:@"Audio output: %@",
            _devicePopup.selectedItem.title];
    }
    [self updatePlaybackUI];
}

- (IBAction)reconnectAudioDevice:(id)sender
{
    (void)sender;
    const mucom88::OperationHandle operation =
        _services->playback_coordinator->Reconnect();
    if (!operation.IsValid()) {
        _statusLabel.stringValue = @"No disconnected playback is available";
    } else {
        _statusLabel.stringValue = @"Reconnecting from the beginning…";
    }
}

- (void)updatePlaybackUI
{
    if (_playbackLabel == nil || _services == nullptr) return;
    const auto snapshot = _services->playback_coordinator->Snapshot();
    [self updatePlaybackUIWithSnapshot:snapshot];
}

- (void)updatePlaybackUIWithSnapshot:
    (const mucom88::PlaybackCoordinatorSnapshot &)snapshot
{
    if (_playbackLabel == nil || _services == nullptr) return;
    const auto document = _model->Snapshot();
    NSString *selectedDevice = StringFromUtf8(snapshot.selected_audio_device_id);
    for (NSMenuItem *item in _devicePopup.itemArray) {
        if ([item.representedObject isEqual:selectedDevice]) {
            [_devicePopup selectItem:item];
            break;
        }
    }
    NSString *owner = snapshot.document_id != 0 &&
        snapshot.document_id != document.document_id ? @"Other document • " : @"";
    NSString *detail = @"";
    if (snapshot.error) detail = [NSString stringWithFormat:@" • %@",
        StringFromUtf8(snapshot.error.message)];
    const auto monitor = snapshot.monitor;
    const mucom88::AudioDiagnostics diagnostics = monitor != nullptr
        ? monitor->audio : mucom88::AudioDiagnostics{};
    _audioDiagnosticsLabel.stringValue = [NSString stringWithFormat:
        @"Audio diagnostics: queued %llu • underruns %llu • dropped %llu • refills %llu • rendered %llu%@",
        static_cast<unsigned long long>(diagnostics.queued_frames),
        static_cast<unsigned long long>(diagnostics.underruns),
        static_cast<unsigned long long>(diagnostics.dropped_frames),
        static_cast<unsigned long long>(diagnostics.refill_events),
        static_cast<unsigned long long>(diagnostics.rendered_frames),
        diagnostics.device_lost ? @" • device lost" :
            (diagnostics.started ? @" • active" : @"")];
    _audioDiagnosticsLabel.toolTip = _audioDiagnosticsLabel.stringValue;
    if (snapshot.audio_device) {
        _audioFormatLabel.stringValue = AudioFormatText(*snapshot.audio_device);
    } else {
        _audioFormatLabel.stringValue =
            @"Audio: requested 44100 Hz, signed 16-bit, 2 channels, 1024 frames/buffer";
    }
    if (snapshot.error.code == mucom88::ServiceErrorCode::UnsupportedFormat ||
        snapshot.error.code == mucom88::ServiceErrorCode::DeviceUnavailable) {
        _audioFormatLabel.stringValue = StringFromUtf8(snapshot.error.message);
    }
    _audioFormatLabel.toolTip = _audioFormatLabel.stringValue;
    if (snapshot.state != mucom88::PlaybackState::Idle &&
        monitor != nullptr && monitor->max_count > 0) {
        _playbackProgress.indeterminate = NO;
        [_playbackProgress stopAnimation:nil];
        _playbackProgress.maxValue = monitor->max_count;
        _playbackProgress.doubleValue = monitor->current_count;
        _playbackLabel.stringValue = [NSString stringWithFormat:
            @"%@%@ • driver %d • %d/%d • loop %d • x%d%@", owner,
            PlaybackStateName(snapshot.state), static_cast<int>(monitor->driver),
            monitor->current_count, monitor->max_count,
            monitor->loop_count, monitor->speed, detail];
    } else {
        const BOOL busy = snapshot.state == mucom88::PlaybackState::Preparing ||
            snapshot.state == mucom88::PlaybackState::Buffering;
        _playbackProgress.indeterminate = busy;
        if (busy) [_playbackProgress startAnimation:nil];
        else {
            [_playbackProgress stopAnimation:nil];
            _playbackProgress.doubleValue = 0.0;
        }
        _playbackLabel.stringValue = [NSString stringWithFormat:@"%@%@%@",
            owner, PlaybackStateName(snapshot.state), detail];
    }
    _pauseButton.title = snapshot.state == mucom88::PlaybackState::Paused
        ? @"Resume" : @"Pause";
    mucom88::EditorCommandState state;
    state.has_document = true;
    state.has_text_view = _editorView != nil;
    state.compiler_ready = _services->compiler->IsReady();
    state.playback_ui_ready = true;
    state.reconnect_available = snapshot.reconnect_available;
    state.playback_state = snapshot.state;
    _playButton.enabled = mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::CompileAndPlay, state);
    _pauseButton.enabled = mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::PauseResume, state);
    _stopButton.enabled = mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Stop, state);
    _reconnectButton.enabled = mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Reconnect, state);
    _devicePopup.enabled = snapshot.state == mucom88::PlaybackState::Idle ||
        snapshot.state == mucom88::PlaybackState::Finished ||
        snapshot.state == mucom88::PlaybackState::Failed ||
        snapshot.state == mucom88::PlaybackState::DeviceLost;
    const BOOL fastEnabled = mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::FastForward, state);
    _fastForwardButton.enabled = fastEnabled;
    _speedPopup.enabled = fastEnabled;
    if (!fastEnabled && _fastForwarding) {
        _fastForwarding = NO;
        _fastForwardButton.state = NSControlStateValueOff;
    }
}

- (IBAction)compileAndPlayDocument:(id)sender
{
    (void)sender;
    NSError *error = nil;
    if (![self syncModelFromEditor:&error]) {
        _statusLabel.stringValue = error.localizedDescription;
        return;
    }
    _compileOperation.Cancel();
    _statusLabel.stringValue = @"Compiling for playback…";
    __weak MucomDocument *weakSelf = self;
    _compileOperation = _services->playback_coordinator->CompileAndPlay(
        [self configuredCompileRequest], {},
        [weakSelf](mucom88::CompileResult result) mutable {
            MucomDocument *document = weakSelf;
            if (document == nil) return;
            const auto current = document->_model->Snapshot();
            if (result.document_id != current.document_id ||
                result.revision != current.revision) return;
            [document showCompileResult:result];
            if (result.Succeeded()) {
                document->_statusLabel.stringValue = @"Compiled; starting playback…";
            } else if (result.error.code != mucom88::ServiceErrorCode::Cancelled) {
                document->_statusLabel.stringValue = StringFromUtf8(
                    result.error.message.empty() ? "Compile failed" :
                        result.error.message);
                if (!result.diagnostics.empty())
                    [document selectLine:result.diagnostics.front().line];
            }
        }, mucom88::PlaybackOwner::Editor(
            _model->Snapshot().document_id));
}

- (IBAction)pauseResumePlayback:(id)sender
{
    (void)sender;
    const auto state = _services->playback_coordinator->Snapshot().state;
    if (state != mucom88::PlaybackState::Buffering &&
        state != mucom88::PlaybackState::Playing &&
        state != mucom88::PlaybackState::Paused) return;
    _services->playback_coordinator->TogglePauseResume();
}

- (IBAction)stopPlayback:(id)sender
{
    (void)sender;
    _fastForwarding = NO;
    _fastForwardButton.state = NSControlStateValueOff;
    _services->playback_coordinator->Stop();
}

- (IBAction)toggleFastForward:(id)sender
{
    (void)sender;
    const auto state = _services->playback_coordinator->Snapshot().state;
    if (state != mucom88::PlaybackState::Buffering &&
        state != mucom88::PlaybackState::Playing &&
        state != mucom88::PlaybackState::Paused) return;
    _fastForwarding = !_fastForwarding;
    _fastForwardButton.state = _fastForwarding
        ? NSControlStateValueOn : NSControlStateValueOff;
    _services->playback_coordinator->SetSpeed(
        _fastForwarding ? static_cast<int>(_fastForwardMultiplier) : 1);
}

- (IBAction)changeFastForwardSpeed:(id)sender
{
    (void)sender;
    _fastForwardMultiplier = _speedPopup.selectedItem.tag;
    if (_fastForwarding) _services->playback_coordinator->SetSpeed(
        static_cast<int>(_fastForwardMultiplier));
}

- (IBAction)beginMomentaryFastForward:(id)sender
{
    (void)sender;
    const auto state = _services->playback_coordinator->Snapshot().state;
    if (state != mucom88::PlaybackState::Buffering &&
        state != mucom88::PlaybackState::Playing &&
        state != mucom88::PlaybackState::Paused) return;
    if (_fastForwarding) return;
    _fastForwarding = YES;
    _fastForwardButton.state = NSControlStateValueOn;
    _services->playback_coordinator->SetSpeed(
        static_cast<int>(_fastForwardMultiplier));
}

- (IBAction)endMomentaryFastForward:(id)sender
{
    (void)sender;
    if (!_fastForwarding) return;
    _fastForwarding = NO;
    _fastForwardButton.state = NSControlStateValueOff;
    _services->playback_coordinator->SetSpeed(1);
}

- (NSURL *)chooseResourceWithTitle:(NSString *)title directory:(BOOL)directory
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = title;
    panel.canChooseDirectories = directory;
    panel.canChooseFiles = !directory;
    panel.allowsMultipleSelection = NO;
    return [panel runModal] == NSModalResponseOK ? panel.URL : nil;
}

- (IBAction)chooseDefaultPCM:(id)sender
{
    (void)sender;
    NSURL *url = [self chooseResourceWithTitle:@"Choose Default PCM File"
        directory:NO];
    if (url != nil) _services->resources.default_pcm_file =
        url.fileSystemRepresentation;
}

- (IBAction)chooseDefaultVoice:(id)sender
{
    (void)sender;
    NSURL *url = [self chooseResourceWithTitle:@"Choose Default Voice File"
        directory:NO];
    if (url != nil) _services->resources.default_voice_file =
        url.fileSystemRepresentation;
}

- (IBAction)chooseExternalROMDirectory:(id)sender
{
    (void)sender;
    NSURL *url = [self chooseResourceWithTitle:@"Choose External ROM Directory"
        directory:YES];
    if (url != nil) _services->resources.external_rom_directory =
        url.fileSystemRepresentation;
}

- (IBAction)chooseRhythmDirectory:(id)sender
{
    (void)sender;
    NSURL *url = [self chooseResourceWithTitle:@"Choose Rhythm Directory"
        directory:YES];
    if (url != nil) _services->resources.rhythm_directory =
        url.fileSystemRepresentation;
}

- (IBAction)toggleExternalROM:(id)sender
{
    (void)sender;
    _services->resources.use_external_rom =
        !_services->resources.use_external_rom;
}

- (IBAction)clearResourceOverrides:(id)sender
{
    (void)sender;
    _services->resources = {};
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
    mucom88::CompileRequest request = [self configuredCompileRequest];
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
    state.playback_ui_ready = _services != nullptr &&
        _services->playback_coordinator != nullptr;
    if (state.playback_ui_ready) {
        const auto playback = _services->playback_coordinator->Snapshot();
        state.playback_state = playback.state;
        state.reconnect_available = playback.reconnect_available;
    }
    if (item.action == @selector(compileDocument:)) {
        return mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::Compile, state);
    }
    if (item.action == @selector(buildPcmFromData:) || item.action == @selector(buildPcmFromList:))
        return !_buildingPcm && _editorView != nil && _editorView.window.attachedSheet == nil;
    if (item.action == @selector(appendUsedVoices:))
        return state.compiler_ready && _editorView != nil && _editorView.window.attachedSheet == nil;
    if (item.action == @selector(removeN88LineNumbers:) ||
        item.action == @selector(convertGChannel:) ||
        item.action == @selector(addMetadataTags:) ||
        item.action == @selector(exportN88Source:)) {
        return _editorView != nil && _editorView.window.attachedSheet == nil;
    }
    if (item.action == @selector(goToLine:)) {
        return mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::GoToLine, state);
    }
    if (item.action == @selector(compileAndPlayDocument:)) return
        mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::CompileAndPlay, state);
    if (item.action == @selector(pauseResumePlayback:)) return
        mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::PauseResume, state);
    if (item.action == @selector(stopPlayback:)) return
        mucom88::IsEditorCommandEnabled(mucom88::EditorCommand::Stop, state);
    if (item.action == @selector(reconnectAudioDevice:)) return
        mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::Reconnect, state);
    if (item.action == @selector(changeAudioDevice:)) return
        state.playback_state == mucom88::PlaybackState::Idle ||
        state.playback_state == mucom88::PlaybackState::Finished ||
        state.playback_state == mucom88::PlaybackState::Failed ||
        state.playback_state == mucom88::PlaybackState::DeviceLost;
    if (item.action == @selector(toggleFastForward:) ||
        item.action == @selector(beginMomentaryFastForward:) ||
        item.action == @selector(endMomentaryFastForward:)) return
        mucom88::IsEditorCommandEnabled(
            mucom88::EditorCommand::FastForward, state);
    if (item.action == @selector(toggleExternalROM:)) {
        if ([(id)item isKindOfClass:NSMenuItem.class]) {
            ((NSMenuItem *)item).state = _services->resources.use_external_rom
                ? NSControlStateValueOn : NSControlStateValueOff;
        }
        return YES;
    }
    if (item.action == @selector(chooseDefaultPCM:) ||
        item.action == @selector(chooseDefaultVoice:) ||
        item.action == @selector(chooseExternalROMDirectory:) ||
        item.action == @selector(chooseRhythmDirectory:) ||
        item.action == @selector(clearResourceOverrides:)) return YES;
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
    if (_presentationObserver != nil) {
        [NSNotificationCenter.defaultCenter removeObserver:_presentationObserver];
        _presentationObserver = nil;
    }
    if (_services != nullptr) _services->playback_coordinator->DocumentClosed(
        _model->Snapshot().document_id);
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
    if (_presentationObserver != nil)
        [NSNotificationCenter.defaultCenter removeObserver:_presentationObserver];
    _compileOperation.Cancel();
}

@end

@interface MucomAppDelegate : NSObject <NSApplicationDelegate> {
    std::shared_ptr<mucom88::ApplicationServices> _services;
    id _eventMonitor;
    NSTimer *_audioDeviceEventTimer;
    MucomPlaybackPresentationController *_presentationController;
    BOOL _controlF1Held;
    MucomHomeWindowController *_homeController;
    MucomPlayerWindowController *_playerController;
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

    NSMenuItem *toolsItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:toolsItem];
    NSMenu *toolsMenu = [[NSMenu alloc] initWithTitle:@"Tools"];
    toolsItem.submenu = toolsMenu;
    AddMenuItem(toolsMenu, @"Remove N88 Line Numbers…",
        @selector(removeN88LineNumbers:), @"");
    AddMenuItem(toolsMenu, @"Convert G Channel q to @…",
        @selector(convertGChannel:), @"");

    AddMenuItem(toolsMenu, @"Add Metadata Tags…", @selector(addMetadataTags:), @"");
    AddMenuItem(toolsMenu, @"Append Used FM Voices…", @selector(appendUsedVoices:), @"");
    AddMenuItem(toolsMenu, @"Build PCM Bank from DATA…", @selector(buildPcmFromData:), @"");
    AddMenuItem(toolsMenu, @"Build PCM Bank from List…", @selector(buildPcmFromList:), @"");
    AddMenuItem(toolsMenu, @"Export N88-BASIC Source…", @selector(exportN88Source:), @"");

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
    AddMenuItem(buildMenu, @"Compile & Play (F5 / F12)",
        @selector(compileAndPlayDocument:), @"");

    NSMenuItem *playbackItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:playbackItem];
    NSMenu *playbackMenu = [[NSMenu alloc] initWithTitle:@"Playback"];
    playbackItem.submenu = playbackMenu;
    AddMenuItem(playbackMenu, @"Pause / Resume (Esc)",
        @selector(pauseResumePlayback:), @"");
    AddMenuItem(playbackMenu, @"Stop", @selector(stopPlayback:), @"");
    AddMenuItem(playbackMenu, @"Reconnect Audio Output",
        @selector(reconnectAudioDevice:), @"");
    AddMenuItem(playbackMenu, @"Fast Forward", @selector(toggleFastForward:), @"");
    [playbackMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *resourcesItem = AddMenuItem(
        playbackMenu, @"Resources", nil, @"");
    NSMenu *resourcesMenu = [[NSMenu alloc] initWithTitle:@"Resources"];
    resourcesItem.submenu = resourcesMenu;
    AddMenuItem(resourcesMenu, @"Choose Default PCM…",
        @selector(chooseDefaultPCM:), @"");
    AddMenuItem(resourcesMenu, @"Choose Default Voice…",
        @selector(chooseDefaultVoice:), @"");
    AddMenuItem(resourcesMenu, @"Choose Rhythm Directory…",
        @selector(chooseRhythmDirectory:), @"");
    AddMenuItem(resourcesMenu, @"Choose External ROM Directory…",
        @selector(chooseExternalROMDirectory:), @"");
    AddMenuItem(resourcesMenu, @"Use External ROM",
        @selector(toggleExternalROM:), @"");
    [resourcesMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(resourcesMenu, @"Clear Resource Overrides",
        @selector(clearResourceOverrides:), @"");

    NSMenuItem *windowItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:windowItem];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    windowItem.submenu = windowMenu;
    AddMenuItem(windowMenu, @"MUCOM88 Home",
        @selector(showHomeWindow:), @"").target = self;
    AddMenuItem(windowMenu, @"Player / Sound Monitor",
        @selector(showPlayerWindow:), @"").target = self;
    [windowMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(windowMenu, @"Minimize", @selector(performMiniaturize:), @"m");
    AddMenuItem(windowMenu, @"Zoom", @selector(performZoom:), @"");
    [windowMenu addItem:NSMenuItem.separatorItem];
    AddMenuItem(windowMenu, @"Bring All to Front", @selector(arrangeInFront:), @"");
    NSApp.windowsMenu = windowMenu;
}

- (IBAction)showHomeWindow:(id)sender
{
    if (_homeController == nil)
        _homeController = [[MucomHomeWindowController alloc]
            initWithServices:_services];
    [_homeController showWindow:sender];
    [_homeController.window makeKeyAndOrderFront:sender];
}

- (IBAction)showPlayerWindow:(id)sender
{
    if (_playerController == nil)
        _playerController = [[MucomPlayerWindowController alloc]
            initWithServices:_services];
    [_playerController showWindow:sender];
    [_playerController.window makeKeyAndOrderFront:sender];
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
    _presentationController = [[MucomPlaybackPresentationController alloc]
        initWithServices:_services];
    [_presentationController start];
    [self installMainMenu];
    __weak MucomAppDelegate *weakSelf = self;
    _eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        (NSEventMaskKeyDown | NSEventMaskKeyUp)
        handler:^NSEvent *(NSEvent *event) {
            MucomAppDelegate *delegate = weakSelf;
            if (delegate == nil) return event;
            NSString *characters = event.charactersIgnoringModifiers;
            const unichar key = characters.length > 0
                ? [characters characterAtIndex:0] : 0;
            const BOOL keyDown = event.type == NSEventTypeKeyDown;
            if (keyDown && !event.isARepeat &&
                (key == NSF5FunctionKey || key == NSF12FunctionKey)) {
                if ([NSApp sendAction:@selector(compileAndPlayDocument:)
                        to:nil from:nil]) return nil;
            }
            if (keyDown && key == 0x1b) {
                if ([NSApp sendAction:@selector(pauseResumePlayback:)
                        to:nil from:nil]) return nil;
            }
            const BOOL controlF1 = key == NSF1FunctionKey &&
                (event.modifierFlags & NSEventModifierFlagControl) != 0;
            if (controlF1 && keyDown && !delegate->_controlF1Held) {
                delegate->_controlF1Held = YES;
                if ([NSApp sendAction:@selector(beginMomentaryFastForward:)
                        to:nil from:nil]) return nil;
            } else if (key == NSF1FunctionKey && !keyDown &&
                delegate->_controlF1Held) {
                delegate->_controlF1Held = NO;
                [NSApp sendAction:@selector(endMomentaryFastForward:)
                    to:nil from:nil];
                return nil;
            }
            return event;
        }];
    __weak MucomAppDelegate *weakAudioDelegate = self;
    _audioDeviceEventTimer = [NSTimer scheduledTimerWithTimeInterval:0.2
        repeats:YES block:^(NSTimer *timer) {
            (void)timer;
            MucomAppDelegate *delegate = weakAudioDelegate;
            if (delegate == nil || delegate->_services == nullptr) return;
            delegate->_services->audio->PumpDeviceEvents();
        }];
}

- (void)applicationDidResignActive:(NSNotification *)notification
{
    (void)notification;
    if (_controlF1Held) {
        _controlF1Held = NO;
        [NSApp sendAction:@selector(endMomentaryFastForward:) to:nil from:nil];
    }
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
    if (_eventMonitor != nil) {
        [NSEvent removeMonitor:_eventMonitor];
        _eventMonitor = nil;
    }
    [_audioDeviceEventTimer invalidate];
    [_presentationController stop];
    _presentationController = nil;
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
