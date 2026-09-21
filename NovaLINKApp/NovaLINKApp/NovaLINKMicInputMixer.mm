// This file is part of NovaLINK.
//
// Captures a real hardware microphone and injects its PCM into NovaLINKDevice so that
// clients reading the virtual input (Zoom, OBS, …) receive desktop audio + mic mixed.
// Local playthrough hosts still hear desktop-only (see driver ReadInput mix rules).
// Hardware mic IO is demand-driven via kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost.

// Self Include
#import "NovaLINKMicInputMixer.h"

// Local Includes
#import "NovaLINKAudioDevice.h"
#import "NovaLINKDevice.h"
#import "NovaLINKMicrophoneAccess.h"
#import "NovaLINK_Types.h"
#import "NovaLINK_Utils.h"

// PublicUtility Includes
#import "CADebugMacros.h"
#import "CAException.h"
#import "CAHALAudioSystemObject.h"

// System Includes
#import <AudioToolbox/AudioToolbox.h>
#import <algorithm>
#import <cmath>
#import <vector>


#pragma clang assume_nonnull begin

namespace {

struct ConverterInputContext {
    const AudioBufferList* srcABL;
    UInt32 framesRemaining;
    UInt32 bytesPerFrame;
    bool provided;
};

OSStatus ConverterInputProc(AudioConverterRef,
                            UInt32* ioNumberDataPackets,
                            AudioBufferList* ioData,
                            AudioStreamPacketDescription* __nullable* __nullable,
                            void* inUserData)
{
    auto* ctx = static_cast<ConverterInputContext*>(inUserData);
    if (ctx->provided || ctx->framesRemaining == 0) {
        *ioNumberDataPackets = 0;
        return noErr;
    }

    const UInt32 framesToGive = std::min(*ioNumberDataPackets, ctx->framesRemaining);
    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0] = ctx->srcABL->mBuffers[0];
    ioData->mBuffers[0].mDataByteSize = framesToGive * ctx->bytesPerFrame;
    *ioNumberDataPackets = framesToGive;
    ctx->framesRemaining -= framesToGive;
    ctx->provided = true;
    return noErr;
}

AudioStreamBasicDescription StereoFloatFormat(Float64 sampleRate)
{
    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags =
            kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = 8;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 8;
    asbd.mChannelsPerFrame = 2;
    asbd.mBitsPerChannel = 32;
    return asbd;
}

// Opening a Bluetooth headset as an *input* forces macOS into HFP/HSP (mono ~8/16 kHz) for that
// device's output as well. Playthrough then sounds like a phone call / "monster" voice. Never use
// BT/AirPlay devices as the hardware mic for inject — prefer built-in / USB instead.
BOOL IsUnsafeMicTransport(const NovaLINKAudioDevice& device)
{
    try {
        switch (device.GetTransportType()) {
            case kAudioDeviceTransportTypeBluetooth:
            case kAudioDeviceTransportTypeBluetoothLE:
            case kAudioDeviceTransportTypeAirPlay:
                return YES;
            default:
                return NO;
        }
    } catch (...) {
        return NO;
    }
}

BOOL IsBuiltInTransport(const NovaLINKAudioDevice& device)
{
    try {
        return device.GetTransportType() == kAudioDeviceTransportTypeBuiltIn;
    } catch (...) {
        return NO;
    }
}

}  // namespace

@implementation NovaLINKMicInputMixer {
    NSObject* _lock;
    BOOL _running;
    AudioObjectID _micDeviceID;
    AudioObjectID _novaLINKDeviceID;
    AudioObjectID _monitoredNovaLINKDeviceID;
    AudioDeviceIOProcID _ioProcID;
    AudioConverterRef _converter;
    AudioStreamBasicDescription _micFormat;
    AudioStreamBasicDescription _injectFormat;
    dispatch_queue_t _injectQueue;
    dispatch_queue_t _demandQueue;
    AudioObjectPropertyListenerBlock _demandListener;
    Float64 _novaLINKSampleRate;
}

+ (instancetype) sharedInstance {
    static NovaLINKMicInputMixer* instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[NovaLINKMicInputMixer alloc] init];
    });
    return instance;
}

