#pragma once

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <vector>
#include <mutex>
#include <memory>

class NovaLINK_AudioPassThroughEngine {
public:
    NovaLINK_AudioPassThroughEngine() {
        // 기본 출력 장치 가져오기
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDefaultSystemOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        
        UInt32 size = sizeof(AudioDeviceID);
        OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, 
                                                   &propertyAddress,
                                                   0, 
                                                   nullptr,
                                                   &size, 
                                                   &mOutputDeviceID);

        if(status == noErr) {
            // 출력 오디오 스트림 포맷 설정
            size = sizeof(AudioStreamBasicDescription);
            propertyAddress.mSelector = kAudioDevicePropertyStreamFormat;
            propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
            
            // 출력 포맷 초기화
            mOutputFormat.mSampleRate = 44100.0;
            mOutputFormat.mFormatID = kAudioFormatLinearPCM;
            mOutputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
            mOutputFormat.mBytesPerPacket = 8;
            mOutputFormat.mFramesPerPacket = 1;
            mOutputFormat.mBytesPerFrame = 8;
            mOutputFormat.mChannelsPerFrame = 2;
            mOutputFormat.mBitsPerChannel = 32;
            
            status = AudioObjectGetPropertyData(mOutputDeviceID, 
                                              &propertyAddress,
                                              0, 
                                              nullptr,
                                              &size, 
                                              &mOutputFormat);

            if(status == noErr) {
                // 출력 AudioUnit 생성
                AudioComponentDescription desc;
                desc.componentType = kAudioUnitType_Output;
                desc.componentSubType = kAudioUnitSubType_HALOutput;
                desc.componentManufacturer = kAudioUnitManufacturer_Apple;
                desc.componentFlags = 0;
                desc.componentFlagsMask = 0;

                AudioComponent component = AudioComponentFindNext(nullptr, &desc);
                if(component != nullptr) {
                    status = AudioComponentInstanceNew(component, &mOutputUnit);
                    if(status == noErr) {
                        // 출력 AudioUnit 구성
                        UInt32 enableIO = 1;
                        AudioUnitSetProperty(mOutputUnit,
                                           kAudioOutputUnitProperty_EnableIO,
                                           kAudioUnitScope_Output,
                                           0,
                                           &enableIO,
                                           sizeof(enableIO));

                        // 현재 장치 설정
                        AudioUnitSetProperty(mOutputUnit,
                                           kAudioOutputUnitProperty_CurrentDevice,
                                           kAudioUnitScope_Global,
                                           0,
                                           &mOutputDeviceID,
                                           sizeof(mOutputDeviceID));

                        // 콜백 설정
                        AURenderCallbackStruct callback;
                        callback.inputProc = RenderCallback;
                        callback.inputProcRefCon = this;
                        AudioUnitSetProperty(mOutputUnit,
                                           kAudioUnitProperty_SetRenderCallback,
                                           kAudioUnitScope_Input,
                                           0,
                                           &callback,
                                           sizeof(callback));

                        // 초기화
                        AudioUnitInitialize(mOutputUnit);
                    }
                }
            }
        }
    }

    ~NovaLINK_AudioPassThroughEngine() {
        if(mOutputUnit) {
            AudioUnitUninitialize(mOutputUnit);
            AudioComponentInstanceDispose(mOutputUnit);
        }
    }

    void Start() {
        if(mOutputUnit) {
            AudioOutputUnitStart(mOutputUnit);
        }
    }

    void Stop() {
        if(mOutputUnit) {
            AudioOutputUnitStop(mOutputUnit);
        }
    }


    void ProcessAudioBuffer(const Float32* inBuffer, UInt32 inNumberFrames) {
        if(inBuffer && inNumberFrames > 0) {
            std::lock_guard<std::mutex> lock(mBufferMutex);
            const size_t byteCount = inNumberFrames * sizeof(Float32) * 2; // stereo
            mCurrentBuffer.resize(byteCount);
            memcpy(mCurrentBuffer.data(), inBuffer, byteCount);
        }
    }

private:
    static OSStatus RenderCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* /*ioActionFlags*/,
                                 const AudioTimeStamp* /*inTimeStamp*/,
                                 UInt32 /*inBusNumber*/,
                                 UInt32 /*inNumberFrames*/,
                                 AudioBufferList* ioData) {
        NovaLINK_AudioPassThroughEngine* engine = static_cast<NovaLINK_AudioPassThroughEngine*>(inRefCon);
        return engine->Render(ioData);
    }

    OSStatus Render(AudioBufferList* ioData) {
        std::lock_guard<std::mutex> lock(mBufferMutex);
        
        if(!mCurrentBuffer.empty() && ioData->mNumberBuffers > 0) {
            // 저장된 오디오 데이터를 출력 버퍼로 복사
            const UInt32 bytesToCopy = static_cast<UInt32>(
                std::min    void ProcessAudioBuffer(const Float32* inBuffer, UInt32 inNumberFrames) {
                    if(inBuffer && inNumberFrames > 0) {
                        std::lock_guard<std::mutex> lock(mBufferMutex);
                        const size_t byteCount = inNumberFrames * sizeof(Float32) * 2; // stereo
                        mCurrentBuffer.resize(byteCount);
                        memcpy(mCurrentBuffer.data(), inBuffer, byteCount);
                    }
                }mCurrentBuffer.size(),
                        static_cast<size_t>(ioData->mBuffers[0].mDataByteSize))
            );
            
            memcpy(ioData->mBuffers[0].mData, mCurrentBuffer.data(), bytesToCopy);
            ioData->mBuffers[0].mDataByteSize = bytesToCopy;
        }

        return noErr;
    }

    AudioDeviceID mOutputDeviceID = 0;
    AudioStreamBasicDescription mOutputFormat;
    AudioUnit mOutputUnit = nullptr;
    std::vector<uint8_t> mCurrentBuffer;
    std::mutex mBufferMutex;
};

