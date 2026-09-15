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
//  NovaLINK_TaskQueue.cpp
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

// Self Include
#include "NovaLINK_TaskQueue.h"

// Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_Utils.h"
#include "NovaLINK_PlugIn.h"
#include "NovaLINK_Clients.h"
#include "NovaLINK_ClientMap.h"
#include "NovaLINK_ClientTasks.h"

// PublicUtility Includes
#include "CAException.h"
#include "CADebugMacros.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include "CAAtomic.h"
#pragma clang diagnostic pop

// System Includes
#include <mach/mach_init.h>
#include <mach/mach_time.h>
#include <mach/task.h>


#pragma clang assume_nonnull begin

#pragma mark Construction/destruction

NovaLINK_TaskQueue::NovaLINK_TaskQueue()
:
    // The inline documentation for thread_time_constraint_policy.period says "A value of 0 indicates that there is no
    // inherent periodicity in the computation". So I figure setting the period to 0 means the scheduler will take as long
    // as it wants to wake our real-time thread, which is fine for us, but once it has only other real-time threads can
    // preempt us. (And that's only if they won't make our computation take longer than kRealTimeThreadMaximumComputationNs).
    mRealTimeThread(&NovaLINK_TaskQueue::RealTimeThreadProc,
                    this,
                    /* inPeriod = */ 0,
                    NanosToAbsoluteTime(kRealTimeThreadNominalComputationNs),
                    NanosToAbsoluteTime(kRealTimeThreadMaximumComputationNs),
                    /* inIsPreemptible = */ true),
    mNonRealTimeThread(&NovaLINK_TaskQueue::NonRealTimeThreadProc, this)
{
    // Init the semaphores
    auto createSemaphore = [] () {
        semaphore_t theSemaphore;
        kern_return_t theError = semaphore_create(mach_task_self(), &theSemaphore, SYNC_POLICY_FIFO, 0);
        
        NovaLINK_Utils::ThrowIfMachError("NovaLINK_TaskQueue::NovaLINK_TaskQueue", "semaphore_create", theError);
        
        ThrowIf(theSemaphore == SEMAPHORE_NULL,
                CAException(kAudioHardwareUnspecifiedError),
                "NovaLINK_TaskQueue::NovaLINK_TaskQueue: Could not create semaphore");
        
        return theSemaphore;
    };
    
    mRealTimeThreadWorkQueuedSemaphore = createSemaphore();
    mNonRealTimeThreadWorkQueuedSemaphore = createSemaphore();
    mRealTimeThreadSyncTaskCompletedSemaphore = createSemaphore();
    mNonRealTimeThreadSyncTaskCompletedSemaphore = createSemaphore();
    
    // Pre-allocate enough tasks in mNonRealTimeThreadTasksFreeList that the real-time threads should never have to
    // allocate memory when adding a task to the non-realtime queue.
    for(UInt32 i = 0; i < kNonRealTimeThreadTaskBufferSize; i++)
    {
        NovaLINK_Task* theTask = new NovaLINK_Task;
        mNonRealTimeThreadTasksFreeList.push_NA(theTask);
    }
    
    // Start the worker threads
    mRealTimeThread.Start();
    mNonRealTimeThread.Start();
}

