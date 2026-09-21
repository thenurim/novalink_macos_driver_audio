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
//  NovaLINK_Clients.h
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef __NovaLINKDriver__NovaLINK_Clients__
#define __NovaLINKDriver__NovaLINK_Clients__

// Local Includes
#include "NovaLINK_Client.h"
#include "NovaLINK_ClientMap.h"

// PublicUtility Includes
#include "CAMutex.h"

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


// Forward Declations
class NovaLINK_ClientTasks;


#pragma clang assume_nonnull begin

//==================================================================================================
//	NovaLINK_Clients
//
//  Holds information about the clients (of the host) of the NovaLINKDevice, i.e. the apps registered
//  with the HAL, generally so they can do IO at some point. NovaLINKApp and the music player are special
//  case clients.
//
//  Methods whose names end with "RT" should only be called from real-time threads.
//==================================================================================================

class NovaLINK_Clients
{
    
    friend class NovaLINK_ClientTasks;
    
public:
                                        NovaLINK_Clients(AudioObjectID inOwnerDeviceID, NovaLINK_TaskQueue* inTaskQueue);
                                        ~NovaLINK_Clients() = default;
    // Disallow copying. (It could make sense to implement these in future, but we don't need them currently.)
                                        NovaLINK_Clients(const NovaLINK_Clients&) = delete;
                                        NovaLINK_Clients& operator=(const NovaLINK_Clients&) = delete;
    
    void                                AddClient(NovaLINK_Client inClient);
    void                                RemoveClient(const UInt32 inClientID);
    
private:
    // Only NovaLINK_TaskQueue is allowed to call these (through the NovaLINK_ClientTasks interface). We get notifications
    // from the HAL when clients start/stop IO and they have to be processed in the order we receive them to
    // avoid race conditions. If these methods could be called directly those calls would skip any queued calls.
    bool                                StartIONonRT(UInt32 inClientID);
    bool                                StopIONonRT(UInt32 inClientID);
    // Marks that a client has begun reading input (ReadInput). Idempotent per IO session.
    void                                StartInputIONonRT(UInt32 inClientID);

public:
    bool                                ClientsRunningIO() const;
    bool                                ClientsOtherThanNovaLINKAppRunningIO() const;
    bool                                ClientsOtherThanPassthroughHostReadingInput() const;
    // Real-time safe: true if this client should be marked as reading input (not yet marked).
    bool                                ClientShouldMarkInputIORT(UInt32 inClientID) const;
    
private:
    void                                SendIORunningNotifications(bool sendIsRunningNotification,
                                                                   bool sendIsRunningSomewhereOtherThanNovaLINKAppNotification,
                                                                   bool sendInputRunningSomewhereOtherThanPassthroughHostNotification) const;
public:
    bool                                IsXPCHelper(UInt32 inClientID) const { return inClientID == mXPCHelperClientID; }
    // App or XPCHelper — either can host playthrough and should not trigger nested StartIO playthrough requests.
    // Matches every HAL client from those processes, not a single stored client ID.
    bool                                IsPassthroughHostRT(UInt32 inClientID) const;
    bool                                IsPassthroughHostNonRT(UInt32 inClientID) const;
    bool                                NovaLINKAppHasClientRegistered() const { return mNovaLINKAppClientCount > 0; }
    
    inline pid_t                        GetMusicPlayerProcessIDProperty() const { return mMusicPlayerProcessIDProperty; }
    inline CFStringRef                  CopyMusicPlayerBundleIDProperty() const { return mMusicPlayerBundleIDProperty.CopyCFString(); }
    
    // Returns true if the PID was changed
    bool                                SetMusicPlayer(const pid_t inPID);
    // Returns true if the bundle ID was changed
    bool                                SetMusicPlayer(const CACFString inBundleID);
    
    bool                                IsMusicPlayerRT(const UInt32 inClientID) const;
    
private:
    AudioObjectID                       mOwnerDeviceID;
    NovaLINK_ClientMap                       mClientMap;
    
    // Counters for the number of clients that are doing IO. These are used to tell whether any clients
    // are currently doing IO without having to check every client's mDoingIO.
    //
    // We need to reference count this rather than just using a bool because the HAL might (but usually
    // doesn't) call our StartIO/StopIO functions for clients other than the first to start and last to
    // stop.
    UInt64                              mStartCount = 0;
    UInt64                              mStartCountExcludingNovaLINKApp = 0;
    // Non-passthrough clients that have performed ReadInput in their current IO session.
    UInt64                              mInputStartCountExcludingPassthrough = 0;
    
    CAMutex                             mMutex { "Clients" };
    
    SInt64                              mNovaLINKAppClientID = -1;
    SInt64                              mXPCHelperClientID = -1;
    UInt32                              mNovaLINKAppClientCount = 0;
    UInt32                              mXPCHelperClientCount = 0;
    
    // The value of the kAudioDeviceCustomPropertyMusicPlayerProcessID property, or 0 if it's unset/null.
    // We store this separately because the music player might not always be a client, but could be added
    // as one at a later time.
    pid_t                               mMusicPlayerProcessIDProperty = 0;
    
    // The value of the kAudioDeviceCustomPropertyMusicPlayerBundleID property, or the empty string if it's
    // unset/null. UTF8 encoding.
    //
    // As with mMusicPlayerProcessID, we keep a copy of the bundle ID the user sets for the music player
    // because there might be no client with that bundle ID. In that case we need to be able to give the
    // property's value if the HAL asks for it, and to recognise the music player if it's added a client.
    CACFString                          mMusicPlayerBundleIDProperty { "" };
    
};

#pragma clang assume_nonnull end

#endif /* __NovaLINKDriver__NovaLINK_Clients__ */
