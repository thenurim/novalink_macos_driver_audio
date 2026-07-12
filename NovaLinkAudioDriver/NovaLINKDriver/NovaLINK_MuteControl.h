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
//  NovaLINK_MuteControl.h
//  NovaLINKDriver
//
//  Copyright © 2017 Kyle Neideck
//

#ifndef NovaLINKDriver__NovaLINK_MuteControl
#define NovaLINKDriver__NovaLINK_MuteControl

// Superclass Includes
#include "NovaLINK_Control.h"

// PublicUtility Includes
#include "CAMutex.h"

// System Includes
#include <MacTypes.h>
#include <CoreAudio/CoreAudio.h>


#pragma clang assume_nonnull begin

class NovaLINK_MuteControl
:
    public NovaLINK_Control
{

#pragma mark Construction/Destruction

public:
                              NovaLINK_MuteControl(AudioObjectID inObjectID,
                                              AudioObjectID inOwnerObjectID,
                                              AudioObjectPropertyScope inScope =
                                                      kAudioObjectPropertyScopeOutput,
                                              AudioObjectPropertyElement inElement =
                                                      kAudioObjectPropertyElementMaster);

#pragma mark Property Operations

    bool                      HasProperty(AudioObjectID inObjectID,
                                          pid_t inClientPID,
                                          const AudioObjectPropertyAddress& inAddress) const;

    bool                      IsPropertySettable(AudioObjectID inObjectID,
                                                 pid_t inClientPID,
                                                 const AudioObjectPropertyAddress& inAddress) const;

    UInt32                    GetPropertyDataSize(AudioObjectID inObjectID,
                                                  pid_t inClientPID,
                                                  const AudioObjectPropertyAddress& inAddress,
                                                  UInt32 inQualifierDataSize,
                                                  const void* inQualifierData) const;

    void                      GetPropertyData(AudioObjectID inObjectID,
                                              pid_t inClientPID,
                                              const AudioObjectPropertyAddress& inAddress,
                                              UInt32 inQualifierDataSize,
                                              const void* inQualifierData,
                                              UInt32 inDataSize,
                                              UInt32& outDataSize,
                                              void* outData) const;

    void                      SetPropertyData(AudioObjectID inObjectID,
                                              pid_t inClientPID,
                                              const AudioObjectPropertyAddress& inAddress,
                                              UInt32 inQualifierDataSize,
                                              const void* inQualifierData,
                                              UInt32 inDataSize,
                                              const void* inData);

#pragma mark Implementation

private:
    CAMutex                   mMutex;
    bool                      mMuted;

};

#pragma clang assume_nonnull end

#endif /* NovaLINKDriver__NovaLINK_MuteControl */