- (instancetype) init {
    if ((self = [super init])) {
        _lock = [NSObject new];
        _running = NO;
        _micDeviceID = kAudioObjectUnknown;
        _novaLINKDeviceID = kAudioObjectUnknown;
        _monitoredNovaLINKDeviceID = kAudioObjectUnknown;
        _ioProcID = nullptr;
        _converter = nullptr;
        _novaLINKSampleRate = 44100.0;
        _injectQueue = dispatch_queue_create("life.thenurim.novalink.MicInject",
                                             DISPATCH_QUEUE_SERIAL);
        _demandQueue = dispatch_queue_create("life.thenurim.novalink.MicDemand",
                                             DISPATCH_QUEUE_SERIAL);
        _demandListener = nil;
        memset(&_micFormat, 0, sizeof(_micFormat));
        memset(&_injectFormat, 0, sizeof(_injectFormat));
    }
    return self;
}

- (void) dealloc {
    [self stop];
}

+ (AudioObjectID) resolveHardwareInputDeviceID {
    AudioObjectID inputDevice = kAudioObjectUnknown;
    AudioObjectID builtInFallback = kAudioObjectUnknown;
    AudioObjectID anySafeFallback = kAudioObjectUnknown;
    CAHALAudioSystemObject audioSystem;

    auto considerDevice = [&](NovaLINKAudioDevice device) {
        if (device.GetObjectID() == kAudioObjectUnknown ||
            device.IsNovaLINKDeviceInstance() ||
            device.GetNumberStreams(true) == 0 ||
            IsUnsafeMicTransport(device)) {
            return;
        }
        if (IsBuiltInTransport(device) && builtInFallback == kAudioObjectUnknown) {
            builtInFallback = device.GetObjectID();
        }
        if (anySafeFallback == kAudioObjectUnknown) {
            anySafeFallback = device.GetObjectID();
        }
    };

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKAudioDevice defaultInput = audioSystem.GetDefaultAudioDevice(true, false);
        if (defaultInput.GetObjectID() != kAudioObjectUnknown &&
            !defaultInput.IsNovaLINKDeviceInstance() &&
            defaultInput.GetNumberStreams(true) > 0 &&
            !IsUnsafeMicTransport(defaultInput)) {
            inputDevice = defaultInput.GetObjectID();
        } else if (defaultInput.GetObjectID() != kAudioObjectUnknown &&
                   IsUnsafeMicTransport(defaultInput)) {
            LogWarning("NovaLINKMicInputMixer: Default input is Bluetooth/AirPlay — skipping to "
                       "avoid forcing HFP (mono / low-rate) on the headset output");
        }
    });

    if (inputDevice != kAudioObjectUnknown) {
        return inputDevice;
    }

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        UInt32 numDevices = audioSystem.GetNumberAudioDevices();
        std::vector<AudioObjectID> devices(numDevices);
        audioSystem.GetAudioDevices(numDevices, devices.data());

        for (UInt32 i = 0; i < numDevices; i++) {
            considerDevice(NovaLINKAudioDevice(devices[i]));
        }
    });

    if (builtInFallback != kAudioObjectUnknown) {
        return builtInFallback;
    }
    return anySafeFallback;
}

