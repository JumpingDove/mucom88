#import "editor/macos/phase5_windows.h"

#include <filesystem>
#include <utility>

#include "editor/playback_presentation.h"

namespace {

NSString *Phase5String(const std::string &value)
{
    NSString *text = [[NSString alloc] initWithBytes:value.data()
        length:value.size() encoding:NSUTF8StringEncoding];
    return text != nil ? text : @"";
}

NSTextField *Phase5Label(NSString *value)
{
    NSTextField *field = [NSTextField labelWithString:value ?: @""];
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    return field;
}

NSScrollView *Phase5TableScroll(NSTableView *table)
{
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.documentView = table;
    return scroll;
}

void PresentServiceError(NSWindow *window, NSString *title,
    const mucom88::ServiceError &error)
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = Phase5String(error.message);
    if (window != nil) [alert beginSheetModalForWindow:window completionHandler:nil];
    else [alert runModal];
}

NSString *PlaylistStateText(mucom88::PlaylistState state)
{
    switch (state) {
    case mucom88::PlaylistState::Idle: return @"Idle";
    case mucom88::PlaylistState::Starting: return @"Starting";
    case mucom88::PlaylistState::Playing: return @"Playing";
    case mucom88::PlaylistState::Advancing: return @"Advancing";
    case mucom88::PlaylistState::Stopped: return @"Stopped";
    case mucom88::PlaylistState::Failed: return @"Failed";
    }
    return @"Unknown";
}

NSString *PlaylistEntryStateText(mucom88::PlaylistEntryState state)
{
    switch (state) {
    case mucom88::PlaylistEntryState::Pending: return @"Pending";
    case mucom88::PlaylistEntryState::Loading: return @"Loading";
    case mucom88::PlaylistEntryState::Compiling: return @"Compiling";
    case mucom88::PlaylistEntryState::Ready: return @"Ready";
    case mucom88::PlaylistEntryState::Playing: return @"Playing";
    case mucom88::PlaylistEntryState::Failed: return @"Failed";
    case mucom88::PlaylistEntryState::Skipped: return @"Skipped";
    }
    return @"Unknown";
}

} // namespace

NSNotificationName const MucomPlaybackPresentationDidUpdateNotification =
    @"MucomPlaybackPresentationDidUpdateNotification";

@interface MucomPlaybackPresentationUpdate () {
    mucom88::PlaybackCoordinatorSnapshot _playbackSnapshot;
    mucom88::PlaybackPresentation _playbackPresentation;
}
@end

@implementation MucomPlaybackPresentationUpdate

- (instancetype)initWithSnapshot:
    (const mucom88::PlaybackCoordinatorSnapshot &)snapshot
{
    self = [super init];
    if (self != nil) {
        _playbackSnapshot = snapshot;
        _playbackPresentation =
            mucom88::BuildPlaybackPresentation(snapshot.monitor);
    }
    return self;
}

- (const mucom88::PlaybackCoordinatorSnapshot &)playbackSnapshot
{
    return _playbackSnapshot;
}

- (const mucom88::PlaybackPresentation &)presentation
{
    return _playbackPresentation;
}

@end


@interface MucomPlaybackPresentationController () {
    std::shared_ptr<mucom88::ApplicationServices> _services;
    mucom88::PlaybackSubscriptionId _subscription;
    NSTimer *_timer;
    BOOL _running;
}
@end

@implementation MucomPlaybackPresentationController

- (instancetype)initWithServices:
    (std::shared_ptr<mucom88::ApplicationServices>)services
{
    self = [super init];
    if (self != nil) _services = std::move(services);
    return self;
}

- (void)publishSnapshot:
    (const mucom88::PlaybackCoordinatorSnapshot &)snapshot
{
    if (!_running) return;
    MucomPlaybackPresentationUpdate *update =
        [[MucomPlaybackPresentationUpdate alloc] initWithSnapshot:snapshot];
    [NSNotificationCenter.defaultCenter
        postNotificationName:MucomPlaybackPresentationDidUpdateNotification
        object:update];
}