NovaLINK_TaskQueue::~NovaLINK_TaskQueue()
{
    // Join the worker threads
    NovaLINKLogAndSwallowExceptionsMsg("NovaLINK_TaskQueue::~NovaLINK_TaskQueue", "QueueSync", ([&] {
        QueueSync(kNovaLINKTaskStopWorkerThread, /* inRunOnRealtimeThread = */ true);
        QueueSync(kNovaLINKTaskStopWorkerThread, /* inRunOnRealtimeThread = */ false);
    }));

    // Destroy the semaphores
    auto destroySemaphore = [] (semaphore_t inSemaphore) {
        kern_return_t theError = semaphore_destroy(mach_task_self(), inSemaphore);
        
        NovaLINK_Utils::LogIfMachError("NovaLINK_TaskQueue::~NovaLINK_TaskQueue", "semaphore_destroy", theError);
    };
    
    destroySemaphore(mRealTimeThreadWorkQueuedSemaphore);
    destroySemaphore(mNonRealTimeThreadWorkQueuedSemaphore);
    destroySemaphore(mRealTimeThreadSyncTaskCompletedSemaphore);
    destroySemaphore(mNonRealTimeThreadSyncTaskCompletedSemaphore);
    
    NovaLINK_Task* theTask;
    
    // Delete the tasks in the non-realtime tasks free list
    while((theTask = mNonRealTimeThreadTasksFreeList.pop_atomic()) != NULL)
    {
        delete theTask;
    }
    
    // Delete any tasks left on the non-realtime queue that need to be
    while((theTask = mNonRealTimeThreadTasks.pop_atomic()) != NULL)
    {
        if(!theTask->IsSync())
        {
            delete theTask;
        }
    }
}

//static
UInt32  NovaLINK_TaskQueue::NanosToAbsoluteTime(UInt32 inNanos)
{
    // Converts a duration from nanoseconds to absolute time (i.e. number of bus cycles). Used for calculating
    // the real-time thread's time constraint policy.
    
    mach_timebase_info_data_t theTimebaseInfo;
    mach_timebase_info(&theTimebaseInfo);
    
    Float64 theTicksPerNs = static_cast<Float64>(theTimebaseInfo.denom) / theTimebaseInfo.numer;
    return static_cast<UInt32>(inNanos * theTicksPerNs);
}

#pragma mark Task queueing

void    NovaLINK_TaskQueue::QueueSync_SwapClientShadowMaps(NovaLINK_ClientMap* inClientMap)
{
    // TODO: Is there any reason to use uintptr_t when we pass pointers to tasks like this? I can't think of any
    //       reason for a system to have (non-function) pointers larger than 64-bit, so I figure they should fit.
    //
    //       From http://en.cppreference.com/w/cpp/language/reinterpret_cast:
    //       "A pointer converted to an integer of sufficient size and back to the same pointer type is guaranteed
    //        to have its original value [...]"
    QueueSync(kNovaLINKTaskSwapClientShadowMaps, /* inRunOnRealtimeThread = */ true, reinterpret_cast<UInt64>(inClientMap));
}

void    NovaLINK_TaskQueue::QueueAsync_SendPropertyNotification(AudioObjectPropertySelector inProperty, AudioObjectID inDeviceID)
{
    DebugMsg("NovaLINK_TaskQueue::QueueAsync_SendPropertyNotification: Queueing property notification. inProperty=%u inDeviceID=%u",
             inProperty,
             inDeviceID);
    NovaLINK_Task theTask(kNovaLINKTaskSendPropertyNotification, /* inIsSync = */ false, inProperty, inDeviceID);
    QueueOnNonRealtimeThread(theTask);
}

bool    NovaLINK_TaskQueue::Queue_UpdateClientIOState(bool inSync, NovaLINK_Clients* inClients, UInt32 inClientID, bool inDoingIO)
{
    DebugMsg("NovaLINK_TaskQueue::Queue_UpdateClientIOState: Queueing %s %s",
             (inDoingIO ? "kNovaLINKTaskStartClientIO" : "kNovaLINKTaskStopClientIO"),
             (inSync ? "synchronously" : "asynchronously"));
    
    NovaLINK_TaskID theTaskID = (inDoingIO ? kNovaLINKTaskStartClientIO : kNovaLINKTaskStopClientIO);
    UInt64 theClientsPtrArg = reinterpret_cast<UInt64>(inClients);
    UInt64 theClientIDTaskArg = static_cast<UInt64>(inClientID);
    
    if(inSync)
    {
        return QueueSync(theTaskID, false, theClientsPtrArg, theClientIDTaskArg);
    }
    else
    {
        NovaLINK_Task theTask(theTaskID, /* inIsSync = */ false, theClientsPtrArg, theClientIDTaskArg);
        QueueOnNonRealtimeThread(theTask);
        
        // This method's return value isn't used when queueing async, because we can't know what it should be yet.
        return false;
    }
}

