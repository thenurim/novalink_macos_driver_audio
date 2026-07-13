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
//  NovaLINKAboutPanel.m
//  NovaLINKApp
//
//  Copyright © 2016, 2024 Kyle Neideck
//

// Self Include
#import "NovaLINKAboutPanel.h"

// Local Includes
#import "NovaLINK_Types.h"

// PublicUtility Includes
#include "CADebugMacros.h"


NS_ASSUME_NONNULL_BEGIN

static NSInteger const kVersionLabelTag = 1;
static NSInteger const kCopyrightLabelTag = 2;
static NSInteger const kProjectWebsiteLabelTag = 3;
static NSInteger const kContributorsLabelTag = 4;

@implementation NovaLINKAboutPanel {
    NSPanel* aboutPanel;
    
    NSTextField* versionLabel;
    NSTextField* copyrightLabel;
    NSTextField* websiteLabel;
    NSTextField* contributorsLabel;
    
    NSTextView* licenseView;
}

- (instancetype)initWithPanel:(NSPanel*)inAboutPanel licenseView:(NSTextView*)inLicenseView {
    if ((self = [super init])) {
        aboutPanel = inAboutPanel;
        
        versionLabel = [[aboutPanel contentView] viewWithTag:kVersionLabelTag];
        copyrightLabel = [[aboutPanel contentView] viewWithTag:kCopyrightLabelTag];
        websiteLabel = [[aboutPanel contentView] viewWithTag:kProjectWebsiteLabelTag];
        contributorsLabel = [[aboutPanel contentView] viewWithTag:kContributorsLabelTag];
        
        licenseView = inLicenseView;
        
        [self initAboutPanel];
    }
    
    return self;
}

- (void) initAboutPanel {
    // Set up the About NovaLINK window
    
    NSBundle* bundle = [NSBundle mainBundle];
    
    if (bundle == nil) {
        NSLog(@"NovaLINK: NovaLINKAboutPanel::initAboutPanel: Could not find main bundle");
    } else {
        // Version number label
        NSString* __nullable version =
            [[bundle infoDictionary] objectForKey:@"CFBundleShortVersionString"];
        
        if (version) {
            versionLabel.stringValue = [NSString stringWithFormat:@"Version %@", version];
        }
        
        // Copyright notice label
        NSString* __nullable copyrightNotice =
            [[bundle infoDictionary] objectForKey:@"NSHumanReadableCopyright"];
        
        if (copyrightNotice) {
            copyrightLabel.alignment = NSTextAlignmentCenter;
            copyrightLabel.stringValue = (NSString*)copyrightNotice;
        }
        
        // Project website link label
        websiteLabel.selectable = YES;
        websiteLabel.allowsEditingTextAttributes = YES;
        websiteLabel.alignment = NSTextAlignmentCenter;
        
        NSString* projectURL = [NSString stringWithUTF8String:kNovaLINKProjectURL];
        NSFont* linkFont = [NSFont labelFontOfSize:11.0];
        NSMutableParagraphStyle* centered = [[NSMutableParagraphStyle alloc] init];
        centered.alignment = NSTextAlignmentCenter;
        websiteLabel.attributedStringValue =
            [[NSAttributedString alloc] initWithString:projectURL
                                            attributes:@{ NSLinkAttributeName: projectURL,
                                                          NSFontAttributeName: linkFont,
                                                          NSParagraphStyleAttributeName: centered }];
        [websiteLabel sizeToFit];
        // Keep the link horizontally centered in the left column (separator is at x≈383).
        CGFloat leftColumnWidth = 383.0;
        NSRect websiteFrame = websiteLabel.frame;
        websiteFrame.origin.x = MAX(8.0, (leftColumnWidth - websiteFrame.size.width) / 2.0);
        websiteFrame.size.width = MIN(websiteFrame.size.width + 4.0, leftColumnWidth - 16.0);
        websiteLabel.frame = websiteFrame;
        
        // Contributors link is unused in the current layout.
        contributorsLabel.hidden = YES;
        
        // Load the text of the license into the text view
        NSString* __nullable licensePath = [bundle pathForResource:@"LICENSE" ofType:nil];
        
        NSError* err;
        NSString* __nullable licenseStr = (!licensePath ? nil :
            [NSString stringWithContentsOfFile:(NSString*)licensePath
                                      encoding:NSASCIIStringEncoding
                                         error:&err]);
        
        if (err || !licenseStr || [licenseStr isEqualToString:@""]) {
            NSLog(@"Error loading license file: %@", err);
            licenseStr = @"Error: could not open license file.";
        }
        
        licenseView.string = (NSString*)licenseStr;
        
        NSFont* __nullable font = [NSFont fontWithName:@"Andale Mono" size:0.0];
        if (font) {
            licenseView.textStorage.font = font;
        }
    }
}

- (void) show {
    DebugMsg("NovaLINKAboutPanel::showAboutPanel: Opening \"About NovaLINK\" panel");
    
    // We have to make aboutPanel visible before calling [NSApp activateIgnoringOtherApps:YES]
    // or the app won't be activated the first time (not sure why it only happens the first
    // time) and aboutPanel won't open. WindowServer logs this explanation:
    // 0[SetFrontProcessWithInfo]: CPS: Rejecting the request for pid 1234 due to the activation count being 0; launch ts=19302059379458, current time=19314267188375, window count = 0.
    [aboutPanel setIsVisible:YES];
    [aboutPanel makeKeyAndOrderFront:self];
    
    // On macOS 14.4, aboutPanel needs "Release When Closed" unchecked in MainMenu.xib. Otherwise,
    // aboutPanel will never open again if you click the close button.
    
    // This is deprecated for NSApplication.activate, but that stops aboutPanel from ever being shown.
    [NSApp activateIgnoringOtherApps:YES];
    
    DebugMsg("NovaLINKAboutPanel::showAboutPanel: Finished opening panel. "
             "aboutPanel.isVisible %d, aboutPanel.isKeyWindow %d, NSApp.isActive %d",
             aboutPanel.isVisible,
             aboutPanel.isKeyWindow,
             NSApp.isActive);
}

@end

@implementation NovaLINKLinkField

- (void) resetCursorRects {
    // Change the mouse cursor when hovering over the link. (It does change by default, but only after
    // you've clicked it once.)
    [self addCursorRect:self.bounds cursor:[NSCursor pointingHandCursor]];
}

@end

NS_ASSUME_NONNULL_END

