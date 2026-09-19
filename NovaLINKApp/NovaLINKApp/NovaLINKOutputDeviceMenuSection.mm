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
//  NovaLINKOutputDeviceMenuSection.mm
//  NovaLINKApp
//
//  Copyright © 2016-2018 Kyle Neideck
//

// Self Include
#import "NovaLINKOutputDeviceMenuSection.h"

// Local Includes
#import "NovaLINK_Utils.h"
#import "NovaLINK_Types.h"
#import "NovaLINKAudioDevice.h"

// PublicUtility Includes
#import "CAAutoDisposer.h"
#import "CAHALAudioDevice.h"
#import "CAHALAudioSystemObject.h"
#import "CAPropertyAddress.h"

// STL Includes
#import <set>


#pragma clang assume_nonnull begin

static NSInteger const kOutputDeviceMenuItemTag = 5;
static int64_t const kPopulateDebounceNsec = 150 * NSEC_PER_MSEC;

@implementation NovaLINKOutputDeviceMenuSection {
    NSMenu* novaLINKMenu;
    NovaLINKAudioDeviceManager* audioDevices;
    NovaLINKPreferredOutputDevices* preferredDevices;
    NSMutableArray<NSMenuItem*>* outputDeviceMenuItems;
    // HAL queries and property listeners must not run on the main queue. Doing so freezes the
    // status-item menu after long uptime (Bluetooth / device-list spam) while System Settings
    // still works — coreaudiod is fine, AppKit is not.
    dispatch_queue_t halQueue;
    uint64_t populateGeneration;
    BOOL menuIsTracking;
    NSArray<NSDictionary*>* __nullable pendingItemInfos;
    // Called when a CoreAudio property has changed and we might need to update the menu. For
    // example, when a device is connected or disconnected.
    AudioObjectPropertyListenerBlock refreshNeededListener;
    // The devices we've added refreshNeededListener to. Used to avoid adding it to a device twice
    // for the same property and to remove it from all devices in dealloc.
    std::set<AudioObjectID> listenedDevices_kAudioDevicePropertyDataSources;
    std::set<AudioObjectID> listenedDevices_kAudioDevicePropertyDataSource;
}

- (instancetype) initWithNovaLINKMenu:(NSMenu*)inNovaLINKMenu
                    audioDevices:(NovaLINKAudioDeviceManager*)inAudioDevices
                preferredDevices:(NovaLINKPreferredOutputDevices*)inPreferredDevices {
    if ((self = [super init])) {
        novaLINKMenu = inNovaLINKMenu;
        audioDevices = inAudioDevices;
        preferredDevices = inPreferredDevices;
        outputDeviceMenuItems = [NSMutableArray new];
        populateGeneration = 0;
        menuIsTracking = NO;
        pendingItemInfos = nil;
        halQueue = dispatch_queue_create("life.thenurim.novalink.OutputDeviceMenu",
                                         DISPATCH_QUEUE_SERIAL);

        NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:self
               selector:@selector(menuDidBeginTracking:)
                   name:NSMenuDidBeginTrackingNotification
                 object:novaLINKMenu];
        [nc addObserver:self
               selector:@selector(menuDidEndTracking:)
                   name:NSMenuDidEndTrackingNotification
                 object:novaLINKMenu];

        [self listenForDevicesAddedOrRemoved];
        [self schedulePopulateNovaLINKMenuDebounced:NO];
    }
    
    return self;
}

- (void) dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];

    // Tell CoreAudio not to call the listener block anymore. This probably isn't necessary.
    //
    // I think it's safe to do this without dispatching to the main queue because dealloc and
    // refreshNeededListener should be essentially mutually exclusive. If refreshNeededListener is
    // invoked and gets a value for weakSelf, it holds the strong ref until it returns, so dealloc
    // won't be called. If refreshNeededListener is invoked and deallocation has started, it will
    // get nil for weakSelf and just return.
    auto removeListener = [&] (CAHALAudioObject audioObject, AudioObjectPropertySelector prop) {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            // Check the object still exists first to reduce unnecessary error logs.
            if (CAHALAudioObject::ObjectExists(audioObject.GetObjectID())) {
                audioObject.RemovePropertyListenerBlock(CAPropertyAddress(prop),
                                                        halQueue,
                                                        refreshNeededListener);
            }
        });
    };

    // Remove the listener from each audio object we added it to.
    removeListener(CAHALAudioSystemObject(), kAudioHardwarePropertyDevices);

    for (auto deviceID : listenedDevices_kAudioDevicePropertyDataSources) {
        removeListener(NovaLINKAudioDevice(deviceID), kAudioDevicePropertyDataSources);
    }

    for (auto deviceID : listenedDevices_kAudioDevicePropertyDataSource) {
        removeListener(NovaLINKAudioDevice(deviceID), kAudioDevicePropertyDataSource);
    }
}