- (void)start
{
    if (_running || _services == nullptr) return;
    _running = YES;
    __weak MucomPlaybackPresentationController *weakSelf = self;
    _subscription = _services->playback_coordinator->Subscribe(
        [weakSelf](mucom88::PlaybackCoordinatorSnapshot snapshot) {
            dispatch_async(dispatch_get_main_queue(), ^{
                MucomPlaybackPresentationController *controller = weakSelf;
                if (controller != nil) [controller publishSnapshot:snapshot];
            });
        });
    _timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 15.0)
        repeats:YES block:^(NSTimer *timer) {
            (void)timer;
            MucomPlaybackPresentationController *controller = weakSelf;
            if (controller == nil || controller->_services == nullptr) return;
            [controller publishSnapshot:
                controller->_services->playback_coordinator->Snapshot()];
        }];
    [self publishSnapshot:_services->playback_coordinator->Snapshot()];
}

- (void)stop
{
    if (!_running) return;
    _running = NO;
    [_timer invalidate];
    _timer = nil;
    if (_subscription != 0 && _services != nullptr) {
        _services->playback_coordinator->Unsubscribe(_subscription);
        _subscription = 0;
    }
}

- (void)dealloc
{
    [self stop];
}

@end

@interface MucomHomeWindowController () <NSTableViewDataSource, NSTableViewDelegate> {
    std::shared_ptr<mucom88::ApplicationServices> _services;
    mucom88::LibrarySnapshot _snapshot;
    mucom88::OperationHandle _scanOperation;
    mucom88::OperationHandle _compileOperation;
    mucom88::OperationHandle _exportOperation;
    NSTableView *_directoryTable;
    NSTableView *_songTable;
    NSTextField *_pathLabel;
    NSTextField *_statusLabel;
    NSTextField *_metadataLabel;
}
@end

@implementation MucomHomeWindowController

- (instancetype)initWithServices:
    (std::shared_ptr<mucom88::ApplicationServices>)services
{
    NSRect frame = NSMakeRect(160, 130, 1080, 620);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
        backing:NSBackingStoreBuffered defer:NO];
    window.title = @"MUCOM88 Home";
    self = [super initWithWindow:window];
    if (self == nil) return nil;
    _services = std::move(services);
    [self buildInterface];
    return self;
}

