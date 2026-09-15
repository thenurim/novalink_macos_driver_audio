// This file is part of NovaLINK.
//
// Serializes Microphone TCC so CoreAudio input StartIOProc calls cannot stack
// identical "wants to access the microphone" dialogs.

#ifndef NovaLINKApp__NovaLINKMicrophoneAccess
#define NovaLINKApp__NovaLINKMicrophoneAccess

#include <stdbool.h>

#ifdef __OBJC__
#import <Foundation/Foundation.h>

#pragma clang assume_nonnull begin

@interface NovaLINKMicrophoneAccess : NSObject

// YES only when AVAuthorizationStatusAuthorized (macOS 10.14+). Pre-10.14 always YES.
+ (BOOL) isAuthorized;

// If already authorized/denied, invokes completion immediately on the main queue.
// If not determined, shows at most one system prompt for this process.
+ (void) requestAccessIfNeededWithCompletion:(void (^)(BOOL granted))completion;

@end

#pragma clang assume_nonnull end
#endif /* __OBJC__ */

#ifdef __cplusplus
extern "C" {
#endif

// C++-callable gate used by NovaLINKPlayThrough before StartIOProc on input devices.
bool NovaLINKMicrophoneAccessIsAuthorized(void);

#ifdef __cplusplus
}
#endif

#endif /* NovaLINKApp__NovaLINKMicrophoneAccess */