- (BOOL) prepareDevicesLocked {
    AudioObjectID micID = [NovaLINKMicInputMixer resolveHardwareInputDeviceID];
    if (micID == kAudioObjectUnknown) {
        LogError("NovaLINKMicInputMixer: No hardware input device found");
        return NO;
    }

    NovaLINKDevice novaLINKDevice;
    _novaLINKDeviceID = novaLINKDevice.GetObjectID();
    if (_novaLINKDeviceID == kAudioObjectUnknown) {
        LogError("NovaLINKMicInputMixer: NovaLINKDevice not found");
        return NO;
    }

    NovaLINKAudioDevice micDevice(micID);
    NovaLINKAudioDevice novaDevice(_novaLINKDeviceID);

    _novaLINKSampleRate = novaDevice.GetNominalSampleRate();
    _injectFormat = StereoFloatFormat(_novaLINKSampleRate);

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        if (micDevice.IsValidNominalSampleRate(_novaLINKSampleRate) &&
            micDevice.GetNominalSampleRate() != _novaLINKSampleRate) {
            micDevice.SetNominalSampleRate(_novaLINKSampleRate);
        }
    });

    AudioStreamBasicDescription micFormats[1] = {};
    UInt32 numFormats = 0;
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        UInt32 n = 1;
        micDevice.GetCurrentVirtualFormats(true, n, micFormats);
        numFormats = n;
    });

    if (numFormats == 0 || micFormats[0].mChannelsPerFrame == 0 || micFormats[0].mBytesPerFrame == 0) {
        LogError("NovaLINKMicInputMixer: Could not read mic stream format");
        return NO;
    }

    _micFormat = micFormats[0];

    if (_converter) {
        AudioConverterDispose(_converter);
        _converter = nullptr;
    }

    OSStatus err = AudioConverterNew(&_micFormat, &_injectFormat, &_converter);
    if (err != noErr || !_converter) {
        LogError("NovaLINKMicInputMixer: AudioConverterNew failed (%d)", (int)err);
        _converter = nullptr;
        return NO;
    }

    _micDeviceID = micID;
    return YES;
}

static OSStatus MicInputIOProc(AudioObjectID,
                               const AudioTimeStamp*,
                               const AudioBufferList* inInputData,
                               const AudioTimeStamp*,
                               AudioBufferList*,
                               const AudioTimeStamp*,
                               void* inClientData)
{
    NovaLINKMicInputMixer* mixer = (__bridge NovaLINKMicInputMixer*)inClientData;
    [mixer processInputBuffer:inInputData];
    return noErr;
}

- (void) processInputBuffer:(const AudioBufferList* __nullable)inInputData {
    if (!inInputData || inInputData->mNumberBuffers == 0 || !inInputData->mBuffers[0].mData) {
        return;
    }

    const AudioBuffer& srcBuf = inInputData->mBuffers[0];
    if (srcBuf.mDataByteSize == 0) {
        return;
    }

    // Copy off the realtime thread; convert + inject on a serial queue.
    NSData* rawCopy = [NSData dataWithBytes:srcBuf.mData length:srcBuf.mDataByteSize];
    const UInt32 srcChannels = srcBuf.mNumberChannels;

    dispatch_async(_injectQueue, ^{
        [self convertAndInjectRawAudio:rawCopy channelCount:srcChannels];
    });
}

- (void) convertAndInjectRawAudio:(NSData*)rawCopy channelCount:(UInt32)srcChannels {
    AudioConverterRef converter = nullptr;
    AudioObjectID novaID = kAudioObjectUnknown;
    AudioStreamBasicDescription micFormat = {};
    AudioStreamBasicDescription injectFormat = {};
    Float64 novaRate = 44100.0;

    @synchronized (_lock) {
        if (!_running || !_converter || rawCopy.length == 0) {
            return;
        }
        converter = _converter;
        novaID = _novaLINKDeviceID;
        micFormat = _micFormat;
        injectFormat = _injectFormat;
        novaRate = _novaLINKSampleRate;
    }

    if (micFormat.mBytesPerFrame == 0) {
        return;
    }

    const UInt32 srcFrames = (UInt32)(rawCopy.length / micFormat.mBytesPerFrame);
    if (srcFrames == 0) {
        return;
    }

    AudioBufferList srcAbl = {};
    srcAbl.mNumberBuffers = 1;
    srcAbl.mBuffers[0].mNumberChannels = srcChannels ? srcChannels : micFormat.mChannelsPerFrame;
    srcAbl.mBuffers[0].mDataByteSize = (UInt32)rawCopy.length;
    srcAbl.mBuffers[0].mData = (void*)rawCopy.bytes;

    ConverterInputContext ctx = { &srcAbl, srcFrames, micFormat.mBytesPerFrame, false };

    const Float64 ratio = novaRate / std::max(micFormat.mSampleRate, 1.0);
    UInt32 dstFramesCapacity = static_cast<UInt32>(std::ceil(ratio * srcFrames)) + 32;

    std::vector<Float32> dstSamples(dstFramesCapacity * injectFormat.mChannelsPerFrame, 0.0f);
    AudioBufferList dstAbl = {};
    dstAbl.mNumberBuffers = 1;
    dstAbl.mBuffers[0].mNumberChannels = injectFormat.mChannelsPerFrame;
    dstAbl.mBuffers[0].mDataByteSize =
            static_cast<UInt32>(dstSamples.size() * sizeof(Float32));
    dstAbl.mBuffers[0].mData = dstSamples.data();

    UInt32 outPackets = dstFramesCapacity;
    const OSStatus err = AudioConverterFillComplexBuffer(converter,
                                                         ConverterInputProc,
                                                         &ctx,
                                                         &outPackets,
                                                         &dstAbl,
                                                         nullptr);
    if (err != noErr || outPackets == 0) {
        return;
    }

    const CFIndex byteCount = static_cast<CFIndex>(outPackets * injectFormat.mBytesPerFrame);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8*>(dstSamples.data()),
                                  byteCount);
    if (!data) {
        return;
    }

    UInt32 size = sizeof(CFDataRef);
    AudioObjectSetPropertyData(novaID,
                               &kNovaLINKInjectMicAudioAddress,
                               0,
                               nullptr,
                               size,
                               &data);
    CFRelease(data);
}