- (void) listenForDevicesAddedOrRemoved {
    NovaLINKOutputDeviceMenuSection* __weak weakSelf = self;

    refreshNeededListener = ^(UInt32 inNumberAddresses,
                              const AudioObjectPropertyAddress* inAddresses) {
        #pragma unused (inNumberAddresses, inAddresses)
        // Return immediately — never call the HAL from inside a property listener.
        [weakSelf schedulePopulateNovaLINKMenuDebounced:YES];
    };

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        CAHALAudioSystemObject().AddPropertyListenerBlock(
            CAPropertyAddress(kAudioHardwarePropertyDevices),
            halQueue,
            refreshNeededListener);
    });
}

- (void) schedulePopulateNovaLINKMenuDebounced:(BOOL)debounced {
    NovaLINKOutputDeviceMenuSection* __weak weakSelf = self;
    dispatch_async(halQueue, ^{
        NovaLINKOutputDeviceMenuSection* strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }

        strongSelf->populateGeneration++;
        const uint64_t generation = strongSelf->populateGeneration;
        const int64_t delay = debounced ? kPopulateDebounceNsec : 0;

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay), strongSelf->halQueue, ^{
            NovaLINKOutputDeviceMenuSection* innerSelf = weakSelf;
            if (!innerSelf || generation != innerSelf->populateGeneration) {
                return;
            }
            NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
                [innerSelf collectAndApplyMenuItems];
            });
        });
    });
}

- (void) menuDidBeginTracking:(NSNotification*)notification {
    #pragma unused (notification)
    menuIsTracking = YES;
}

- (void) menuDidEndTracking:(NSNotification*)notification {
    #pragma unused (notification)
    menuIsTracking = NO;
    if (pendingItemInfos) {
        NSArray<NSDictionary*>* infos = pendingItemInfos;
        pendingItemInfos = nil;
        [self applyMenuItemInfos:infos];
    }
}

- (void) collectAndApplyMenuItems {
    NSMutableArray<NSDictionary*>* itemInfos = [NSMutableArray new];
    std::set<AudioObjectID> outputDeviceIDs;

    CAHALAudioSystemObject audioSystem;
    UInt32 numDevices = audioSystem.GetNumberAudioDevices();

    if (numDevices > 0) {
        CAAutoArrayDelete<AudioObjectID> devices(numDevices);
        audioSystem.GetAudioDevices(numDevices, devices);

        for (UInt32 i = 0; i < numDevices; i++) {
            NovaLINKAudioDevice device(devices[i]);
            BOOL canBeOutputDevice = YES;
            NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
                canBeOutputDevice = device.CanBeOutputDeviceInNovaLINKApp();
            });

            if (!canBeOutputDevice) {
                continue;
            }

            outputDeviceIDs.insert(device.GetObjectID());
            [itemInfos addObjectsFromArray:[self itemInfosForDevice:device]];
            [self listenForDataSourceChangesOnDevice:device];
        }
    }

    [self removeStaleDataSourceListeners:outputDeviceIDs];

    NSArray<NSDictionary*>* infos = [itemInfos copy];
    NovaLINKOutputDeviceMenuSection* __weak weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        NovaLINKOutputDeviceMenuSection* strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        if (strongSelf->menuIsTracking) {
            strongSelf->pendingItemInfos = infos;
            return;
        }
        [strongSelf applyMenuItemInfos:infos];
    });
}

- (void) listenForDataSourceChangesOnDevice:(NovaLINKAudioDevice)device {
    const AudioObjectID deviceID = device.GetObjectID();

    if (listenedDevices_kAudioDevicePropertyDataSources.count(deviceID) == 0) {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            device.AddPropertyListenerBlock(CAPropertyAddress(kAudioDevicePropertyDataSources,
                                                              kAudioDevicePropertyScopeOutput),
                                            halQueue,
                                            refreshNeededListener);
            listenedDevices_kAudioDevicePropertyDataSources.insert(deviceID);
        });
    }

    if (listenedDevices_kAudioDevicePropertyDataSource.count(deviceID) == 0) {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            device.AddPropertyListenerBlock(CAPropertyAddress(kAudioDevicePropertyDataSource,
                                                              kAudioDevicePropertyScopeOutput),
                                            halQueue,
                                            refreshNeededListener);
            listenedDevices_kAudioDevicePropertyDataSource.insert(deviceID);
        });
    }
}

