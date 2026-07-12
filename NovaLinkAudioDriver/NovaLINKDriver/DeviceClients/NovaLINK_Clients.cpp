// This file is part of NovaLINK.
//
// NovaLINK is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.
//
// NovaLINK is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with NovaLINK. If not, see <http://www.gnu.org/licenses/>.

//
//  NovaLINK_Clients.cpp
//  NovaLINKDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//

// Self Include
#include "NovaLINK_Clients.h"

// Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_PlugIn.h"

// PublicUtility Includes
#include "CAException.h"
#include "CACFDictionary.h"
#include "CADispatchQueue.h"


#pragma mark Construction/Destruction

NovaLINK_Clients::NovaLINK_Clients(AudioObjectID inOwnerDeviceID, NovaLINK_TaskQueue* inTaskQueue)
:
    mOwnerDeviceID(inOwnerDeviceID),
    mClientMap(inTaskQueue)
{
    mRelativeVolumeCurve.AddRange(kAppRelativeVolumeMinRawValue,
                                  kAppRelativeVolumeMaxRawValue,
                                  kAppRelativeVolumeMinDbValue,
                                  kAppRelativeVolumeMaxDbValue);
}

#pragma mark Add/Remove Clients

void    NovaLINK_Clients::AddClient(NovaLINK_Client inClient)
{
    CAMutex::Locker theLocker(mMutex);

    // Check whether this is the music player's client
    bool pidMatchesMusicPlayerProperty =
        (mMusicPlayerProcessIDProperty != 0 && inClient.mProcessID == mMusicPlayerProcessIDProperty);
    bool bundleIDMatchesMusicPlayerProperty =
        (mMusicPlayerBundleIDProperty != "" &&
         inClient.mBundleID.IsValid() &&
         inClient.mBundleID == mMusicPlayerBundleIDProperty);
    
    inClient.mIsMusicPlayer = (pidMatchesMusicPlayerProperty || bundleIDMatchesMusicPlayerProperty);
    
    if(inClient.mIsMusicPlayer)
    {
        DebugMsg("NovaLINK_Clients::AddClient: Adding music player client. mClientID = %u", inClient.mClientID);
    }
    
    mClientMap.AddClient(inClient);
    
    // If we're adding NovaLINKApp, update our local copy of its client ID
    if(inClient.mBundleID.IsValid() && inClient.mBundleID == kNovaLINKAppBundleID)
    {
        mNovaLINKAppClientID = inClient.mClientID;
    }
}

void    NovaLINK_Clients::RemoveClient(const UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    NovaLINK_Client theRemovedClient = mClientMap.RemoveClient(inClientID);
    
    // If we're removing NovaLINKApp, clear our local copy of its client ID
    if(theRemovedClient.mClientID == mNovaLINKAppClientID)
    {
        mNovaLINKAppClientID = -1;
    }
}

#pragma mark IO Status