- (void)buildInterface
{
    NSView *content = self.window.contentView;
    NSButton *choose = [NSButton buttonWithTitle:@"Choose Folder…"
        target:self action:@selector(chooseFolder:)];
    NSButton *back = [NSButton buttonWithTitle:@"Back"
        target:self action:@selector(goBack:)];
    NSButton *refresh = [NSButton buttonWithTitle:@"Refresh"
        target:self action:@selector(refresh:)];
    NSButton *open = [NSButton buttonWithTitle:@"Open in Editor"
        target:self action:@selector(openInEditor:)];
    NSButton *play = [NSButton buttonWithTitle:@"Play"
        target:self action:@selector(playSelection:)];
    NSButton *exportMub = [NSButton buttonWithTitle:@"Export MUB…"
        target:self action:@selector(exportSelection:)];
    NSButton *playlist = [NSButton buttonWithTitle:@"Start Playlist"
        target:self action:@selector(startPlaylist:)];
    NSStackView *buttons = [NSStackView stackViewWithViews:
        @[choose, back, refresh, open, play, exportMub, playlist]];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 8.0;
    buttons.translatesAutoresizingMaskIntoConstraints = NO;

    _pathLabel = Phase5Label(@"No folder selected");
    _pathLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel = Phase5Label(@"Choose a folder to browse MUC and N88 files.");
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;

    _directoryTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    NSTableColumn *directoryColumn = [[NSTableColumn alloc]
        initWithIdentifier:@"directory"];
    directoryColumn.title = @"Folders";
    directoryColumn.width = 210;
    [_directoryTable addTableColumn:directoryColumn];
    _directoryTable.headerView = nil;
    _directoryTable.delegate = self;
    _directoryTable.dataSource = self;
    _directoryTable.target = self;
    _directoryTable.doubleAction = @selector(openDirectory:);

    _songTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    const struct { NSString *identifier; NSString *title; CGFloat width; } columns[] = {
        {@"name", @"File", 180}, {@"kind", @"Kind", 65},
        {@"title", @"Title", 190}, {@"composer", @"Composer", 150}
    };
    for (const auto &column : columns) {
        NSTableColumn *tableColumn = [[NSTableColumn alloc]
            initWithIdentifier:column.identifier];
        tableColumn.title = column.title;
        tableColumn.width = column.width;
        [_songTable addTableColumn:tableColumn];
    }
    _songTable.delegate = self;
    _songTable.dataSource = self;
    _songTable.target = self;
    _songTable.doubleAction = @selector(openInEditor:);

    _metadataLabel = [NSTextField wrappingLabelWithString:
        @"Select a song to inspect its metadata."];
    _metadataLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _metadataLabel.selectable = YES;
    NSView *inspector = [[NSView alloc] initWithFrame:NSZeroRect];
    [inspector addSubview:_metadataLabel];
    [NSLayoutConstraint activateConstraints:@[
        [_metadataLabel.leadingAnchor constraintEqualToAnchor:inspector.leadingAnchor
            constant:12],
        [_metadataLabel.trailingAnchor constraintEqualToAnchor:inspector.trailingAnchor
            constant:-12],
        [_metadataLabel.topAnchor constraintEqualToAnchor:inspector.topAnchor
            constant:12]
    ]];

    NSSplitView *split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    split.translatesAutoresizingMaskIntoConstraints = NO;
    split.vertical = YES;
    split.dividerStyle = NSSplitViewDividerStyleThin;
    NSScrollView *directoryScroll = Phase5TableScroll(_directoryTable);
    NSScrollView *songScroll = Phase5TableScroll(_songTable);
    [split addSubview:directoryScroll];
    [split addSubview:songScroll];
    [split addSubview:inspector];
    [directoryScroll.widthAnchor constraintGreaterThanOrEqualToConstant:170].active = YES;
    [songScroll.widthAnchor constraintGreaterThanOrEqualToConstant:430].active = YES;
    [inspector.widthAnchor constraintGreaterThanOrEqualToConstant:230].active = YES;

    [content addSubview:buttons];
    [content addSubview:_pathLabel];
    [content addSubview:split];
    [content addSubview:_statusLabel];
    [NSLayoutConstraint activateConstraints:@[
        [buttons.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12],
        [buttons.trailingAnchor constraintLessThanOrEqualToAnchor:content.trailingAnchor
            constant:-12],
        [buttons.topAnchor constraintEqualToAnchor:content.topAnchor constant:12],
        [_pathLabel.leadingAnchor constraintEqualToAnchor:content.leadingAnchor
            constant:12],
        [_pathLabel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor
            constant:-12],
        [_pathLabel.topAnchor constraintEqualToAnchor:buttons.bottomAnchor constant:8],
        [split.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12],
        [split.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],
        [split.topAnchor constraintEqualToAnchor:_pathLabel.bottomAnchor constant:8],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:content.leadingAnchor
            constant:12],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor
            constant:-12],
        [_statusLabel.topAnchor constraintEqualToAnchor:split.bottomAnchor constant:8],
        [_statusLabel.bottomAnchor constraintEqualToAnchor:content.bottomAnchor
            constant:-10]
    ]];
}

- (void)showWindow:(id)sender
{
    [super showWindow:sender];
    if (_snapshot.current_directory.empty())
        [self scanDirectory:NSHomeDirectory()];
}

- (void)scanDirectory:(NSString *)directory
{
    if (directory.length == 0 || _services == nullptr) return;
    _scanOperation.Cancel();
    _statusLabel.stringValue = @"Scanning…";
    __weak MucomHomeWindowController *weakSelf = self;
    _scanOperation = _services->library->ScanAsync(
        directory.fileSystemRepresentation,
        [weakSelf](mucom88::ServiceResult<mucom88::LibrarySnapshot> result) {
            MucomHomeWindowController *controller = weakSelf;
            if (controller == nil) return;
            if (!result.Succeeded()) {
                controller->_statusLabel.stringValue =
                    Phase5String(result.error.message);
                return;
            }
            controller->_snapshot = std::move(result.value);
            controller->_pathLabel.stringValue =
                Phase5String(controller->_snapshot.current_directory);
            controller->_statusLabel.stringValue = [NSString stringWithFormat:
                @"%lu folders, %lu songs",
                static_cast<unsigned long>(controller->_snapshot.directories.size()),
                static_cast<unsigned long>(controller->_snapshot.songs.size())];
            [controller->_directoryTable reloadData];
            [controller->_songTable reloadData];
            controller->_metadataLabel.stringValue =
                @"Select a song to inspect its metadata.";
        });
}

