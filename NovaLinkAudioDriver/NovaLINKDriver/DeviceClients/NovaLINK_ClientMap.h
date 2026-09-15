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
//  NovaLINK_ClientMap.h
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef __NovaLINKDriver__NovaLINK_ClientMap__
#define __NovaLINKDriver__NovaLINK_ClientMap__

// Local Includes
#include "NovaLINK_Client.h"
#include "NovaLINK_TaskQueue.h"

// PublicUtility Includes
#include "CAMutex.h"
#include "CACFString.h"

// STL Includes
#include <map>
#include <vector>
#include <functional>


// Forward Declarations
class NovaLINK_ClientTasks;


#pragma clang assume_nonnull begin

//==================================================================================================
//	NovaLINK_ClientMap
//
//  This class stores the clients (NovaLINK_Client) that have been registered with NovaLINKDevice by the HAL.
//  It also maintains maps from clients' PIDs and bundle IDs to the clients. When a client is
//  removed by the HAL we add it to a map of past clients to keep track of settings specific to that
//  client.
//
//  Since the maps are read from during IO, this class has to be real-time safe when accessing
//  them. So each map has an identical "shadow" map, which we use to buffer updates.
//
//  To update the clients we lock the shadow maps, modify them, have NovaLINK_TaskQueue's real-time
//  thread swap them with the main maps, and then repeat the modification to keep both sets of maps
//  identical. We have to swap the maps on a real-time thread so we can take the main maps' lock
//  without risking priority inversion, but this way the actual work doesn't need to be real-time
//  safe.
//
//  Methods that only read from the maps and are called on non-real-time threads will just read
//  from the shadow maps because it's easier.
//
//  Methods whose names end with "RT" and "NonRT" can only safely be called from real-time and
//  non-real-time threads respectively. (Methods with neither are most likely non-RT.)
//==================================================================================================

class NovaLINK_ClientMap
{
    
    friend class NovaLINK_ClientTasks;
    
    typedef std::vector<NovaLINK_Client*> NovaLINK_ClientPtrList;
    
public:
                                                        NovaLINK_ClientMap(NovaLINK_TaskQueue* inTaskQueue) : mTaskQueue(inTaskQueue), mMapsMutex("Maps mutex"), mShadowMapsMutex("Shadow maps mutex") { };

    void                                                AddClient(NovaLINK_Client inClient);
    
private:
    void                                                AddClientToShadowMaps(NovaLINK_Client inClient);
    
public:
    // Returns the removed client
    NovaLINK_Client                                          RemoveClient(UInt32 inClientID);
    
    // These methods are functionally identical except that GetClientRT must only be called from real-time threads and GetClientNonRT
    // must only be called from non-real-time threads. Both return true if a client was found.
    bool                                                GetClientRT(UInt32 inClientID, NovaLINK_Client* outClient) const;
    bool                                                GetClientNonRT(UInt32 inClientID, NovaLINK_Client* outClient) const;
    
private:
    static bool                                         GetClient(const std::map<UInt32, NovaLINK_Client>& inClientMap,
                                                                  UInt32 inClientID,
                                                                  NovaLINK_Client* outClient);
    
public:
    std::vector<NovaLINK_Client>                             GetClientsByPID(pid_t inPID) const;
    
    // Set the isMusicPlayer flag for each client. (True if the client has the given bundle ID/PID, false otherwise.)
    void                                                UpdateMusicPlayerFlags(pid_t inMusicPlayerPID);
    void                                                UpdateMusicPlayerFlags(CACFString inMusicPlayerBundleID);
    
private:
    void                                                UpdateMusicPlayerFlagsInShadowMaps(std::function<bool(NovaLINK_Client)> inIsMusicPlayerTest);
    
public:
    void                                                StartIONonRT(UInt32 inClientID) { UpdateClientIOStateNonRT(inClientID, true); }
    void                                                StopIONonRT(UInt32 inClientID) { UpdateClientIOStateNonRT(inClientID, false); }
    void                                                StartInputIONonRT(UInt32 inClientID);
    
private:
    void                                                UpdateClientIOStateNonRT(UInt32 inClientID, bool inDoingIO);
    
    // Has a real-time thread call SwapInShadowMapsRT. (Synchronously queues the call as a task on mTaskQueue.)
    // The shadow maps mutex must be locked when calling this method.
    void                                                SwapInShadowMaps();
    // Note that this method is called by NovaLINK_TaskQueue through the NovaLINK_ClientTasks interface. The shadow maps
    // mutex must be locked when calling this method.
    void                                                SwapInShadowMapsRT();
    
private:
    NovaLINK_TaskQueue*                                      mTaskQueue;
    
    // Must be held to access mClientMap or mClientMapByPID. Code that runs while holding this mutex needs
    // to be real-time safe. Should probably not be held for most operations on mClientMapByBundleID because,
    // as far as I can tell, code that works with CFStrings is unlikely to be real-time safe.
    CAMutex                                             mMapsMutex;
    // Should only be locked by non-real-time threads. Should not be released until the maps have been
    // made identical to their shadow maps.
    CAMutex                                             mShadowMapsMutex;
    
    // The clients currently registered with NovaLINKDevice. Indexed by client ID.
    std::map<UInt32, NovaLINK_Client>                        mClientMap;
    // We keep this in sync with mClientMap so it can be modified outside of real-time safe sections and
    // then swapped in on a real-time thread, which is safe.
    std::map<UInt32, NovaLINK_Client>                        mClientMapShadow;
    
    // These maps hold lists of pointers to clients in mClientMap/mClientMapShadow. Lists because a process
    // can have multiple clients and clients can have the same bundle ID.
    
    std::map<pid_t, NovaLINK_ClientPtrList>                  mClientMapByPID;
    std::map<pid_t, NovaLINK_ClientPtrList>                  mClientMapByPIDShadow;
    
    std::map<CACFString, NovaLINK_ClientPtrList>             mClientMapByBundleID;
    std::map<CACFString, NovaLINK_ClientPtrList>             mClientMapByBundleIDShadow;
    
    // Clients are added to mPastClientMap so we can restore settings specific to them if they get
    // added again.
    std::map<CACFString, NovaLINK_Client>                    mPastClientMap;
    
};

#pragma clang assume_nonnull end

#endif /* __NovaLINKDriver__NovaLINK_ClientMap__ */