void    NovaLINK_TaskQueue::QueueAsync_StartClientInputIO(NovaLINK_Clients* inClients, UInt32 inClientID)
{
    DebugMsg("NovaLINK_TaskQueue::QueueAsync_StartClientInputIO: Queueing kNovaLINKTaskStartClientInputIO asynchronously");
    NovaLINK_Task theTask(kNovaLINKTaskStartClientInputIO,
                          /* inIsSync = */ false,
                          reinterpret_cast<UInt64>(inClients),
                          static_cast<UInt64>(inClientID));
    QueueOnNonRealtimeThread(theTask);
}

UInt64    NovaLINK_TaskQueue::QueueSync(NovaLINK_TaskID inTaskID, bool inRunOnRealtimeThread, UInt64 inTaskArg1, UInt64 inTaskArg2)
{
    DebugMsg("NovaLINK_TaskQueue::QueueSync: Queueing task synchronously to be processed on the %s thread. inTaskID=%d inTaskArg1=%llu inTaskArg2=%llu",
             (inRunOnRealtimeThread ? "realtime" : "non-realtime"),
             inTaskID,
             inTaskArg1,
             inTaskArg2);
    
    // Create the task
    NovaLINK_Task theTask(inTaskID, /* inIsSync = */ true, inTaskArg1, inTaskArg2);
    
    // Add the task to the queue
    TAtomicStack<NovaLINK_Task>& theTasks = (inRunOnRealtimeThread ? mRealTimeThreadTasks : mNonRealTimeThreadTasks);
    theTasks.push_atomic(&theTask);
    
    // Wake the worker thread so it'll process the task. (Note that semaphore_signal has an implicit barrier.)
    kern_return_t theError = semaphore_signal(inRunOnRealtimeThread ? mRealTimeThreadWorkQueuedSemaphore : mNonRealTimeThreadWorkQueuedSemaphore);
    NovaLINK_Utils::ThrowIfMachError("NovaLINK_TaskQueue::QueueSync", "semaphore_signal", theError);
    
    // Wait until the task has been processed.
    //
    // The worker thread signals all threads waiting on this semaphore when it finishes a task. The comments in WorkerThreadProc
    // explain why we have to check the condition in a loop here.
    bool didLogTimeoutMessage = false;
    while(!theTask.IsComplete())
    {
        semaphore_t theTaskCompletedSemaphore =
            inRunOnRealtimeThread ? mRealTimeThreadSyncTaskCompletedSemaphore : mNonRealTimeThreadSyncTaskCompletedSemaphore;
        // TODO: Because the worker threads use semaphore_signal_all instead of semaphore_signal, a thread can miss the signal if
        //       it isn't waiting at the right time. Using a timeout for now as a temporary fix so threads don't get stuck here.
        theError = semaphore_timedwait(theTaskCompletedSemaphore,
                                       (mach_timespec_t){ 0, kRealTimeThreadMaximumComputationNs * 4 });
        
        if(theError == KERN_OPERATION_TIMED_OUT)
        {
            if(!didLogTimeoutMessage && inRunOnRealtimeThread)
            {
                DebugMsg("NovaLINK_TaskQueue::QueueSync: Task %d taking longer than expected.", theTask.GetTaskID());
                didLogTimeoutMessage = true;
            }
        }
        else
        {
            NovaLINK_Utils::ThrowIfMachError("NovaLINK_TaskQueue::QueueSync", "semaphore_timedwait", theError);
        }
        
        CAMemoryBarrier();
    }
    
    if(didLogTimeoutMessage)
    {
        DebugMsg("NovaLINK_TaskQueue::QueueSync: Late task %d finished.", theTask.GetTaskID());
    }
    
    if(theTask.GetReturnValue() != INT64_MAX)
    {
        DebugMsg("NovaLINK_TaskQueue::QueueSync: Task %d returned %llu.", theTask.GetTaskID(), theTask.GetReturnValue());
    }
    
    return theTask.GetReturnValue();
}