- (const mucom88::LibraryEntry *)selectedSong
{
    const NSInteger row = _songTable.selectedRow;
    if (row < 0 || static_cast<std::size_t>(row) >= _snapshot.songs.size())
        return nullptr;
    return &_snapshot.songs[static_cast<std::size_t>(row)];
}

- (IBAction)chooseFolder:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK)
        [self scanDirectory:panel.URL.path];
}

- (IBAction)goBack:(id)sender
{
    (void)sender;
    if (!_snapshot.parent_available) return;
    std::filesystem::path parent =
        std::filesystem::path(_snapshot.current_directory).parent_path();
    [self scanDirectory:Phase5String(parent.string())];
}

- (IBAction)refresh:(id)sender
{
    (void)sender;
    if (!_snapshot.current_directory.empty())
        [self scanDirectory:Phase5String(_snapshot.current_directory)];
}

- (IBAction)openDirectory:(id)sender
{
    (void)sender;
    const NSInteger row = _directoryTable.clickedRow >= 0
        ? _directoryTable.clickedRow : _directoryTable.selectedRow;
    if (row < 0 || static_cast<std::size_t>(row) >=
        _snapshot.directories.size()) return;
    [self scanDirectory:Phase5String(
        _snapshot.directories[static_cast<std::size_t>(row)].absolute_path)];
}

- (IBAction)openInEditor:(id)sender
{
    (void)sender;
    const auto *entry = [self selectedSong];
    if (entry == nullptr) return;
    NSURL *url = [NSURL fileURLWithPath:Phase5String(entry->absolute_path)];
    [NSDocumentController.sharedDocumentController
        openDocumentWithContentsOfURL:url display:YES
        completionHandler:^(NSDocument *document, BOOL alreadyOpen, NSError *error) {
            (void)document;
            (void)alreadyOpen;
            if (error != nil) [NSApp presentError:error];
        }];
}

- (IBAction)playSelection:(id)sender
{
    (void)sender;
    const auto *entry = [self selectedSong];
    if (entry == nullptr) return;
    const auto request = _services->library->LoadCompileRequest(
        entry->absolute_path, _services->resources);
    if (!request.Succeeded()) {
        PresentServiceError(self.window, @"Unable to load song", request.error);
        return;
    }
    _compileOperation.Cancel();
    _statusLabel.stringValue = @"Compiling for direct playback…";
    __weak MucomHomeWindowController *weakSelf = self;
    _compileOperation = _services->playback_coordinator->CompileAndPlay(
        request.value, {},
        [weakSelf](mucom88::CompileResult result) {
            MucomHomeWindowController *controller = weakSelf;
            if (controller == nil) return;
            controller->_statusLabel.stringValue = result.Succeeded()
                ? @"Direct playback started"
                : Phase5String(result.error.message);
        }, mucom88::PlaybackOwner::Browser(mucom88::NextPlaybackOwnerToken()));
}

- (IBAction)exportSelection:(id)sender
{
    (void)sender;
    const auto *entry = [self selectedSong];
    if (entry == nullptr) return;
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = [[Phase5String(entry->display_name)
        stringByDeletingPathExtension] stringByAppendingPathExtension:@"mub"];
    if ([panel runModal] != NSModalResponseOK) return;
    const std::string destination = panel.URL.fileSystemRepresentation;
    const auto request = _services->library->LoadCompileRequest(
        entry->absolute_path, _services->resources);
    if (!request.Succeeded()) {
        PresentServiceError(self.window, @"Unable to load song", request.error);
        return;
    }
    _compileOperation.Cancel();
    _exportOperation.Cancel();
    _statusLabel.stringValue = @"Compiling for MUB export…";
    __weak MucomHomeWindowController *weakSelf = self;
    _compileOperation = _services->compiler->CompileAsync(request.value,
        [weakSelf, destination](mucom88::CompileResult compiled) {
            MucomHomeWindowController *controller = weakSelf;
            if (controller == nil) return;
            if (!compiled.Succeeded()) {
                controller->_statusLabel.stringValue =
                    Phase5String(compiled.error.message);
                return;
            }
            mucom88::ExportRequest exportRequest;
            exportRequest.song = compiled.song;
            exportRequest.format = mucom88::ExportFormat::Mub;
            exportRequest.destination_path = destination;
            controller->_exportOperation = controller->_services->exporter->ExportAsync(
                std::move(exportRequest), {},
                [weakSelf](mucom88::ExportResult result) {
                    MucomHomeWindowController *current = weakSelf;
                    if (current == nil) return;
                    current->_statusLabel.stringValue = result.Succeeded()
                        ? @"MUB export completed"
                        : Phase5String(result.error.message);
                });
        });
}

