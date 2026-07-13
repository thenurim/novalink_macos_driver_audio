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
//  MockAudioDevice.cpp
//  NovaLINKAppUnitTests
//
//  Copyright © 2020 Kyle Neideck
//

// Self Include
#include "MockAudioDevice.h"

// NovaLINK Includes
#include "NovaLINK_Types.h"

// STL Includes
#include <functional>


MockAudioDevice::MockAudioDevice(const std::string& inUID)
:
    mUID(inUID),
    mNominalSampleRate(44100.0),
    mIOBufferSize(512),
    MockAudioObject(static_cast<AudioObjectID>(std::hash<std::string>{}(inUID)))
{
}

CACFString MockAudioDevice::GetPlayerBundleID() const
{
    if(mUID != kNovaLINKDeviceUID)
    {
        throw "Only NovaLINKDevice has kAudioDeviceCustomPropertyMusicPlayerBundleID";
    }

    return mPlayerBundleID;
}

void MockAudioDevice::SetPlayerBundleID(const CACFString& inPlayerBundleID)
{
    if(mUID != kNovaLINKDeviceUID)
    {
        throw "Only NovaLINKDevice has kAudioDeviceCustomPropertyMusicPlayerBundleID";
    }

    mPlayerBundleID = inPlayerBundleID;
}