bool    NovaLINK_Clients::StartIONonRT(UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    bool didStartIO = false;
    
    NovaLINK_Client theClient;
    bool didFindClient = mClientMap.GetClientNonRT(inClientID, &theClient);
    
    ThrowIf(!didFindClient, NovaLINK_InvalidClientException(), "NovaLINK_Clients::StartIO: Cannot start IO for client that was never added");
    
    bool sendIsRunningNotification = false;
    bool sendIsRunningSomewhereOtherThanNovaLINKAppNotification = false;

    if(!theClient.mDoingIO)
    {
        // Make sure we can start
        ThrowIf(mStartCount == UINT64_MAX, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Clients::StartIO: failed to start because the ref count was maxxed out already");
        
        DebugMsg("NovaLINK_Clients::StartIO: Client %u (%s, %d) starting IO",
                 inClientID,
                 CFStringGetCStringPtr(theClient.mBundleID.GetCFString(), kCFStringEncodingUTF8),
                 theClient.mProcessID);
        
        mClientMap.StartIONonRT(inClientID);
        
        mStartCount++;
        
        // Update mStartCountExcludingNovaLINKApp
        if(!IsNovaLINKApp(inClientID))
        {
            ThrowIf(mStartCountExcludingNovaLINKApp == UINT64_MAX, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Clients::StartIO: failed to start because mStartCountExcludingNovaLINKApp was maxxed out already");
            
            mStartCountExcludingNovaLINKApp++;
            
            if(mStartCountExcludingNovaLINKApp == 1)
            {
                sendIsRunningSomewhereOtherThanNovaLINKAppNotification = true;
            }
        }
        
        // Return true if no other clients were running IO before this one started, which means the device should start IO
        didStartIO = (mStartCount == 1);
        sendIsRunningNotification = didStartIO;
    }
    
    Assert(mStartCountExcludingNovaLINKApp == mStartCount - 1 || mStartCountExcludingNovaLINKApp == mStartCount,
           "mStartCount and mStartCountExcludingNovaLINKApp are out of sync");
    
    SendIORunningNotifications(sendIsRunningNotification, sendIsRunningSomewhereOtherThanNovaLINKAppNotification);

    return didStartIO;
}

bool    NovaLINK_Clients::StopIONonRT(UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    bool didStopIO = false;
    
    NovaLINK_Client theClient;
    bool didFindClient = mClientMap.GetClientNonRT(inClientID, &theClient);
    
    ThrowIf(!didFindClient, NovaLINK_InvalidClientException(), "NovaLINK_Clients::StopIO: Cannot stop IO for client that was never added");
    
    bool sendIsRunningNotification = false;
    bool sendIsRunningSomewhereOtherThanNovaLINKAppNotification = false;
    
    if(theClient.mDoingIO)
    {
        DebugMsg("NovaLINK_Clients::StopIO: Client %u (%s, %d) stopping IO",
                 inClientID,
                 CFStringGetCStringPtr(theClient.mBundleID.GetCFString(), kCFStringEncodingUTF8),
                 theClient.mProcessID);
        
        mClientMap.StopIONonRT(inClientID);
        
        ThrowIf(mStartCount <= 0, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Clients::StopIO: Underflowed mStartCount");
        
        mStartCount--;
        
        // Update mStartCountExcludingNovaLINKApp
        if(!IsNovaLINKApp(inClientID))
        {
            ThrowIf(mStartCountExcludingNovaLINKApp <= 0, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Clients::StopIO: Underflowed mStartCountExcludingNovaLINKApp");
            
            mStartCountExcludingNovaLINKApp--;
            
            if(mStartCountExcludingNovaLINKApp == 0)
            {
                sendIsRunningSomewhereOtherThanNovaLINKAppNotification = true;
            }
        }
        
        // Return true if we stopped IO entirely (i.e. there are no clients still running IO)
        didStopIO = (mStartCount == 0);
        sendIsRunningNotification = didStopIO;
    }
    
    Assert(mStartCountExcludingNovaLINKApp == mStartCount - 1 || mStartCountExcludingNovaLINKApp == mStartCount,
           "mStartCount and mStartCountExcludingNovaLINKApp are out of sync");
    
    SendIORunningNotifications(sendIsRunningNotification, sendIsRunningSomewhereOtherThanNovaLINKAppNotification);
    
    return didStopIO;
}

bool    NovaLINK_Clients::ClientsRunningIO() const
{
    return mStartCount > 0;
}

bool    NovaLINK_Clients::ClientsOtherThanNovaLINKAppRunningIO() const
{
    return mStartCountExcludingNovaLINKApp > 0;
}

void    NovaLINK_Clients::SendIORunningNotifications(bool sendIsRunningNotification, bool sendIsRunningSomewhereOtherThanNovaLINKAppNotification) const
{
    if(sendIsRunningNotification || sendIsRunningSomewhereOtherThanNovaLINKAppNotification)
    {
        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, ^{
            AudioObjectPropertyAddress theChangedProperties[2];
            UInt32 theNotificationCount = 0;

            if(sendIsRunningNotification)
            {
                DebugMsg("NovaLINK_Clients::SendIORunningNotifications: Sending kAudioDevicePropertyDeviceIsRunning");
                theChangedProperties[0] = { kAudioDevicePropertyDeviceIsRunning, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
                theNotificationCount++;
            }

            if(sendIsRunningSomewhereOtherThanNovaLINKAppNotification)
            {
                DebugMsg("NovaLINK_Clients::SendIORunningNotifications: Sending kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp");
                theChangedProperties[theNotificationCount] = kNovaLINKRunningSomewhereOtherThanNovaLINKAppAddress;
                theNotificationCount++;
            }

            NovaLINK_PlugIn::Host_PropertiesChanged(mOwnerDeviceID, theNotificationCount, theChangedProperties);
        });
    }
}

#pragma mark Music Player

bool    NovaLINK_Clients::SetMusicPlayer(const pid_t inPID)
{
    ThrowIf(inPID < 0, NovaLINK_InvalidClientPIDException(), "NovaLINK_Clients::SetMusicPlayer: Invalid music player PID");
    
    CAMutex::Locker theLocker(mMutex);
    
    if(mMusicPlayerProcessIDProperty == inPID)
    {
        // We're not changing the properties, so return false
        return false;
    }
    
    mMusicPlayerProcessIDProperty = inPID;
    // Unset the bundle ID property
    mMusicPlayerBundleIDProperty = "";
    
    DebugMsg("NovaLINK_Clients::SetMusicPlayer: Setting music player by PID. inPID=%d", inPID);
    
    // Update the clients' mIsMusicPlayer fields
    mClientMap.UpdateMusicPlayerFlags(inPID);
    
    return true;
}

bool    NovaLINK_Clients::SetMusicPlayer(const CACFString inBundleID)
{
    Assert(inBundleID.IsValid(), "NovaLINK_Clients::SetMusicPlayer: Invalid CACFString given as bundle ID");
    
    CAMutex::Locker theLocker(mMutex);
    
    if(mMusicPlayerBundleIDProperty == inBundleID)
    {
        // We're not changing the properties, so return false
        return false;
    }
    
    mMusicPlayerBundleIDProperty = inBundleID;
    // Unset the PID property
    mMusicPlayerProcessIDProperty = 0;
    
    DebugMsg("NovaLINK_Clients::SetMusicPlayer: Setting music player by bundle ID. inBundleID=%s",
             CFStringGetCStringPtr(inBundleID.GetCFString(), kCFStringEncodingUTF8));
    
    // Update the clients' mIsMusicPlayer fields
    mClientMap.UpdateMusicPlayerFlags(inBundleID);
    
    return true;
}

bool    NovaLINK_Clients::IsMusicPlayerRT(const UInt32 inClientID) const
{
    NovaLINK_Client theClient;
    bool didGetClient = mClientMap.GetClientRT(inClientID, &theClient);
    return didGetClient && theClient.mIsMusicPlayer;
}

#pragma mark App Volumes

Float32 NovaLINK_Clients::GetClientRelativeVolumeRT(UInt32 inClientID) const
{
    NovaLINK_Client theClient;
    bool didGetClient = mClientMap.GetClientRT(inClientID, &theClient);
    return (didGetClient ? theClient.mRelativeVolume : 1.0f);
}

SInt32 NovaLINK_Clients::GetClientPanPositionRT(UInt32 inClientID) const
{
    NovaLINK_Client theClient;
    bool didGetClient = mClientMap.GetClientRT(inClientID, &theClient);
    return (didGetClient ? theClient.mPanPosition : kAppPanCenterRawValue);
}

bool    NovaLINK_Clients::SetClientsRelativeVolumes(const CACFArray inAppVolumes)
{
    bool didChangeAppVolumes = false;
    
    // Each element in appVolumes is a CFDictionary containing the process id and/or bundle id of an app, and its
    // new relative volume
    for(UInt32 i = 0; i < inAppVolumes.GetNumberItems(); i++)
    {
        CACFDictionary theAppVolume(false);
        inAppVolumes.GetCACFDictionary(i, theAppVolume);
        
        // Get the app's PID from the dict
        pid_t theAppPID;
        bool didFindPID = theAppVolume.GetSInt32(CFSTR(kNovaLINKAppVolumesKey_ProcessID), theAppPID);
        
        // Get the app's bundle ID from the dict
        CACFString theAppBundleID;
        theAppBundleID.DontAllowRelease();
        theAppVolume.GetCACFString(CFSTR(kNovaLINKAppVolumesKey_BundleID), theAppBundleID);
        
        ThrowIf(!didFindPID && !theAppBundleID.IsValid(),
                NovaLINK_InvalidClientRelativeVolumeException(),
                "NovaLINK_Clients::SetClientsRelativeVolumes: App volume was sent without PID or bundle ID for app");
        
        bool didGetVolume;
        {
            SInt32 theRawRelativeVolume;
            didGetVolume = theAppVolume.GetSInt32(CFSTR(kNovaLINKAppVolumesKey_RelativeVolume), theRawRelativeVolume);
            
            if (didGetVolume) {
                ThrowIf(didGetVolume && (theRawRelativeVolume < kAppRelativeVolumeMinRawValue || theRawRelativeVolume > kAppRelativeVolumeMaxRawValue),
                        NovaLINK_InvalidClientRelativeVolumeException(),
                        "NovaLINK_Clients::SetClientsRelativeVolumes: Relative volume for app out of valid range");
                
                // Apply the volume curve to the raw volume
                //
                // mRelativeVolumeCurve uses the default kPow2Over1Curve transfer function, so we also multiply by 4 to
                // keep the middle volume equal to 1 (meaning apps' volumes are unchanged by default).
                Float32 theRelativeVolume = mRelativeVolumeCurve.ConvertRawToScalar(theRawRelativeVolume) * 4;

                // Try to update the client's volume, first by PID and then by bundle ID. Always try
                // both because apps can have multiple clients.
                if(mClientMap.SetClientsRelativeVolume(theAppPID, theRelativeVolume))
                {
                    didChangeAppVolumes = true;
                }

                if(mClientMap.SetClientsRelativeVolume(theAppBundleID, theRelativeVolume))
                {
                    didChangeAppVolumes = true;
                }

                // TODO: If the app isn't currently a client, we should add it to the past clients
                //       map, or update its past volume if it's already in there.
            }
        }
        
        bool didGetPanPosition;
        {
            SInt32 thePanPosition;
            didGetPanPosition = theAppVolume.GetSInt32(CFSTR(kNovaLINKAppVolumesKey_PanPosition), thePanPosition);
            if (didGetPanPosition) {
                ThrowIf(didGetPanPosition && (thePanPosition < kAppPanLeftRawValue || thePanPosition > kAppPanRightRawValue),
                                              NovaLINK_InvalidClientPanPositionException(),
                                              "NovaLINK_Clients::SetClientsRelativeVolumes: Pan position for app out of valid range");
                
                if(mClientMap.SetClientsPanPosition(theAppPID, thePanPosition))
                {
                    didChangeAppVolumes = true;
                }

                if(mClientMap.SetClientsPanPosition(theAppBundleID, thePanPosition))
                {
                    didChangeAppVolumes = true;
                }

                // TODO: If the app isn't currently a client, we should add it to the past clients
                //       map, or update its past pan position if it's already in there.
            }
        }
        
        ThrowIf(!didGetVolume && !didGetPanPosition,
                NovaLINK_InvalidClientRelativeVolumeException(),
                "NovaLINK_Clients::SetClientsRelativeVolumes: No volume or pan position in request");
    }
    
    return didChangeAppVolumes;
}

