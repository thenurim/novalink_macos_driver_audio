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
//  NovaLINK_XPCHelper.h
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

#ifndef NovaLINKDriver__NovaLINK_XPCHelper
#define NovaLINKDriver__NovaLINK_XPCHelper

// System Includes
#include <MacTypes.h>

#if defined(__cplusplus)
extern "C" {
#endif

// On failure, returns one of the kNovaLINKXPC_* error codes, or the error code received from NovaLINKXPCHelper. Returns kNovaLINKXPC_Success otherwise.
UInt64 StartNovaLINKAppPlayThroughSync(bool inIsForUISoundsDevice);

// Starts Helper-hosted fallback playthrough when the companion app is not registered.
UInt64 StartFallbackPlayThroughSync(bool inIsForUISoundsDevice);

#if defined(__cplusplus)
}
#endif

#endif /* NovaLINKDriver__NovaLINK_XPCHelper */

