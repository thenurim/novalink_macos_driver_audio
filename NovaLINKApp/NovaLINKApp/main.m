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

// Prevents the classic race where several processes pass a "already running?" check before any
 // of them appear in NSWorkspace — that race was stacking TCC microphone dialogs.
static int AcquireSingleInstanceLock(void) {
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

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        NSLog(@"NovaLINK Audio Passthrough already running. Exiting.");
        close(fd);
        return -2;
    }

    // Keep fd open for process lifetime so the lock is held until exit.
    return fd;
}

int main(int argc, const char * argv[]) {
    int lockFd = AcquireSingleInstanceLock();
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
