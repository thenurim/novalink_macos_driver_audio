// This file is part of NovaLINK.

#import "NovaLINKMicrophoneAccess.h"

#import <AVFoundation/AVCaptureDevice.h>


@implementation NovaLINKMicrophoneAccess

+ (BOOL) isAuthorized {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
    if (@available(macOS 10.14, *)) {
        return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]
                == AVAuthorizationStatusAuthorized;
    }
#endif
    return YES;
}

+ (void) requestAccessIfNeededWithCompletion:(void (^)(BOOL granted))completion {
    void (^finish)(BOOL) = ^(BOOL granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(granted);
        });
    };

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
    if (@available(macOS 10.14, *)) {
        AVAuthorizationStatus status =
                [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusAuthorized) {
            finish(YES);
            return;
        }
        if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
            finish(NO);
            return;
        }

        // NotDetermined — coalesce concurrent callers into a single system prompt.
        static NSMutableArray<void (^)(BOOL)>* waiters = nil;
        static BOOL requestStarted = NO;

        @synchronized (self) {
            if (!waiters) {
                waiters = [NSMutableArray new];
            }
            [waiters addObject:[finish copy]];
            if (requestStarted) {
                return;
            }
            requestStarted = YES;
        }

        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
            NSArray<void (^)(BOOL)>* callbacks;
            @synchronized (self) {
                callbacks = [waiters copy];
                [waiters removeAllObjects];
                // Allow a future prompt only if the user somehow resets TCC while we run.
                if (!granted) {
                    requestStarted = NO;
                }
            }
            for (void (^cb)(BOOL) in callbacks) {
                cb(granted);
            }
        }];
        return;
    }
#endif
    finish(YES);
}

@end

bool NovaLINKMicrophoneAccessIsAuthorized(void) {
    return [NovaLINKMicrophoneAccess isAuthorized] ? true : false;
}
