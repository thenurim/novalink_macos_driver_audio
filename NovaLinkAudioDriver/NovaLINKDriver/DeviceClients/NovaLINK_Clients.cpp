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
#include "CADispatchQueue.h"


#pragma mark Construction/Destruction

NovaLINK_Clients::NovaLINK_Clients(AudioObjectID inOwnerDeviceID, NovaLINK_TaskQueue* inTaskQueue)
:
    mOwnerDeviceID(inOwnerDeviceID),
    mClientMap(inTaskQueue)
{
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

    // The HAL registers multiple clients per process. Every App/Helper client must be a
    // passthrough host so playthrough ReadInput is not treated as Zoom-style capture.
    inClient.mIsPassthroughHost =
            inClient.mBundleID.IsValid() &&
            (inClient.mBundleID == kNovaLINKAppBundleID ||
             inClient.mBundleID == kNovaLINKXPCHelperBundleID);
    
    mClientMap.AddClient(inClient);
    
    // If we're adding NovaLINKApp or XPCHelper, update our local copy of their client ID.
    // XPCHelper hosts fallback playthrough when the companion app is not running.
    if(inClient.mBundleID.IsValid() && inClient.mBundleID == kNovaLINKAppBundleID)
    {
        mNovaLINKAppClientID = inClient.mClientID;
        mNovaLINKAppClientCount++;
    }
    else if(inClient.mBundleID.IsValid() && inClient.mBundleID == kNovaLINKXPCHelperBundleID)
    {
        mXPCHelperClientID = inClient.mClientID;
        mXPCHelperClientCount++;
    }
}

