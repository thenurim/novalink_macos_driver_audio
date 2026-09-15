// This file is part of NovaLINK / NovaLINK.
//
//  main.m
//  NovaLINKApp
//

#import <Cocoa/Cocoa.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// Must match NovaLINKAppDelegate.mm
static NSString* const kNovaLINKShowCompanionUINotification =
    @"life.thenurim.novalink.ShowCompanionUI";
// Ask the --agent process to exit cleanly so this GUI process can take the lock.
// KeepAlive SuccessfulExit=false means launchd will not respawn after a clean exit.
static NSString* const kNovaLINKQuitAgentForCompanionUINotification =
    @"life.thenurim.novalink.QuitAgentForCompanionUI";

// Prevents the classic race where several processes pass a "already running?" check before any
// of them appear in NSWorkspace — that race was stacking TCC microphone dialogs.
static int AcquireSingleInstanceLock(BOOL isAgentLaunch) {
    // Stable path under the user's home so different launch methods share one lock.
    NSString* lockPath =
        [NSHomeDirectory() stringByAppendingPathComponent:
            @"Library/Application Support/NovaLINK/passthrough-companion.lock"];

    [[NSFileManager defaultManager]
        createDirectoryAtPath:[lockPath stringByDeletingLastPathComponent]
  withIntermediateDirectories:YES
                   attributes:nil
                        error:nil];

    int fd = open(lockPath.fileSystemRepresentation, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        NSLog(@"NovaLINK Audio Passthrough: failed to open lock file (%s). Continuing.",
              strerror(errno));
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        // Keep fd open for process lifetime so the lock is held until exit.
        return fd;
    }

    if (isAgentLaunch) {
        NSLog(@"NovaLINK Audio Passthrough already running. Agent exiting.");
        close(fd);
        return -2;
    }

    // Background --agent holds the lock. In-process NSStatusItem promotion from a long-running
    // LaunchAgent is unreliable (menu bar icon often never appears). Prefer a clean handoff:
    // agent exits (no KeepAlive respawn on success), then this process takes the lock as UI.
    NSLog(@"NovaLINK Audio Passthrough already running (agent). Requesting agent handoff for UI.");
    NSDistributedNotificationCenter* center = [NSDistributedNotificationCenter defaultCenter];
    for (int i = 0; i < 3; i++) {
        [center postNotificationName:kNovaLINKQuitAgentForCompanionUINotification
                              object:nil
                            userInfo:nil
                  deliverImmediately:YES];
        usleep(50 * 1000);
    }

    // Wait for the agent to release the lock (NSApplicationMain unwind + flock unlock).
    for (int attempt = 0; attempt < 50; attempt++) {
        usleep(100 * 1000);
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            NSLog(@"NovaLINK Audio Passthrough: acquired lock after agent handoff.");
            return fd;
        }
    }

    // Fallback: ask the still-running agent to promote itself (best-effort).
    NSLog(@"NovaLINK Audio Passthrough: agent handoff timed out. Requesting in-process UI promote.");
    for (int i = 0; i < 5; i++) {
        [center postNotificationName:kNovaLINKShowCompanionUINotification
                              object:nil
                            userInfo:nil
                  deliverImmediately:YES];
        usleep(100 * 1000);
    }
    close(fd);
    return -2;
}

static BOOL ArgumentsContainAgentFlag(int argc, const char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--agent") == 0) {
            return YES;
        }
    }
    return NO;
}

int main(int argc, const char * argv[]) {
    BOOL isAgent = ArgumentsContainAgentFlag(argc, argv);
    int lockFd = AcquireSingleInstanceLock(isAgent);
    if (lockFd == -2) {
        return 0;
    }

    int result = NSApplicationMain(argc, argv);

    if (lockFd >= 0) {
        flock(lockFd, LOCK_UN);
        close(lockFd);
    }
    return result;
}