- (BOOL) startLocked {
    if (![self prepareDevicesLocked]) {
        return NO;
    }

    if (![NovaLINKMicrophoneAccess isAuthorized]) {
        LogWarning("NovaLINKMicInputMixer: Microphone not authorized — skipping input IO start "
                   "(avoids stacked TCC dialogs)");
        return NO;
    }

    try {
        NovaLINKAudioDevice micDevice(_micDeviceID);
        _ioProcID = micDevice.CreateIOProcID(MicInputIOProc, (__bridge void*)self);
        micDevice.StartIOProc(_ioProcID);
        _running = YES;
        DebugMsg("NovaLINKMicInputMixer: Started (mic %u → NovaLINK %u @ %.0f Hz)",
                 _micDeviceID,
                 _novaLINKDeviceID,
                 _novaLINKSampleRate);
        return YES;
    } catch (const CAException& e) {
        LogError("NovaLINKMicInputMixer: Failed to start mic IO (%d)", e.GetError());
        [self stopIOLocked];
        return NO;
    }
}

- (void) ensureStarted {
    // Resolve desired config without tearing down an already-good session.
    AudioObjectID desiredMic = [NovaLINKMicInputMixer resolveHardwareInputDeviceID];
    Float64 desiredRate = 44100.0;
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKDevice novaLINKDevice;
        if (novaLINKDevice.GetObjectID() != kAudioObjectUnknown) {
            desiredRate = NovaLINKAudioDevice(novaLINKDevice.GetObjectID()).GetNominalSampleRate();
        }
    });

    @synchronized (_lock) {
        if (_running &&
            _micDeviceID == desiredMic &&
            desiredMic != kAudioObjectUnknown &&
            std::fabs(_novaLINKSampleRate - desiredRate) < 0.5) {
            return;
        }
    }

    if (![NovaLINKMicrophoneAccess isAuthorized]) {
        [NovaLINKMicrophoneAccess requestAccessIfNeededWithCompletion:^(BOOL granted) {
            if (granted) {
                [[NovaLINKMicInputMixer sharedInstance] syncToCaptureDemand];
            }
        }];
        return;
    }

    @synchronized (_lock) {
        if (_running &&
            _micDeviceID == desiredMic &&
            desiredMic != kAudioObjectUnknown &&
            std::fabs(_novaLINKSampleRate - desiredRate) < 0.5) {
            return;
        }

        [self stopIOLocked];
        [self startLocked];
    }
}

- (BOOL) isCaptureDemanded {
    BOOL demanded = NO;
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKDevice novaLINKDevice;
        AudioObjectID novaID = novaLINKDevice.GetObjectID();
        if (novaID == kAudioObjectUnknown) {
            return;
        }
        NovaLINKAudioDevice device(novaID);
        if (!device.HasProperty(kNovaLINKInputRunningSomewhereOtherThanPassthroughHostAddress)) {
            return;
        }
        CFTypeRef value = device.GetPropertyData_CFType(
                kNovaLINKInputRunningSomewhereOtherThanPassthroughHostAddress);
        if (value != nullptr) {
            demanded = CFBooleanGetValue(static_cast<CFBooleanRef>(value));
        }
    });
    return demanded;
}