- (IBAction)startPlaylist:(id)sender
{
    (void)sender;
    const auto operation = _services->playlist->Start(
        _snapshot.songs, _services->resources);
    _statusLabel.stringValue = operation.IsValid()
        ? @"Playlist started" : @"No playable MUC files";
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    return tableView == _directoryTable
        ? static_cast<NSInteger>(_snapshot.directories.size())
        : static_cast<NSInteger>(_snapshot.songs.size());
}

- (NSView *)tableView:(NSTableView *)tableView
    viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSTextField *field = Phase5Label(@"");
    if (tableView == _directoryTable) {
        field.stringValue = Phase5String(
            _snapshot.directories[static_cast<std::size_t>(row)].display_name);
        return field;
    }
    const auto &entry = _snapshot.songs[static_cast<std::size_t>(row)];
    NSString *identifier = tableColumn.identifier;
    if ([identifier isEqualToString:@"name"])
        field.stringValue = Phase5String(entry.display_name);
    else if ([identifier isEqualToString:@"kind"])
        field.stringValue = entry.kind == mucom88::DocumentKind::Muc
            ? @"MUC" : @"N88";
    else if ([identifier isEqualToString:@"title"])
        field.stringValue = Phase5String(mucom88::MetadataService::DisplayTitle(
            entry.metadata, entry.absolute_path));
    else if ([identifier isEqualToString:@"composer"])
        field.stringValue = Phase5String(entry.metadata.composer);
    return field;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    if (notification.object != _songTable) return;
    const auto *entry = [self selectedSong];
    if (entry == nullptr) {
        _metadataLabel.stringValue = @"Select a song to inspect its metadata.";
        return;
    }
    const auto &metadata = entry->metadata;
    NSString *error = entry->file_error
        ? [@"\nError: " stringByAppendingString:
            Phase5String(entry->file_error.message)] : @"";
    _metadataLabel.stringValue = [NSString stringWithFormat:
        @"Title: %@\nAuthor: %@\nComposer: %@\nDate: %@\nVoice: %@\nPCM: %@\n\n%@%@",
        Phase5String(metadata.title), Phase5String(metadata.author),
        Phase5String(metadata.composer), Phase5String(metadata.date),
        Phase5String(metadata.voice), Phase5String(metadata.pcm),
        Phase5String(metadata.comment), error];
}

- (void)dealloc
{
    _scanOperation.Cancel();
    _compileOperation.Cancel();
    _exportOperation.Cancel();
}

@end

@interface MucomPlayerWindowController () <NSTableViewDataSource, NSTableViewDelegate> {
    std::shared_ptr<mucom88::ApplicationServices> _services;
    std::shared_ptr<const mucom88::MonitorSnapshot> _lastMonitorSnapshot;
    mucom88::PlaybackPresentation _presentation;
    mucom88::PlaylistSnapshot _playlistSnapshot;
    id _presentationObserver;
    NSTableView *_channelTable;
    NSTableView *_playlistTable;
    NSTextField *_nowPlaying;
    NSTextField *_diagnostics;
    NSTextField *_playlistStatus;
    NSTextField *_secondsField;
    NSTextField *_percentField;
    NSButton *_loopButton;
}
@end

@implementation MucomPlayerWindowController

- (instancetype)initWithServices:
    (std::shared_ptr<mucom88::ApplicationServices>)services
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:
        NSMakeRect(190, 100, 1120, 700)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
        backing:NSBackingStoreBuffered defer:NO];
    window.title = @"MUCOM88 Player / Sound Monitor";
    self = [super initWithWindow:window];
    if (self == nil) return nil;
    _services = std::move(services);
    [self buildInterface];
    __weak MucomPlayerWindowController *weakSelf = self;
    _presentationObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:MucomPlaybackPresentationDidUpdateNotification
        object:nil queue:NSOperationQueue.mainQueue
        usingBlock:^(NSNotification *notification) {
            MucomPlayerWindowController *controller = weakSelf;
            if (controller == nil || !controller.window.isVisible) return;
            MucomPlaybackPresentationUpdate *update =
                (MucomPlaybackPresentationUpdate *)notification.object;
            [controller refreshPresentationWithUpdate:update];
        }];
    [self refreshPresentation];
    return self;
}