void   NovaLINK_TaskQueue::QueueOnNonRealtimeThread(NovaLINK_Task inTask)
{
    // Add the task to our task list
    NovaLINK_Task* freeTask = mNonRealTimeThreadTasksFreeList.pop_atomic();
    
    if(freeTask == NULL)
    {
        LogWarning("NovaLINK_TaskQueue::QueueOnNonRealtimeThread: No pre-allocated tasks left in the free list. Allocating new task.");
        freeTask = new NovaLINK_Task;
    }
    
    *freeTask = inTask;
    
    mNonRealTimeThreadTasks.push_atomic(freeTask);
    
    // Signal the worker thread to process the task. (Note that semaphore_signal has an implicit barrier.)
    kern_return_t theError = semaphore_signal(mNonRealTimeThreadWorkQueuedSemaphore);
    NovaLINK_Utils::ThrowIfMachError("NovaLINK_TaskQueue::QueueOnNonRealtimeThread", "semaphore_signal", theError);
}

#pragma mark Worker threads

void    NovaLINK_TaskQueue::AssertCurrentThreadIsRTWorkerThread(const char* inCallerMethodName)
{
#if DEBUG  // This Assert macro always checks the condition, even in release builds if the compiler doesn't optimise it away
    if(!mRealTimeThread.IsCurrentThread())
    {
        DebugMsg("%s should only be called on the realtime worker thread.", inCallerMethodName);
        __ASSERT_STOP;  // TODO: Figure out a better way to assert with a formatted message
    }
    
    Assert(mRealTimeThread.IsTimeConstraintThread(), "mRealTimeThread should be in a time-constraint priority band.");
#else
    #pragma unused (inCallerMethodName)
#endif
}

//static
void* __nullable    NovaLINK_TaskQueue::RealTimeThreadProc(void* inRefCon)
{
    DebugMsg("NovaLINK_TaskQueue::RealTimeThreadProc: The realtime worker thread has started");
    
    NovaLINK_TaskQueue* refCon = static_cast<NovaLINK_TaskQueue*>(inRefCon);
    refCon->WorkerThreadProc(refCon->mRealTimeThreadWorkQueuedSemaphore,
                             refCon->mRealTimeThreadSyncTaskCompletedSemaphore,
                             &refCon->mRealTimeThreadTasks,
                             NULL,
                             [&] (NovaLINK_Task* inTask) { return refCon->ProcessRealTimeThreadTask(inTask); });
    
    return NULL;
}

//static
void* __nullable    NovaLINK_TaskQueue::NonRealTimeThreadProc(void* inRefCon)
{
    DebugMsg("NovaLINK_TaskQueue::NonRealTimeThreadProc: The non-realtime worker thread has started");
    
    NovaLINK_TaskQueue* refCon = static_cast<NovaLINK_TaskQueue*>(inRefCon);
    refCon->WorkerThreadProc(refCon->mNonRealTimeThreadWorkQueuedSemaphore,
                             refCon->mNonRealTimeThreadSyncTaskCompletedSemaphore,
                             &refCon->mNonRealTimeThreadTasks,
                             &refCon->mNonRealTimeThreadTasksFreeList,
                             [&] (NovaLINK_Task* inTask) { return refCon->ProcessNonRealTimeThreadTask(inTask); });
    
    return NULL;
}

