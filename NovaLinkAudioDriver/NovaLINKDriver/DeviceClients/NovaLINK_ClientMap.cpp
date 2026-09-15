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
//  NovaLINK_ClientMap.cpp
//  NovaLINKDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//

// Self Include
#include "NovaLINK_ClientMap.h"

// Local Includes
#include "NovaLINK_Types.h"

// PublicUtility Includes
#include "CAException.h"


#pragma clang assume_nonnull begin

void    NovaLINK_ClientMap::AddClient(NovaLINK_Client inClient)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    // Add the new client to the shadow maps
    AddClientToShadowMaps(inClient);
    
    // Swap the maps with their shadow maps
    SwapInShadowMaps();
    
    // The shadow maps (which were the main maps until we swapped them) are now missing the new client. Add it again to
    // keep the sets of maps identical.
    AddClientToShadowMaps(inClient);

    // Insert the client into the past clients map. We do this here rather than in RemoveClient
    // because some apps add multiple clients with the same bundle ID and we want to give them all
    // the same settings.
    if(inClient.mBundleID.IsValid())
    {
        mPastClientMap[inClient.mBundleID] = inClient;
    }
}

void    NovaLINK_ClientMap::AddClientToShadowMaps(NovaLINK_Client inClient)
{
    ThrowIf(mClientMapShadow.count(inClient.mClientID) != 0,
            NovaLINK_InvalidClientException(),
            "NovaLINK_ClientMap::AddClientToShadowMaps: Tried to add client whose client ID was already in use");
    
    // Add to the client ID shadow map
    mClientMapShadow[inClient.mClientID] = inClient;
    
    // Get a reference to the client in the map so we can add it to the pointer maps
    NovaLINK_Client& clientInMap = mClientMapShadow.at(inClient.mClientID);
    
    // Add to the PID shadow map
    mClientMapByPIDShadow[inClient.mProcessID].push_back(&clientInMap);
    
    // Add to the bundle ID shadow map
    if(inClient.mBundleID.IsValid())
    {
        mClientMapByBundleIDShadow[inClient.mBundleID].push_back(&clientInMap);
    }
}

NovaLINK_Client    NovaLINK_ClientMap::RemoveClient(UInt32 inClientID)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    auto theClientItr = mClientMapShadow.find(inClientID);
    
    // Removing a client that was never added is an error
    ThrowIf(theClientItr == mClientMapShadow.end(),
            NovaLINK_InvalidClientException(),
            "NovaLINK_ClientMap::RemoveClient: Could not find client to be removed");
    
    NovaLINK_Client theClient = theClientItr->second;
    
    // Remove the client from the shadow maps
    mClientMapShadow.erase(theClientItr);
    mClientMapByPIDShadow.erase(theClient.mProcessID);
    if(theClient.mBundleID.IsValid())
    {
        mClientMapByBundleID.erase(theClient.mBundleID);
    }
    
    // Swap the maps with their shadow maps
    SwapInShadowMaps();
    
    // Erase the client again so the maps and their shadow maps are kept identical
    mClientMapShadow.erase(inClientID);
    mClientMapByPIDShadow.erase(theClient.mProcessID);
    if(theClient.mBundleID.IsValid())
    {
        mClientMapByBundleID.erase(theClient.mBundleID);
    }
    
    return theClient;
}

bool    NovaLINK_ClientMap::GetClientRT(UInt32 inClientID, NovaLINK_Client* outClient) const
{
    CAMutex::Locker theMapsLocker(mMapsMutex);
    return GetClient(mClientMap, inClientID, outClient);
}

bool    NovaLINK_ClientMap::GetClientNonRT(UInt32 inClientID, NovaLINK_Client* outClient) const
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    return GetClient(mClientMapShadow, inClientID, outClient);
}

//static
bool    NovaLINK_ClientMap::GetClient(const std::map<UInt32, NovaLINK_Client>& inClientMap, UInt32 inClientID, NovaLINK_Client* outClient)
{
    auto theClientItr = inClientMap.find(inClientID);
    
    if(theClientItr != inClientMap.end())
    {
        *outClient = theClientItr->second;
        return true;
    }
    
    return false;
}