- (void)buildInterface
{
    NSView *content = self.window.contentView;
    _nowPlaying = Phase5Label(@"Nothing playing");
    _nowPlaying.font = [NSFont boldSystemFontOfSize:14];
    _diagnostics = Phase5Label(@"Idle");
    _playlistStatus = Phase5Label(@"Playlist: Idle");
    for (NSTextField *label in @[_nowPlaying, _diagnostics, _playlistStatus])
        label.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton *pause = [NSButton buttonWithTitle:@"Pause / Resume"
        target:self action:@selector(pauseResume:)];
    NSButton *previous = [NSButton buttonWithTitle:@"Previous"
        target:self action:@selector(previous:)];
    NSButton *next = [NSButton buttonWithTitle:@"Next"
        target:self action:@selector(next:)];
    NSButton *stop = [NSButton buttonWithTitle:@"Stop"
        target:self action:@selector(stop:)];
    _loopButton = [NSButton checkboxWithTitle:@"Loop folder"
        target:nil action:nil];
    _loopButton.state = NSControlStateValueOn;
    _secondsField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _secondsField.stringValue = @"90";
    _secondsField.placeholderString = @"seconds";
    _percentField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _percentField.stringValue = @"150";
    _percentField.placeholderString = @"percent";
    NSButton *apply = [NSButton buttonWithTitle:@"Apply Policy"
        target:self action:@selector(applyPolicy:)];
    NSStackView *controls = [NSStackView stackViewWithViews:
        @[pause, previous, next, stop, _loopButton,
          Phase5Label(@"Max sec"), _secondsField,
          Phase5Label(@"Max %"), _percentField, apply]];
    controls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    controls.spacing = 7;
    controls.translatesAutoresizingMaskIntoConstraints = NO;
    [_secondsField.widthAnchor constraintEqualToConstant:58].active = YES;
    [_percentField.widthAnchor constraintEqualToConstant:58].active = YES;

    _channelTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    const struct { NSString *identifier; NSString *title; CGFloat width; } channelColumns[] = {
        {@"channel", @"Ch", 35}, {@"mute", @"Mute", 45},
        {@"voice", @"Voice", 50}, {@"volume", @"Vol", 45},
        {@"detune", @"Detune", 55}, {@"address", @"Addr", 55},
        {@"note", @"Note", 50}, {@"key", @"Key", 40},
        {@"lfo", @"LFO", 40}, {@"reverb", @"Rev", 40},
        {@"pan", @"Pan", 40}, {@"quantize", @"Q", 40}
    };
    for (const auto &column : channelColumns) {
        NSTableColumn *item = [[NSTableColumn alloc]
            initWithIdentifier:column.identifier];
        item.title = column.title;
        item.width = column.width;
        [_channelTable addTableColumn:item];
    }
    _channelTable.dataSource = self;
    _channelTable.delegate = self;

    _playlistTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    for (const auto &column : {
        std::pair<NSString *, NSString *>(@"playlistName", @"Playlist"),
        std::pair<NSString *, NSString *>(@"playlistState", @"State"),
        std::pair<NSString *, NSString *>(@"playlistError", @"Error")}) {
        NSTableColumn *item = [[NSTableColumn alloc]
            initWithIdentifier:column.first];
        item.title = column.second;
        item.width = [column.first isEqualToString:@"playlistName"] ? 260 : 150;
        [_playlistTable addTableColumn:item];
    }
    _playlistTable.dataSource = self;
    _playlistTable.delegate = self;

    NSSplitView *split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    split.translatesAutoresizingMaskIntoConstraints = NO;
    split.vertical = NO;
    split.dividerStyle = NSSplitViewDividerStyleThin;
    NSScrollView *channelScroll = Phase5TableScroll(_channelTable);
    NSScrollView *playlistScroll = Phase5TableScroll(_playlistTable);
    [split addSubview:channelScroll];
    [split addSubview:playlistScroll];
    // NSSplitView lays out its children, but an Auto Layout child without an
    // intrinsic height may otherwise collapse to zero. Keep both panes
    // visible while still allowing the divider and window resize to adjust
    // their actual sizes.
    [channelScroll.heightAnchor constraintGreaterThanOrEqualToConstant:250].active = YES;
    [playlistScroll.heightAnchor constraintGreaterThanOrEqualToConstant:130].active = YES;

    [content addSubview:_nowPlaying];
    [content addSubview:_diagnostics];
    [content addSubview:controls];
    [content addSubview:_playlistStatus];
    [content addSubview:split];
    [NSLayoutConstraint activateConstraints:@[
        [_nowPlaying.leadingAnchor constraintEqualToAnchor:content.leadingAnchor
            constant:12],
        [_nowPlaying.trailingAnchor constraintEqualToAnchor:content.trailingAnchor
            constant:-12],
        [_nowPlaying.topAnchor constraintEqualToAnchor:content.topAnchor constant:12],
        [_diagnostics.leadingAnchor constraintEqualToAnchor:_nowPlaying.leadingAnchor],
        [_diagnostics.trailingAnchor constraintEqualToAnchor:_nowPlaying.trailingAnchor],
        [_diagnostics.topAnchor constraintEqualToAnchor:_nowPlaying.bottomAnchor
            constant:6],
        [controls.leadingAnchor constraintEqualToAnchor:_nowPlaying.leadingAnchor],
        [controls.trailingAnchor constraintLessThanOrEqualToAnchor:
            content.trailingAnchor constant:-12],
        [controls.topAnchor constraintEqualToAnchor:_diagnostics.bottomAnchor
            constant:9],
        [_playlistStatus.leadingAnchor constraintEqualToAnchor:_nowPlaying.leadingAnchor],
        [_playlistStatus.trailingAnchor constraintEqualToAnchor:_nowPlaying.trailingAnchor],
        [_playlistStatus.topAnchor constraintEqualToAnchor:controls.bottomAnchor
            constant:8],
        [split.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12],
        [split.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],
        [split.topAnchor constraintEqualToAnchor:_playlistStatus.bottomAnchor constant:8],
        [split.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12]
    ]];
}

