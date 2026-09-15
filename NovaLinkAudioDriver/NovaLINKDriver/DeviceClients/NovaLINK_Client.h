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
//  NovaLINK_Client.h
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef __NovaLINKDriver__NovaLINK_Client__
#define __NovaLINKDriver__NovaLINK_Client__

// PublicUtility Includes
#include "CACFString.h"

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


#pragma clang assume_nonnull begin

//==================================================================================================
//	NovaLINK_Client
//
//  Client meaning a client (of the host) of the NovaLINKDevice, i.e. an app registered with the HAL,
//  generally so it can do IO at some point.
//==================================================================================================

class NovaLINK_Client
{
    
public:
                                  NovaLINK_Client() = default;
                                  NovaLINK_Client(const AudioServerPlugInClientInfo* inClientInfo);
                                  ~NovaLINK_Client() = default;
                                  NovaLINK_Client(const NovaLINK_Client& inClient) { Copy(inClient); };
                                  NovaLINK_Client& operator=(const NovaLINK_Client& inClient) { Copy(inClient); return *this; }
    
private:
    void                          Copy(const NovaLINK_Client& inClient);
    
public:
    // These fields are duplicated from AudioServerPlugInClientInfo (except the mBundleID CFStringRef is
    // wrapped in a CACFString here).
    UInt32                        mClientID;
    pid_t                         mProcessID;
    Boolean                       mIsNativeEndian = true;
    CACFString                    mBundleID;
    
    // Becomes true when the client triggers the plugin host to call StartIO or to begin
    // kAudioServerPlugInIOOperationThread, and false again on StopIO or when
    // kAudioServerPlugInIOOperationThread ends
    bool                          mDoingIO = false;

    // True after the client has performed at least one kAudioServerPlugInIOOperationReadInput
    // in the current IO session. Cleared on StopIO. Distinguishes capture clients from
    // output-only clients that share the same StartIO.
    bool                          mDoingInputIO = false;
    
    // True if NovaLINKApp has set this client as belonging to the music player app
    bool                          mIsMusicPlayer = false;
    
};

#pragma clang assume_nonnull end

#endif /* __NovaLINKDriver__NovaLINK_Client__ */