- (void) removeStaleDataSourceListeners:(const std::set<AudioObjectID>&)currentOutputDeviceIDs {
    auto removeIfStale = [&] (std::set<AudioObjectID>& listened,
                              AudioObjectPropertySelector prop) {
        for (auto it = listened.begin(); it != listened.end(); ) {
            if (currentOutputDeviceIDs.count(*it) == 0) {
                NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
                    if (CAHALAudioObject::ObjectExists(*it)) {
                        NovaLINKAudioDevice(*it).RemovePropertyListenerBlock(
                            CAPropertyAddress(prop, kAudioDevicePropertyScopeOutput),
                            halQueue,
                            refreshNeededListener);
                    }
                });
                it = listened.erase(it);
            } else {
                ++it;
            }
        }
    };

    removeIfStale(listenedDevices_kAudioDevicePropertyDataSources,
                  kAudioDevicePropertyDataSources);
    removeIfStale(listenedDevices_kAudioDevicePropertyDataSource,
                  kAudioDevicePropertyDataSource);
}

- (void) applyMenuItemInfos:(NSArray<NSDictionary*>*)itemInfos {
    NovaLINKAssert([NSThread isMainThread],
              "NovaLINKOutputDeviceMenuSection::applyMenuItemInfos called on non-main thread");

    for (NSMenuItem* item in outputDeviceMenuItems) {
        DebugMsg("NovaLINKOutputDeviceMenuSection::applyMenuItemInfos: Removing %s",
                 item.description.UTF8String);
        [novaLINKMenu removeItem:item];
    }

    [outputDeviceMenuItems removeAllObjects];

    const NSInteger menuItemsIdx = [novaLINKMenu indexOfItemWithTag:kOutputDeviceMenuItemTag] + 1;
    for (NSDictionary* info in itemInfos) {
        NSMenuItem* item = [self menuItemFromInfo:info];
        DebugMsg("NovaLINKOutputDeviceMenuSection::applyMenuItemInfos: Inserting %s",
                 item.description.UTF8String);
        [novaLINKMenu insertItem:item atIndex:menuItemsIdx];
        [outputDeviceMenuItems addObject:item];
    }
}

- (NSArray<NSDictionary*>*) itemInfosForDevice:(CAHALAudioDevice)device {
    NSMutableArray<NSDictionary*>* items = [NSMutableArray new];

    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeOutput;
    UInt32 channel = kAudioObjectPropertyElementMaster;

    UInt32 numDataSources = 0;
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        if (device.HasDataSourceControl(scope, channel) &&
                device.DataSourceControlIsSettable(scope, channel)) {
            numDataSources = device.GetNumberAvailableDataSources(scope, channel);
        }
    });

    BOOL isAirPlay = NO;
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        isAirPlay = (device.GetTransportType() == kAudioDeviceTransportTypeAirPlay);
    });

    if (numDataSources > 0) {
        CAAutoArrayDelete<UInt32> dataSourceIDs(numDataSources);
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            device.GetAvailableDataSources(scope, channel, numDataSources, dataSourceIDs);
        });

        for (UInt32 i = 0; i < numDataSources; i++) {
            DebugMsg("NovaLINKOutputDeviceMenuSection::itemInfosForDevice: "
                     "Creating item. %s%u %s%u",
                     "Device ID:", device.GetObjectID(),
                     ", Data source ID:", dataSourceIDs[i]);

            NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, "(DS)", [&] {
                NSString* dataSourceName =
                    CFBridgingRelease(device.CopyDataSourceNameForID(scope, channel, dataSourceIDs[i]));
                NSString* deviceName = CFBridgingRelease(device.CopyName());
                [items addObject:[self itemInfoForDeviceID:device.GetObjectID()
                                              dataSourceID:@(dataSourceIDs[i])
                                                     title:dataSourceName
                                                   toolTip:deviceName
                                                   airPlay:isAirPlay]];
            });
        }
    } else {
        DebugMsg("NovaLINKOutputDeviceMenuSection::itemInfosForDevice: Creating item. %s%u",
                 "Device ID:", device.GetObjectID());

        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            [items addObject:[self itemInfoForDeviceID:device.GetObjectID()
                                          dataSourceID:nil
                                                 title:CFBridgingRelease(device.CopyName())
                                               toolTip:nil
                                               airPlay:isAirPlay]];
        });
    }

    return items;
}