std::vector<NovaLINK_Client> NovaLINK_ClientMap::GetClientsByPID(pid_t inPID) const
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    std::vector<NovaLINK_Client> theClients;
    
    auto theMapItr = mClientMapByPIDShadow.find(inPID);
    if(theMapItr != mClientMapByPIDShadow.end())
    {
        // Found clients with the PID, so copy them into the return vector
        for(auto& theClientPtrsItr : theMapItr->second)
        {
            theClients.push_back(*theClientPtrsItr);
        }
    }
    
    return theClients;
}

#pragma mark Music Player

void    NovaLINK_ClientMap::UpdateMusicPlayerFlags(pid_t inMusicPlayerPID)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    auto theIsMusicPlayerTest = [&] (NovaLINK_Client theClient) {
        return (theClient.mProcessID == inMusicPlayerPID);
    };
    
    UpdateMusicPlayerFlagsInShadowMaps(theIsMusicPlayerTest);
    SwapInShadowMaps();
    UpdateMusicPlayerFlagsInShadowMaps(theIsMusicPlayerTest);
}

void    NovaLINK_ClientMap::UpdateMusicPlayerFlags(CACFString inMusicPlayerBundleID)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    auto theIsMusicPlayerTest = [&] (NovaLINK_Client theClient) {
        return (theClient.mBundleID.IsValid() && theClient.mBundleID == inMusicPlayerBundleID);
    };
    
    UpdateMusicPlayerFlagsInShadowMaps(theIsMusicPlayerTest);
    SwapInShadowMaps();
    UpdateMusicPlayerFlagsInShadowMaps(theIsMusicPlayerTest);
}

void    NovaLINK_ClientMap::UpdateMusicPlayerFlagsInShadowMaps(std::function<bool(NovaLINK_Client)> inIsMusicPlayerTest)
{
    for(auto& theItr : mClientMapShadow)
    {
        NovaLINK_Client& theClient = theItr.second;
        theClient.mIsMusicPlayer = inIsMusicPlayerTest(theClient);
    }
}

void    NovaLINK_ClientMap::UpdateClientIOStateNonRT(UInt32 inClientID, bool inDoingIO)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);
    
    mClientMapShadow[inClientID].mDoingIO = inDoingIO;
    if(!inDoingIO)
    {
        mClientMapShadow[inClientID].mDoingInputIO = false;
    }
    SwapInShadowMaps();
    mClientMapShadow[inClientID].mDoingIO = inDoingIO;
    if(!inDoingIO)
    {
        mClientMapShadow[inClientID].mDoingInputIO = false;
    }
}

void    NovaLINK_ClientMap::StartInputIONonRT(UInt32 inClientID)
{
    CAMutex::Locker theShadowMapsLocker(mShadowMapsMutex);

    mClientMapShadow[inClientID].mDoingInputIO = true;
    SwapInShadowMaps();
    mClientMapShadow[inClientID].mDoingInputIO = true;
}

void    NovaLINK_ClientMap::SwapInShadowMaps()
{
    mTaskQueue->QueueSync_SwapClientShadowMaps(this);
}

void    NovaLINK_ClientMap::SwapInShadowMapsRT()
{
#if DEBUG
    // This method should only be called by the realtime worker thread in NovaLINK_TaskQueue. The only safe way to call it is on a realtime
    // thread while a non-realtime thread is holding the shadow maps mutex. (These assertions assume that the realtime worker thread is
    // the only thread we'll call this on, but we could decide to change that at some point.)
    mTaskQueue->AssertCurrentThreadIsRTWorkerThread("NovaLINK_ClientMap::SwapInShadowMapsRT");
    
    Assert(!mShadowMapsMutex.IsFree(), "Can't swap in the shadow maps while the shadow maps mutex is free");
    Assert(!mShadowMapsMutex.IsOwnedByCurrentThread(), "The shadow maps mutex should not be held by a realtime thread");
#endif
    
    CAMutex::Locker theMapsLocker(mMapsMutex);
    
    mClientMap.swap(mClientMapShadow);
    mClientMapByPID.swap(mClientMapByPIDShadow);
    mClientMapByBundleID.swap(mClientMapByBundleIDShadow);
}

#pragma clang assume_nonnull end