- (void) installDemandListenerOnDevice:(AudioObjectID)novaID {
    if (novaID == kAudioObjectUnknown) {
        return;
    }

    if (_monitoredNovaLINKDeviceID == novaID && _demandListener != nil) {
        return;
    }

    [self removeDemandListener];

    __weak NovaLINKMicInputMixer* weakSelf = self;
    _demandListener = ^(UInt32, const AudioObjectPropertyAddress*) {
        NovaLINKMicInputMixer* strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        // Return immediately — StartIOProc from inside a HAL listener wedges coreaudiod.
        dispatch_async(strongSelf->_demandQueue, ^{
            [strongSelf syncToCaptureDemand];
        });
    };

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKAudioDevice device(novaID);
        device.AddPropertyListenerBlock(
                kNovaLINKInputRunningSomewhereOtherThanPassthroughHostAddress,
                _demandQueue,
                _demandListener);
        _monitoredNovaLINKDeviceID = novaID;
        DebugMsg("NovaLINKMicInputMixer: Demand monitoring on NovaLINK device %u", novaID);
    });
}

- (void) removeDemandListener {
    if (_demandListener == nil || _monitoredNovaLINKDeviceID == kAudioObjectUnknown) {
        _demandListener = nil;
        _monitoredNovaLINKDeviceID = kAudioObjectUnknown;
        return;
    }

    AudioObjectPropertyListenerBlock listener = _demandListener;
    AudioObjectID deviceID = _monitoredNovaLINKDeviceID;
    _demandListener = nil;
    _monitoredNovaLINKDeviceID = kAudioObjectUnknown;

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKAudioDevice device(deviceID);
        device.RemovePropertyListenerBlock(
                kNovaLINKInputRunningSomewhereOtherThanPassthroughHostAddress,
                _demandQueue,
                listener);
    });
}

- (void) startDemandMonitoring {
    if ([NSThread isMainThread]) {
        dispatch_async(_demandQueue, ^{
            [self startDemandMonitoring];
        });
        return;
    }

    NovaLINKDevice novaLINKDevice;
    AudioObjectID novaID = novaLINKDevice.GetObjectID();
    [self installDemandListenerOnDevice:novaID];
    [self syncToCaptureDemand];
}

- (void) syncToCaptureDemand {
    if ([NSThread isMainThread]) {
        dispatch_async(_demandQueue, ^{
            [self syncToCaptureDemand];
        });
        return;
    }

    if ([self isCaptureDemanded]) {
        DebugMsg("NovaLINKMicInputMixer: Capture client reading NovaLINK input — ensuring mic inject");
        [self ensureStarted];
    } else {
        DebugMsg("NovaLINKMicInputMixer: No capture client — stopping mic inject");
        @synchronized (_lock) {
            [self stopIOLocked];
        }
    }
}

- (void) stopIOLocked {
    _running = NO;

    // Drain pending convert/inject work before tearing down the converter.
    if (_injectQueue) {
        dispatch_sync(_injectQueue, ^{});
    }

    if (_ioProcID != nullptr && _micDeviceID != kAudioObjectUnknown) {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            NovaLINKAudioDevice micDevice(_micDeviceID);
            micDevice.StopIOProc(_ioProcID);
            micDevice.DestroyIOProcID(_ioProcID);
        });
        _ioProcID = nullptr;
    }

    if (_converter) {
        AudioConverterDispose(_converter);
        _converter = nullptr;
    }

    _micDeviceID = kAudioObjectUnknown;
}

- (void) stop {
    // Remove the listener outside `_lock` — RemovePropertyListenerBlock waits for in-flight
    // callbacks, and those callbacks take `_lock` in syncToCaptureDemand.
    [self removeDemandListener];
    @synchronized (_lock) {
        [self stopIOLocked];
    }
}

@end

#pragma clang assume_nonnull end