void    NovaLINK_TaskQueue::WorkerThreadProc(semaphore_t inWorkQueuedSemaphore, semaphore_t inSyncTaskCompletedSemaphore, TAtomicStack<NovaLINK_Task>* inTasks, TAtomicStack2<NovaLINK_Task>* __nullable inFreeList, std::function<bool(NovaLINK_Task*)> inProcessTask)
{
    bool theThreadShouldStop = false;
    
    while(!theThreadShouldStop)
    {
        // Wait until a thread signals that it's added tasks to the queue.
        //
        // Note that we don't have to hold any lock before waiting. If the semaphore is signalled before we begin waiting we'll
        // still get the signal after we do.
        kern_return_t theError = semaphore_wait(inWorkQueuedSemaphore);
        NovaLINK_Utils::ThrowIfMachError("NovaLINK_TaskQueue::WorkerThreadProc", "semaphore_wait", theError);
        
        // Fetch the tasks from the queue.
        //
        // The tasks need to be processed in the order they were added to the queue. Since pop_all_reversed is atomic, other threads
        // can't add new tasks while we're reading, which would mix up the order.
        NovaLINK_Task* theTask = inTasks->pop_all_reversed();
        
        while(theTask != NULL &&
              !theThreadShouldStop)  // Stop processing tasks if we're shutting down
        {
            NovaLINK_Task* theNextTask = theTask->mNext;
            
            NovaLINKAssert(!theTask->IsComplete(),
                      "NovaLINK_TaskQueue::WorkerThreadProc: Cannot process already completed task (ID %d)",
                      theTask->GetTaskID());
            
            NovaLINKAssert(theTask != theNextTask,
                      "NovaLINK_TaskQueue::WorkerThreadProc: NovaLINK_Task %p (ID %d) was added to %s multiple times. arg1=%llu arg2=%llu",
                      theTask,
                      theTask->GetTaskID(),
                      (inTasks == &mRealTimeThreadTasks ? "mRealTimeThreadTasks" : "mNonRealTimeThreadTasks"),
                      theTask->GetArg1(),
                      theTask->GetArg2());
            
            // Process the task
            theThreadShouldStop = inProcessTask(theTask);
            
            // If the task was queued synchronously, let the thread that queued it know we're finished
            if(theTask->IsSync())
            {
                // Marking the task as completed allows QueueSync to return, which means it's possible for theTask to point to
                // invalid memory after this point.
                CAMemoryBarrier();
                theTask->MarkCompleted();
                
                // Signal any threads waiting for their task to be processed.
                //
                // We use semaphore_signal_all instead of semaphore_signal to avoid a race condition in QueueSync. It's possible
                // for threads calling QueueSync to wait on the semaphore in an order different to the order of the tasks they just
                // added to the queue. So after each task is completed we have every waiting thread check if it was theirs.
                //
                // Note that semaphore_signal_all has an implicit barrier.
                theError = semaphore_signal_all(inSyncTaskCompletedSemaphore);
                NovaLINK_Utils::ThrowIfMachError("NovaLINK_TaskQueue::WorkerThreadProc", "semaphore_signal_all", theError);
            }
            else if(inFreeList != NULL)
            {
                // After completing an async task, move it to the free list so the memory can be reused
                inFreeList->push_atomic(theTask);
            }
            
            theTask = theNextTask;
        }
    }
}

bool    NovaLINK_TaskQueue::ProcessRealTimeThreadTask(NovaLINK_Task* inTask)
{
    AssertCurrentThreadIsRTWorkerThread("NovaLINK_TaskQueue::ProcessRealTimeThreadTask");
    
    switch(inTask->GetTaskID())
    {
        case kNovaLINKTaskStopWorkerThread:
            DebugMsg("NovaLINK_TaskQueue::ProcessRealTimeThreadTask: Stopping");
            // Return that the thread should stop itself
            return true;
            
        case kNovaLINKTaskSwapClientShadowMaps:
            {
                DebugMsg("NovaLINK_TaskQueue::ProcessRealTimeThreadTask: Swapping the shadow maps in NovaLINK_ClientMap");
                NovaLINK_ClientMap* theClientMap = reinterpret_cast<NovaLINK_ClientMap*>(inTask->GetArg1());
                NovaLINK_ClientTasks::SwapInShadowMapsRT(theClientMap);
            }
            break;
            
        default:
            Assert(false, "NovaLINK_TaskQueue::ProcessRealTimeThreadTask: Unexpected task ID");
            break;
    }
    
    return false;
}