- (void)refreshPresentation
{
    if (_services == nullptr) return;
    const auto playback = _services->playback_coordinator->Snapshot();
    MucomPlaybackPresentationUpdate *update =
        [[MucomPlaybackPresentationUpdate alloc] initWithSnapshot:playback];
    [self refreshPresentationWithUpdate:update];
}

- (void)refreshPresentationWithUpdate:
    (MucomPlaybackPresentationUpdate *)update
{
    if (_services == nullptr || update == nil) return;
    const auto &playback = [update playbackSnapshot];
    const bool reloadChannels = playback.monitor != _lastMonitorSnapshot;
    _lastMonitorSnapshot = playback.monitor;
    _presentation = [update presentation];
    _playlistSnapshot = _services->playlist->Snapshot();
    if (playback.now_playing) {
        const auto &playing = *playback.now_playing;
        _nowPlaying.stringValue = [NSString stringWithFormat:@"Now Playing: %@",
            Phase5String(mucom88::MetadataService::DisplayTitle(
                playing.metadata, playing.source_path))];
    } else {
        _nowPlaying.stringValue = @"Nothing playing";
    }
    if (_presentation.state == mucom88::PlaybackState::Idle ||
        _presentation.state == mucom88::PlaybackState::Preparing) {
        NSString *stateText = _presentation.state == mucom88::PlaybackState::Idle
            ? @"Idle" : @"Preparing";
        _diagnostics.stringValue = [NSString stringWithFormat:
            @"%@ • underrun %llu • dropped %llu • refill %llu", stateText,
            static_cast<unsigned long long>(_presentation.underruns),
            static_cast<unsigned long long>(_presentation.dropped_frames),
            static_cast<unsigned long long>(_presentation.refill_events)];
    } else {
        _diagnostics.stringValue = [NSString stringWithFormat:
            @"Session %llu • state %ld • driver %d • count %lld / %lld • loop %lld • speed %dx • underrun %llu • dropped %llu • refill %llu",
            static_cast<unsigned long long>(_presentation.session_id),
            static_cast<long>(_presentation.state),
            static_cast<int>(_presentation.driver),
            static_cast<long long>(_presentation.current_count),
            static_cast<long long>(_presentation.maximum_count),
            static_cast<long long>(_presentation.loop_count), _presentation.speed,
            static_cast<unsigned long long>(_presentation.underruns),
            static_cast<unsigned long long>(_presentation.dropped_frames),
            static_cast<unsigned long long>(_presentation.refill_events)];
    }
    _playlistStatus.stringValue = [NSString stringWithFormat:
        @"Playlist: %@ • %lu entries", PlaylistStateText(_playlistSnapshot.state),
        static_cast<unsigned long>(_playlistSnapshot.entries.size())];
    if (reloadChannels) [_channelTable reloadData];
    [_playlistTable reloadData];
    if (_playlistSnapshot.current_index < _playlistSnapshot.entries.size()) {
        [_playlistTable selectRowIndexes:[NSIndexSet indexSetWithIndex:
            _playlistSnapshot.current_index] byExtendingSelection:NO];
    }
}

