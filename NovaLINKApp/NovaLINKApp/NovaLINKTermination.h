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
//  NovaLINKTermination.h
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//
//  Cleans up if NovaLINKApp crashes because of an uncaught C++ or Objective-C exception, or is sent
//  SIGINT/SIGTERM/SIGQUIT. Currently, it just changes the default output device from NovaLINKDevice to
//  the real output device and records debug info for some types of crashes.
//
//  NovaLINKXPCHelper also changes the default device if NovaLINKApp disconnects and leaves NovaLINKDevice as the
//  default. This handles cases like segfaults where it wouldn't be safe to clean up from the
//  crashing process.
//

#ifndef NovaLINKApp__NovaLINKTermination
#define NovaLINKApp__NovaLINKTermination

// Local Includes
#import "NovaLINKAudioDeviceManager.h"

// PublicUtility Includes
#import "CAPThread.h"

// STL Includes
#import <exception>


#pragma clang assume_nonnull begin

class NovaLINKTermination
{

public:
    /*!
     Starts a thread that will clean up before exiting if NovaLINKApp receives SIGINT, SIGTERM or
     SIGQUIT. Sets a similar clean up function to run if NovaLINKApp terminates due to an uncaught
     exception.
     */
    static void                      SetUpTerminationCleanUp(NovaLINKAudioDeviceManager* inAudioDevices);

    /*! Some commented out ways to have NovaLINKApp crash for testing. Does nothing if unmodified. */
    static void                      TestCrash() __attribute__((noinline));

private:
    static void                      StartExitSignalsThread();

    static void                      CleanUpAudioDevices();

    /*! Adds some info about the uncaught exception that caused a crash to the crash report. */
    static void                      AddCurrentExceptionToCrashReport();

    /*! The entry point for sExitSignalsThread. */
    static void* __nullable          ExitSignalsProc(void* __nullable ignored);

    /*! The thread that handles SIGQUIT, SIGTERM and SIGINT. Never destroyed. */
    static CAPThread* const          sExitSignalsThread;
    static sigset_t                  sExitSignals;

    /*! The function that handles std::terminate by default. */
    static std::terminate_handler    sOriginalTerminateHandler;

    /*! The audio device manager. (Must be static to be accessed in our std::terminate_handler.) */
    static NovaLINKAudioDeviceManager* __nullable sAudioDevices;

};

#pragma clang assume_nonnull end

#endif /* NovaLINKApp__NovaLINKTermination */

