// This file is part of NovaLINK.
//
// Captures a real hardware microphone and injects its PCM into NovaLINKDevice so that
// clients reading the virtual input (Zoom, OBS, …) receive desktop audio + mic mixed.
// Local playthrough hosts still hear desktop-only (see driver ReadInput mix rules).

#ifndef NovaLINKApp__NovaLINKMicInputMixer
#define NovaLINKApp__NovaLINKMicInputMixer

#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKMicInputMixer : NSObject

+ (instancetype) sharedInstance;

// Start capturing (or keep capturing) a non-NovaLINK input and inject into NovaLINKDevice.
// No-ops when already running against the same mic / sample rate — avoids tearing down
// CoreAudio input IO (which can re-trigger stacked Microphone TCC dialogs).
- (void) ensureStarted;

// Force stop.
- (void) stop;

@end

#pragma clang assume_nonnull end

#endif /* NovaLINKApp__NovaLINKMicInputMixer */
