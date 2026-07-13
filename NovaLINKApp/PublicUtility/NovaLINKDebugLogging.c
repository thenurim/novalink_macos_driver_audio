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
//  NovaLINKDebugLogging.c
//  PublicUtility
//
//  Copyright © 2020, 2024 Kyle Neideck
//

// Self Include
#include "NovaLINKDebugLogging.h"


#pragma clang assume_nonnull begin

// It's probably not ideal to use a global variable for this, but it's a lot easier.
#if DEBUG || CoreAudio_Debug
    // Enable debug logging by default in debug builds.
    int gDebugLoggingIsEnabled = 1;
#else
    int gDebugLoggingIsEnabled = 0;
#endif

// We don't bother synchronising accesses of gDebugLoggingIsEnabled because it isn't really
// necessary and would complicate code that accesses it on realtime threads.
int NovaLINKDebugLoggingIsEnabled(void)
{
    return gDebugLoggingIsEnabled;
}

void NovaLINKSetDebugLoggingEnabled(int inEnabled)
{
    gDebugLoggingIsEnabled = inEnabled;
}

#pragma clang assume_nonnull end