void    NovaLINK_Clients::RemoveClient(const UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);
    
    NovaLINK_Client theRemovedClient = mClientMap.RemoveClient(inClientID);
    
    // If we're removing NovaLINKApp, clear our local copy of its client ID
    if(theRemovedClient.mBundleID.IsValid() && theRemovedClient.mBundleID == kNovaLINKAppBundleID)
    {
        if(mNovaLINKAppClientCount > 0)
        {
            mNovaLINKAppClientCount--;
        }
        if(mNovaLINKAppClientCount == 0)
        {
            mNovaLINKAppClientID = -1;
        }
        else if(theRemovedClient.mClientID == mNovaLINKAppClientID)
        {
            mNovaLINKAppClientID = -1;
        }
    }
    if(theRemovedClient.mBundleID.IsValid() && theRemovedClient.mBundleID == kNovaLINKXPCHelperBundleID)
    {
        if(mXPCHelperClientCount > 0)
        {
            mXPCHelperClientCount--;
        }
        if(mXPCHelperClientCount == 0)
        {
            mXPCHelperClientID = -1;
        }
        else if(theRemovedClient.mClientID == mXPCHelperClientID)
        {
            mXPCHelperClientID = -1;
        }
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
        
        // Update mStartCountExcludingNovaLINKApp (also excludes XPCHelper fallback playthrough)
        if(!theClient.mIsPassthroughHost)
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
    
    Assert(mStartCountExcludingNovaLINKApp <= mStartCount,
           "mStartCount and mStartCountExcludingNovaLINKApp are out of sync");
    
    SendIORunningNotifications(sendIsRunningNotification,
                               sendIsRunningSomewhereOtherThanNovaLINKAppNotification,
                               false);

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
    bool sendInputRunningSomewhereOtherThanPassthroughHostNotification = false;
    
    if(theClient.mDoingIO)
    {
        DebugMsg("NovaLINK_Clients::StopIO: Client %u (%s, %d) stopping IO",
                 inClientID,
                 CFStringGetCStringPtr(theClient.mBundleID.GetCFString(), kCFStringEncodingUTF8),
                 theClient.mProcessID);

        const bool wasDoingInputIO = theClient.mDoingInputIO;
        
        mClientMap.StopIONonRT(inClientID);
        
        ThrowIf(mStartCount <= 0, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Clients::StopIO: Underflowed mStartCount");
        
        mStartCount--;
        
        // Update mStartCountExcludingNovaLINKApp (also excludes XPCHelper fallback playthrough)
        if(!theClient.mIsPassthroughHost)
        {
            ThrowIf(mStartCountExcludingNovaLINKApp <= 0, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Clients::StopIO: Underflowed mStartCountExcludingNovaLINKApp");
            
            mStartCountExcludingNovaLINKApp--;
            
            if(mStartCountExcludingNovaLINKApp == 0)
            {
                sendIsRunningSomewhereOtherThanNovaLINKAppNotification = true;
            }

            if(wasDoingInputIO)
            {
                ThrowIf(mInputStartCountExcludingPassthrough <= 0,
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Clients::StopIO: Underflowed mInputStartCountExcludingPassthrough");
                mInputStartCountExcludingPassthrough--;
                if(mInputStartCountExcludingPassthrough == 0)
                {
                    sendInputRunningSomewhereOtherThanPassthroughHostNotification = true;
                }
            }
        }
        
        // Return true if we stopped IO entirely (i.e. there are no clients still running IO)
        didStopIO = (mStartCount == 0);
        sendIsRunningNotification = didStopIO;
    }
    
    Assert(mStartCountExcludingNovaLINKApp <= mStartCount,
           "mStartCount and mStartCountExcludingNovaLINKApp are out of sync");
    Assert(mInputStartCountExcludingPassthrough <= mStartCountExcludingNovaLINKApp,
           "mInputStartCountExcludingPassthrough and mStartCountExcludingNovaLINKApp are out of sync");
    
    SendIORunningNotifications(sendIsRunningNotification,
                               sendIsRunningSomewhereOtherThanNovaLINKAppNotification,
                               sendInputRunningSomewhereOtherThanPassthroughHostNotification);
    
    return didStopIO;
}

void    NovaLINK_Clients::StartInputIONonRT(UInt32 inClientID)
{
    CAMutex::Locker theLocker(mMutex);

    NovaLINK_Client theClient;
    bool didFindClient = mClientMap.GetClientNonRT(inClientID, &theClient);
    ThrowIf(!didFindClient,
            NovaLINK_InvalidClientException(),
            "NovaLINK_Clients::StartInputIO: Cannot mark input IO for client that was never added");

    if(!theClient.mDoingIO || theClient.mDoingInputIO || theClient.mIsPassthroughHost)
    {
        return;
    }

    ThrowIf(mInputStartCountExcludingPassthrough == UINT64_MAX,
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINK_Clients::StartInputIO: mInputStartCountExcludingPassthrough maxxed out");

    DebugMsg("NovaLINK_Clients::StartInputIO: Client %u (%s, %d) reading input",
             inClientID,
             CFStringGetCStringPtr(theClient.mBundleID.GetCFString(), kCFStringEncodingUTF8),
             theClient.mProcessID);

    mClientMap.StartInputIONonRT(inClientID);
    mInputStartCountExcludingPassthrough++;

    const bool becameActive = (mInputStartCountExcludingPassthrough == 1);
    SendIORunningNotifications(false, false, becameActive);
}

bool    NovaLINK_Clients::ClientsRunningIO() const
{
    return mStartCount > 0;
}

bool    NovaLINK_Clients::ClientsOtherThanNovaLINKAppRunningIO() const
{
    return mStartCountExcludingNovaLINKApp > 0;
}

bool    NovaLINK_Clients::ClientsOtherThanPassthroughHostReadingInput() const
{
    return mInputStartCountExcludingPassthrough > 0;
}

bool    NovaLINK_Clients::ClientShouldMarkInputIORT(UInt32 inClientID) const
{
    NovaLINK_Client theClient;
    if(!mClientMap.GetClientRT(inClientID, &theClient))
    {
        return false;
    }

    if(theClient.mIsPassthroughHost)
    {
        return false;
    }

    return theClient.mDoingIO && !theClient.mDoingInputIO;
}

bool    NovaLINK_Clients::IsPassthroughHostRT(UInt32 inClientID) const
{
    NovaLINK_Client theClient;
    if(!mClientMap.GetClientRT(inClientID, &theClient))
    {
        return false;
    }
    return theClient.mIsPassthroughHost;
}

bool    NovaLINK_Clients::IsPassthroughHostNonRT(UInt32 inClientID) const
{
    NovaLINK_Client theClient;
    if(!mClientMap.GetClientNonRT(inClientID, &theClient))
    {
        return false;
    }
    return theClient.mIsPassthroughHost;
}

void    NovaLINK_Clients::SendIORunningNotifications(bool sendIsRunningNotification,
                                                     bool sendIsRunningSomewhereOtherThanNovaLINKAppNotification,
                                                     bool sendInputRunningSomewhereOtherThanPassthroughHostNotification) const
{
    if(sendIsRunningNotification ||
       sendIsRunningSomewhereOtherThanNovaLINKAppNotification ||
       sendInputRunningSomewhereOtherThanPassthroughHostNotification)
    {
        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, ^{
            AudioObjectPropertyAddress theChangedProperties[3];
            UInt32 theNotificationCount = 0;

            if(sendIsRunningNotification)
            {
                DebugMsg("NovaLINK_Clients::SendIORunningNotifications: Sending kAudioDevicePropertyDeviceIsRunning");
                theChangedProperties[theNotificationCount] = {
                    kAudioDevicePropertyDeviceIsRunning,
                    kAudioObjectPropertyScopeGlobal,
                    kAudioObjectPropertyElementMaster
                };
                theNotificationCount++;
            }

            if(sendIsRunningSomewhereOtherThanNovaLINKAppNotification)
            {
                DebugMsg("NovaLINK_Clients::SendIORunningNotifications: Sending kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp");
                theChangedProperties[theNotificationCount] = kNovaLINKRunningSomewhereOtherThanNovaLINKAppAddress;
                theNotificationCount++;
            }

            if(sendInputRunningSomewhereOtherThanPassthroughHostNotification)
            {
                DebugMsg("NovaLINK_Clients::SendIORunningNotifications: Sending kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost");
                theChangedProperties[theNotificationCount] =
                        kNovaLINKInputRunningSomewhereOtherThanPassthroughHostAddress;
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