- (NSDictionary*) itemInfoForDeviceID:(AudioDeviceID)deviceID
                         dataSourceID:(NSNumber* __nullable)dataSourceID
                                title:(NSString* __nullable)title
                              toolTip:(NSString* __nullable)toolTip
                              airPlay:(BOOL)airPlay {
    if (!title) {
        title = (toolTip ? toolTip : @"");
    }

    BOOL isSelected =
        [audioDevices isOutputDevice:deviceID] &&
            (!dataSourceID || [audioDevices isOutputDataSource:[dataSourceID unsignedIntValue]]);

    return @{
        @"deviceID": @(deviceID),
        @"dataSourceID": dataSourceID ? NovaLINKNN(dataSourceID) : [NSNull null],
        @"title": NovaLINKNN(title),
        @"toolTip": toolTip ? NovaLINKNN(toolTip) : [NSNull null],
        @"airPlay": @(airPlay),
        @"selected": @(isSelected)
    };
}

- (NSMenuItem*) menuItemFromInfo:(NSDictionary*)info {
    NSString* title = info[@"title"];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:NovaLINKNN(title)
                                                  action:@selector(outputDeviceMenuItemSelected:)
                                           keyEquivalent:@""];

    if ([info[@"airPlay"] boolValue]) {
        item.image = [NSImage imageNamed:@"AirPlayIcon"];
        [item.image setTemplate:YES];
    }

    item.state = [info[@"selected"] boolValue] ? NSOnState : NSOffState;
    id toolTip = info[@"toolTip"];
    item.toolTip = (toolTip == [NSNull null]) ? nil : toolTip;
    item.target = self;
    item.indentationLevel = 1;
    item.representedObject = @{ @"deviceID": NovaLINKNN(info[@"deviceID"]),
                                @"dataSourceID": NovaLINKNN(info[@"dataSourceID"]) };

#if __clang_major__ >= 9
    if (@available(macOS 10.10, *)) {
        item.accessibilityIdentifier = @"output-device";
    }
#endif

    return item;
}

- (void) outputDeviceDidChange {
    [self schedulePopulateNovaLINKMenuDebounced:NO];
}

- (void) outputDeviceMenuItemSelected:(NSMenuItem*)menuItem {
    DebugMsg("NovaLINKOutputDeviceMenuSection::outputDeviceMenuItemSelected: '%s' menu item selected",
             [menuItem.title UTF8String]);
    
    // Make sure the menu item is actually for an output device.
    if (![outputDeviceMenuItems containsObject:menuItem]) {
        return;
    }
    
    // Change to the new output device.
    AudioDeviceID newDeviceID = [[menuItem representedObject][@"deviceID"] unsignedIntValue];
    id newDataSourceID = [menuItem representedObject][@"dataSourceID"];
    
    BOOL changingDevice = ![audioDevices isOutputDevice:newDeviceID];
    BOOL changingDataSource =
        (newDataSourceID != [NSNull null]) &&
            ![audioDevices isOutputDataSource:[newDataSourceID unsignedIntValue]];

    if (changingDevice || changingDataSource) {
        NSString* deviceName =
            menuItem.toolTip ?
                [NSString stringWithFormat:@"%@ (%@)", menuItem.title, menuItem.toolTip] :
                menuItem.title;

        if (changingDevice) {
            // Add the new output device to the list of preferred devices.
            [preferredDevices userChangedOutputDeviceTo:newDeviceID];
        }

        // Dispatched because it usually blocks. (Note that we're using
        // DISPATCH_QUEUE_PRIORITY_HIGH, which is the second highest priority.)
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
            [self changeToOutputDevice:newDeviceID
                         newDataSource:newDataSourceID
                            deviceName:deviceName];
        });
    }
}

- (void) changeToOutputDevice:(AudioDeviceID)deviceID
                newDataSource:(id)dataSourceID
                   deviceName:(NSString*)deviceName {
    NSError* __nullable error;
    
    if (dataSourceID == [NSNull null]) {
        error = [audioDevices setOutputDeviceWithID:deviceID revertOnFailure:YES];
    } else {
        error = [audioDevices setOutputDeviceWithID:deviceID
                                       dataSourceID:[dataSourceID unsignedIntValue]
                                    revertOnFailure:YES];
    }
    
    if (error) {
        // Couldn't change the output device, so show a warning. (No need to change the menu
        // selection back because it gets repopulated every time it's opened.)
        
        // NSAlerts should only be shown on the main thread.
        dispatch_async(dispatch_get_main_queue(), ^{
            NSLog(@"Failed to set output device: %@", deviceName);
            
            NSAlert* alert = [NSAlert new];
            
            alert.messageText =
                [NSString stringWithFormat:@"Failed to set %@ as the output device.", deviceName];
            alert.informativeText = @"This is probably a bug. Feel free to report it.";
            
            [alert runModal];
        });
    }
}

@end

#pragma clang assume_nonnull end