bool    NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask(NovaLINK_Task* inTask)
{
#if DEBUG  // This Assert macro always checks the condition, if for some reason the compiler doesn't optimise it away, even in release builds
    Assert(mNonRealTimeThread.IsCurrentThread(), "ProcessNonRealTimeThreadTask should only be called on the non-realtime worker thread.");
    Assert(mNonRealTimeThread.IsTimeShareThread(), "mNonRealTimeThread should not be in a time-constraint priority band.");
#endif
    
    switch(inTask->GetTaskID())
    {
        case kNovaLINKTaskStopWorkerThread:
            DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Stopping");
            // Return that the thread should stop itself
            return true;
            
        case kNovaLINKTaskStartClientIO:
            DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Processing kNovaLINKTaskStartClientIO");
            try
            {
                NovaLINK_Clients* theClients = reinterpret_cast<NovaLINK_Clients*>(inTask->GetArg1());
                bool didStartIO = NovaLINK_ClientTasks::StartIONonRT(theClients, static_cast<UInt32>(inTask->GetArg2()));
                inTask->SetReturnValue(didStartIO);
            }
            // TODO: Catch the other types of exceptions NovaLINK_ClientTasks::StartIONonRT can throw here as well. Set the task's return
            //       value (rather than rethrowing) so the exceptions can be handled if the task was queued sync. Then
            //       QueueSync_StartClientIO can throw some exception and NovaLINK_StartIO can return an appropriate error code to the
            //       HAL, instead of the driver just crashing.
            //
            //       Do the same for the kNovaLINKTaskStopClientIO case below. And should we set a return value in the catch block for
            //       NovaLINK_InvalidClientException as well, so it can also be rethrown in QueueSync_StartClientIO and then handled?
            catch(NovaLINK_InvalidClientException)
            {
                DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Ignoring NovaLINK_InvalidClientException thrown by StartIONonRT. %s",
                         "It's possible the client was removed before this task was processed.");
            }
            break;

        case kNovaLINKTaskStopClientIO:
            DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Processing kNovaLINKTaskStopClientIO");
            try
            {
                NovaLINK_Clients* theClients = reinterpret_cast<NovaLINK_Clients*>(inTask->GetArg1());
                bool didStopIO = NovaLINK_ClientTasks::StopIONonRT(theClients, static_cast<UInt32>(inTask->GetArg2()));
                inTask->SetReturnValue(didStopIO);
            }
            catch(NovaLINK_InvalidClientException)
            {
                DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Ignoring NovaLINK_InvalidClientException thrown by StopIONonRT. %s",
                         "It's possible the client was removed before this task was processed.");
            }
            break;

        case kNovaLINKTaskStartClientInputIO:
            DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Processing kNovaLINKTaskStartClientInputIO");
            try
            {
                NovaLINK_Clients* theClients = reinterpret_cast<NovaLINK_Clients*>(inTask->GetArg1());
                NovaLINK_ClientTasks::StartInputIONonRT(theClients, static_cast<UInt32>(inTask->GetArg2()));
            }
            catch(NovaLINK_InvalidClientException)
            {
                DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Ignoring NovaLINK_InvalidClientException thrown by StartInputIONonRT. %s",
                         "It's possible the client was removed before this task was processed.");
            }
            break;
            
        case kNovaLINKTaskSendPropertyNotification:
            DebugMsg("NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Processing kNovaLINKTaskSendPropertyNotification");
            {
                AudioObjectPropertyAddress thePropertyAddress[] = {
                    { static_cast<UInt32>(inTask->GetArg1()), kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster } };
                NovaLINK_PlugIn::Host_PropertiesChanged(static_cast<AudioObjectID>(inTask->GetArg2()), 1, thePropertyAddress);
            }
            break;
            
        default:
            Assert(false, "NovaLINK_TaskQueue::ProcessNonRealTimeThreadTask: Unexpected task ID");
            break;
    }
    
    return false;
}

#pragma clang assume_nonnull end

