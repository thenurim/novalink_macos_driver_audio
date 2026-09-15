// This file is part of NovaLINK.
//
// Captures a real hardware microphone and injects its PCM into NovaLINKDevice so that
// clients reading the virtual input (Zoom, OBS, …) receive desktop audio + mic mixed.
// Local playthrough hosts still hear desktop-only (see driver ReadInput mix rules).
//
// Hardware mic IO is demand-driven: started only while a non-passthrough client is
// reading NovaLINK input (custom property 'irin'), so the macOS microphone privacy
// indicator is not left on permanently.

#ifndef NovaLINKApp__NovaLINKMicInputMixer
#define NovaLINKApp__NovaLINKMicInputMixer

#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKMicInputMixer : NSObject

+ (instancetype) sharedInstance;

// Listen for capture-client demand on NovaLINKDevice and start/stop accordingly.
// Safe to call more than once.
- (void) startDemandMonitoring;

// Re-evaluate demand (e.g. after output/sample-rate changes). Starts only if a
// capture client is reading NovaLINK input; otherwise stops.
- (void) syncToCaptureDemand;

// Force stop and tear down the demand listener (process teardown).
- (void) stop;

@end

#pragma clang assume_nonnull end

#endif /* NovaLINKApp__NovaLINKMicInputMixer */