- (IBAction)pauseResume:(id)sender
{
    (void)sender;
    _services->playback_coordinator->TogglePauseResume();
}
- (IBAction)previous:(id)sender
{
    (void)sender;
    _services->playlist->Previous();
}
- (IBAction)next:(id)sender
{
    (void)sender;
    _services->playlist->Next();
}
- (IBAction)stop:(id)sender
{
    (void)sender;
    const auto playlist = _services->playlist->Snapshot();
    if (playlist.state == mucom88::PlaylistState::Playing ||
        playlist.state == mucom88::PlaylistState::Starting ||
        playlist.state == mucom88::PlaylistState::Advancing)
        _services->playlist->Stop();
    else
        _services->playback_coordinator->Stop();
}

- (IBAction)applyPolicy:(id)sender
{
    (void)sender;
    mucom88::PlaylistPolicy policy = _services->playlist->Snapshot().policy;
    policy.loop_folder = _loopButton.state == NSControlStateValueOn;
    policy.maximum_play_seconds = _secondsField.intValue;
    policy.maximum_count_percent = _percentField.intValue;
    const auto error = _services->playlist->SetPolicy(policy);
    if (error) PresentServiceError(self.window, @"Invalid playlist policy", error);
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    return tableView == _channelTable
        ? static_cast<NSInteger>(_presentation.channels.size())
        : static_cast<NSInteger>(_playlistSnapshot.entries.size());
}

- (NSView *)tableView:(NSTableView *)tableView
    viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSTextField *field = Phase5Label(@"");
    NSString *identifier = tableColumn.identifier;
    if (tableView == _playlistTable) {
        const auto &entry = _playlistSnapshot.entries[static_cast<std::size_t>(row)];
        if ([identifier isEqualToString:@"playlistName"])
            field.stringValue = Phase5String(
                std::filesystem::path(entry.path).filename().string());
        else if ([identifier isEqualToString:@"playlistState"])
            field.stringValue = PlaylistEntryStateText(entry.state);
        else
            field.stringValue = Phase5String(entry.error.message);
        return field;
    }
    const auto &channel = _presentation.channels[static_cast<std::size_t>(row)];
    if ([identifier isEqualToString:@"channel"])
        field.stringValue = Phase5String(channel.name);
    else if ([identifier isEqualToString:@"mute"])
        field.stringValue = channel.mute ? @"Yes" : @"";
    else if ([identifier isEqualToString:@"voice"])
        field.stringValue = Phase5String(channel.voice_text);
    else if ([identifier isEqualToString:@"volume"])
        field.integerValue = channel.volume;
    else if ([identifier isEqualToString:@"detune"])
        field.integerValue = channel.detune;
    else if ([identifier isEqualToString:@"address"])
        field.stringValue = Phase5String(channel.address);
    else if ([identifier isEqualToString:@"note"])
        field.stringValue = Phase5String(channel.note);
    else if ([identifier isEqualToString:@"key"])
        field.stringValue = channel.key_on ? @"On" : @"";
    else if ([identifier isEqualToString:@"lfo"])
        field.stringValue = channel.lfo ? @"On" : @"";
    else if ([identifier isEqualToString:@"reverb"])
        field.stringValue = channel.reverb ? @"On" : @"";
    else if ([identifier isEqualToString:@"pan"])
        field.stringValue = Phase5String(channel.pan);
    else if ([identifier isEqualToString:@"quantize"])
        field.integerValue = channel.quantize;
    return field;
}

- (void)dealloc
{
    if (_presentationObserver != nil)
        [NSNotificationCenter.defaultCenter removeObserver:_presentationObserver];
}

@end
