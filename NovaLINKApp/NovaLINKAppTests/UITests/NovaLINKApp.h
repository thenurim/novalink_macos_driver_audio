/*
 * NovaLINKApp.h
 *
 * Generated with
 * sdef "/Applications/NovaLINK Audio Passthrough.app" | sdp -fh --basename NovaLINKApp
 */

#import <AppKit/AppKit.h>
#import <ScriptingBridge/ScriptingBridge.h>


@class NovaLINKAppOutputDevice, NovaLINKAppApplication;



/*
 * NovaLINK
 */

// A hardware device that can play audio
@interface NovaLINKAppOutputDevice : SBObject

@property (copy, readonly) NSString *name;  // The name of the output device.
@property BOOL selected;  // Is this the device to be used for audio output?

@end

// The application program
@interface NovaLINKAppApplication : SBApplication

- (SBElementArray<NovaLINKAppOutputDevice *> *) outputDevices;

@property (copy) NovaLINKAppOutputDevice *selectedOutputDevice;  // The device to be used for audio output

@end

