#import <AppKit/AppKit.h>

#ifdef __cplusplus
#include <memory>
#include "editor/application_services.h"
#include "editor/playback_presentation.h"

FOUNDATION_EXPORT NSNotificationName const
    MucomPlaybackPresentationDidUpdateNotification;

@interface MucomPlaybackPresentationUpdate : NSObject
- (instancetype)initWithSnapshot:
    (const mucom88::PlaybackCoordinatorSnapshot &)snapshot;
- (const mucom88::PlaybackCoordinatorSnapshot &)playbackSnapshot;
- (const mucom88::PlaybackPresentation &)presentation;
@end

@interface MucomPlaybackPresentationController : NSObject
- (instancetype)initWithServices:
    (std::shared_ptr<mucom88::ApplicationServices>)services;
- (void)start;
- (void)stop;
@end

@interface MucomHomeWindowController : NSWindowController
- (instancetype)initWithServices:
    (std::shared_ptr<mucom88::ApplicationServices>)services;
@end

@interface MucomPlayerWindowController : NSWindowController
- (instancetype)initWithServices:
    (std::shared_ptr<mucom88::ApplicationServices>)services;
@end
#endif
