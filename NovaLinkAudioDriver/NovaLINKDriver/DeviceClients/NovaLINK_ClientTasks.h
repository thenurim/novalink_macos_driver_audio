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
//  NovaLINK_ClientTasks.h
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//
//  The interface between the client classes (NovaLINK_Client, NovaLINK_Clients and NovaLINK_ClientMap) and NovaLINK_TaskQueue.
//

#ifndef __NovaLINKDriver__NovaLINK_ClientTasks__
#define __NovaLINKDriver__NovaLINK_ClientTasks__

// Local Includes
#include "NovaLINK_Clients.h"
#include "NovaLINK_ClientMap.h"


// Forward Declarations
class NovaLINK_TaskQueue;


#pragma clang assume_nonnull begin

class NovaLINK_ClientTasks
{
    
    friend class NovaLINK_TaskQueue;
    
private:
    static bool                            StartIONonRT(NovaLINK_Clients* inClients, UInt32 inClientID) { return inClients->StartIONonRT(inClientID); }
    static bool                            StopIONonRT(NovaLINK_Clients* inClients, UInt32 inClientID) { return inClients->StopIONonRT(inClientID); }
    static void                            StartInputIONonRT(NovaLINK_Clients* inClients, UInt32 inClientID) { inClients->StartInputIONonRT(inClientID); }
    
    static void                            SwapInShadowMapsRT(NovaLINK_ClientMap* inClientMap) { inClientMap->SwapInShadowMapsRT(); }
    
};

#pragma clang assume_nonnull end

#endif /* __NovaLINKDriver__NovaLINK_ClientTasks__ */

