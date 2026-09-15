// This file is part of NovaLINK.
//
// Hosts CAPlayThrough-style IO in NovaLINKXPCHelper when the companion app is not running,
// so selecting NovaLINK as the output device does not silence audio.

#ifndef NovaLINKXPCHelper__NovaLINKFallbackPlayThrough
#define NovaLINKXPCHelper__NovaLINKFallbackPlayThrough

// System Includes
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>


#pragma clang assume_nonnull begin

@interface NovaLINKFallbackPlayThrough : NSObject

+ (instancetype) sharedInstance;

// Starts (or restarts) fallback playthrough from NovaLINK → a non-NovaLINK system default.
// Returns an NSError whose code is one of the kNovaLINKXPC_* / kNovaLINKErrorCode_* values.
- (NSError*) startForUISoundsDevice:(BOOL)isUI;

- (void) stop;

@end

#pragma clang assume_nonnull end

#endif /* NovaLINKXPCHelper__NovaLINKFallbackPlayThrough */
