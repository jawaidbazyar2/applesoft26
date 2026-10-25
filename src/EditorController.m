#import "EditorController.h"

#include <stdlib.h>
#include <string.h>

#import "RunWindow.h"
#import "as_common.h"
#import "as_machine.h"
#import "as_tokenize.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface AppController () <RunViewKeyDelegate>
@end

@interface HistoryLineField : NSTextField
@end

@implementation HistoryLineField
- (NSView *)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}
@end

static BOOL status_line_number(NSString *s, int *out) {
    NSRange mark;
    NSUInteger i;
    int n = 0;
    if (!s || !out) return NO;
    mark = [s rangeOfString:@" IN " options:NSBackwardsSearch];
    if (mark.location == NSNotFound) return NO;
    i = NSMaxRange(mark);
    if (i >= s.length) return NO;
    while (i < s.length) {
        unichar c = [s characterAtIndex:i];
        if (c < '0' || c > '9') return NO;
        n = n * 10 + (int)(c - '0');
        if (n > 63999) return NO;
        i++;
    }
    *out = n;
    return YES;
}

@interface StatusField : NSTextField
@property (nonatomic) BOOL jumpsToLine;
@end

@implementation StatusField
- (void)setStringValue:(NSString *)stringValue {
    int line = 0;
    [super setStringValue:stringValue ?: @""];
    self.jumpsToLine = status_line_number(self.stringValue, &line);
    self.textColor = self.jumpsToLine ? NSColor.systemBlueColor : NSColor.blackColor;
    self.toolTip = self.jumpsToLine ? @"Show this line" : nil;
    [self.window invalidateCursorRectsForView:self];
}

- (void)resetCursorRects {
    [super resetCursorRects];
    if (self.jumpsToLine)
        [self addCursorRect:self.bounds cursor:[NSCursor pointingHandCursor]];
}

- (void)mouseDown:(NSEvent *)event {
    if (!self.jumpsToLine) {
        [super mouseDown:event];
        return;
    }
    [self sendAction:self.action to:self.target];
    (void)event;
}
@end

@interface SrcField : NSTextField
@property (nonatomic, copy) NSArray<NSValue *> *linkRanges;
- (NSUInteger)characterIndexForPoint:(NSPoint)point;
- (NSRect)rectForCharacterRange:(NSRange)range;
@end

@implementation SrcField

- (void)layoutLine:(NSTextStorage **)storageOut manager:(NSLayoutManager **)managerOut container:(NSTextContainer **)containerOut {
    NSTextStorage *storage = [[NSTextStorage alloc] initWithAttributedString:self.attributedStringValue];
    NSLayoutManager *lm = [NSLayoutManager new];
    NSTextContainer *tc = [[NSTextContainer alloc] initWithContainerSize:self.bounds.size];
    tc.lineFragmentPadding = 2;
    [lm addTextContainer:tc];
    [storage addLayoutManager:lm];
    [lm ensureLayoutForTextContainer:tc];
    *storageOut = storage;
    *managerOut = lm;
    *containerOut = tc;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    NSTextView *tv = (NSTextView *)self.currentEditor;
    NSLayoutManager *lm;
    NSTextContainer *tc;
    NSTextStorage *storage = nil;
    NSPoint local = point;
    NSUInteger glyph;
    NSRect glyphRect;
    if (tv) {
        if (tv.string.length == 0) return NSNotFound;
        local = [tv convertPoint:point fromView:self];
        local.x -= tv.textContainerOrigin.x;
        local.y -= tv.textContainerOrigin.y;
        lm = tv.layoutManager;
        tc = tv.textContainer;
    } else {
        if (self.attributedStringValue.length == 0) return NSNotFound;
        [self layoutLine:&storage manager:&lm container:&tc];
    }
    if (local.x < 0 || local.y < 0) return NSNotFound;
    [lm ensureLayoutForTextContainer:tc];
    if (lm.numberOfGlyphs == 0) return NSNotFound;
    glyph = [lm glyphIndexForPoint:local inTextContainer:tc fractionOfDistanceThroughGlyph:NULL];
    if (glyph >= lm.numberOfGlyphs) return NSNotFound;
    glyphRect = [lm boundingRectForGlyphRange:NSMakeRange(glyph, 1) inTextContainer:tc];
    if (!NSPointInRect(local, glyphRect)) return NSNotFound;
    return [lm characterIndexForGlyphAtIndex:glyph];
}

- (NSRect)rectForCharacterRange:(NSRange)range {
    NSTextView *tv = (NSTextView *)self.currentEditor;
    NSLayoutManager *lm;
    NSTextContainer *tc;
    NSTextStorage *storage = nil;
    NSRange glyphs;
    NSRect rect;
    if (range.location == NSNotFound || range.length == 0) return NSZeroRect;
    if (tv) {
        if (NSMaxRange(range) > tv.string.length) return NSZeroRect;
        lm = tv.layoutManager;
        tc = tv.textContainer;
        [lm ensureLayoutForTextContainer:tc];
        glyphs = [lm glyphRangeForCharacterRange:range actualCharacterRange:NULL];
        if (glyphs.length == 0) return NSZeroRect;
        rect = [lm boundingRectForGlyphRange:glyphs inTextContainer:tc];
        rect.origin.x += tv.textContainerOrigin.x;
        rect.origin.y += tv.textContainerOrigin.y;
        return [self convertRect:rect fromView:tv];
    }
    if (NSMaxRange(range) > self.attributedStringValue.length) return NSZeroRect;
    [self layoutLine:&storage manager:&lm container:&tc];
    glyphs = [lm glyphRangeForCharacterRange:range actualCharacterRange:NULL];
    if (glyphs.length == 0) return NSZeroRect;
    return [lm boundingRectForGlyphRange:glyphs inTextContainer:tc];
}

- (void)resetCursorRects {
    [super resetCursorRects];
    for (NSValue *value in _linkRanges) {
        NSRect rect = [self rectForCharacterRange:value.rangeValue];
        if (!NSIsEmptyRect(rect)) [self addCursorRect:rect cursor:[NSCursor pointingHandCursor]];
    }
}

@end

@implementation AppController {
    AsProgram *_program;
    AsScreen *_screen;
    AsMachine *_machine;
    NSWindow *_window;
    NSScrollView *_editorScroll;
    NSTableView *_table;
    NSScrollView *_historyScroll;
    NSTableView *_historyTable;
    NSMutableArray<NSString *> *_history;
    NSScrollView *_traceScroll;
    NSTableView *_traceTable;
    NSMutableArray<NSString *> *_traceLines;
    NSMutableArray<NSNumber *> *_traceNumbers;
    NSTextField *_status;
    NSTextField *_immediate;
    NSButton *_runButton;
    NSButton *_pauseButton;
    NSButton *_stopButton;
    NSButton *_stepButton;
    NSTimer *_timer;
    NSFont *_font;
    NSFont *_remarkFont;
    NSColor *_keywordColor;
    NSColor *_variableColor;
    NSURL *_fileURL;
    RunWindow *_runWindow;
    NSUInteger _tick;
    uint16_t _shownLine;
    BOOL _wasRunning;
    BOOL _reloading;
    BOOL _committing;
    BOOL _reloadQueued;
    BOOL _focusPhantom;
    BOOL _showingSyntax;
    BOOL _highlighting;
    BOOL _abandonEdit;
    BOOL _focusRowValid;
    NSInteger _focusRow;
    BOOL _historyOpen;
    BOOL _historyExpanded;
    NSInteger _stripTab;
    NSView *_historyBar;
    NSButton *_historyDisclosure;
    NSButton *_historyTab;
    NSButton *_traceTab;
    id _jumpMonitor;
    BOOL _jumpsShown;
    BOOL _pendingJump;
    uint16_t _pendingJumpNumber;
}

static const CGFloat kHistoryRowHeight = 20;
static const CGFloat kHistoryVisibleRows = 6;
static const CGFloat kHistoryBarHeight = 22;
static const CGFloat kHistoryY = 72;
static const NSInteger kStripHistory = 0;
static const NSInteger kStripTrace = 1;

- (void)applicationWillFinishLaunching:(NSNotification *)note {
    (void)note;
    _program = as_program_new();
    _screen = as_screen_new();
    _machine = as_machine_new(_program, _screen);
    _font = [NSFont fontWithName:@"Menlo" size:13] ?: [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
    _remarkFont = [[NSFontManager sharedFontManager] convertFont:_font toHaveTrait:NSItalicFontMask];
    _keywordColor = [NSColor colorWithCalibratedRed:0.08 green:0.22 blue:0.62 alpha:1];
    _variableColor = [NSColor colorWithCalibratedRed:0.55 green:0.12 blue:0.38 alpha:1];
    _history = [NSMutableArray new];
    _traceLines = [NSMutableArray new];
    _traceNumbers = [NSMutableArray new];
    [self buildMenus];
    [self buildWindow];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    __weak AppController *weakSelf = self;
    (void)note;
    _window.acceptsMouseMovedEvents = YES;
    _jumpMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskMouseMoved | NSEventMaskFlagsChanged |
                                                                  NSEventMaskLeftMouseDown | NSEventMaskMouseExited)
                                                         handler:^NSEvent *(NSEvent *event) {
                                                             AppController *controller = weakSelf;
                                                             if (!controller) return event;
                                                             return [controller handleJumpEvent:event];
                                                         }];
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                              target:self
                                            selector:@selector(tick:)
                                            userInfo:nil
                                             repeats:YES];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)applicationWillTerminate:(NSNotification *)note {
    (void)note;
    if (_jumpMonitor) {
        [NSEvent removeMonitor:_jumpMonitor];
        _jumpMonitor = nil;
    }
    [_timer invalidate];
    _timer = nil;
    _table.dataSource = nil;
    _table.delegate = nil;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    return [self confirmDirty] ? NSTerminateNow : NSTerminateCancel;
}

- (void)buildMenus {
    NSMenu *menubar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new];
    NSMenuItem *fileItem = [NSMenuItem new];
    NSMenuItem *editItem = [NSMenuItem new];
    NSMenuItem *progItem = [NSMenuItem new];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"AppleSoft"];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    NSMenu *progMenu = [[NSMenu alloc] initWithTitle:@"Program"];
    NSMenuItem *item;

    [appMenu addItemWithTitle:@"Quit AppleSoft" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [menubar addItem:appItem];

    item = [[NSMenuItem alloc] initWithTitle:@"New" action:@selector(newDocument:) keyEquivalent:@"n"];
    item.target = self;
    [fileMenu addItem:item];
    item = [[NSMenuItem alloc] initWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
    item.target = self;
    [fileMenu addItem:item];
    item = [[NSMenuItem alloc] initWithTitle:@"Close" action:@selector(closeDocument:) keyEquivalent:@"w"];
    item.target = self;
    [fileMenu addItem:item];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    item = [[NSMenuItem alloc] initWithTitle:@"Save" action:@selector(saveDocument:) keyEquivalent:@"s"];
    item.target = self;
    [fileMenu addItem:item];
    item = [[NSMenuItem alloc] initWithTitle:@"Save As…" action:@selector(saveDocumentAs:) keyEquivalent:@"S"];
    item.target = self;
    [fileMenu addItem:item];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    item = [[NSMenuItem alloc] initWithTitle:@"Renumber" action:@selector(renumber:) keyEquivalent:@""];
    item.target = self;
    [fileMenu addItem:item];
    fileItem.submenu = fileMenu;
    [menubar addItem:fileItem];

    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    {
        unichar backspace = NSBackspaceCharacter;
        item = [[NSMenuItem alloc] initWithTitle:@"Delete line"
                                          action:@selector(deleteLine:)
                                   keyEquivalent:[NSString stringWithCharacters:&backspace length:1]];
        item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        item.target = self;
        [editMenu addItem:item];
    }
    editItem.submenu = editMenu;
    [menubar addItem:editItem];

    item = [[NSMenuItem alloc] initWithTitle:@"Run" action:@selector(runProgram:) keyEquivalent:@"r"];
    item.target = self;
    [progMenu addItem:item];
    item = [[NSMenuItem alloc] initWithTitle:@"Pause" action:@selector(pauseProgram:) keyEquivalent:@""];
    item.target = self;
    [progMenu addItem:item];
    item = [[NSMenuItem alloc] initWithTitle:@"Stop" action:@selector(stopProgram:) keyEquivalent:@"."];
    item.target = self;
    [progMenu addItem:item];
    item = [[NSMenuItem alloc] initWithTitle:@"Step" action:@selector(stepProgram:) keyEquivalent:@""];
    item.target = self;
    [progMenu addItem:item];
    progItem.submenu = progMenu;
    [menubar addItem:progItem];

    NSApp.mainMenu = menubar;
}

- (NSButton *)toolButton:(NSString *)title action:(SEL)action x:(CGFloat)x {
    NSButton *b = [NSButton buttonWithTitle:title target:self action:action];
    b.frame = NSMakeRect(x, 10, 72, 26);
    b.translatesAutoresizingMaskIntoConstraints = YES;
    b.autoresizingMask = NSViewMaxYMargin;
    b.bezelStyle = NSBezelStyleRounded;
    return b;
}

- (void)buildWindow {
    NSRect frame = NSMakeRect(60, 80, 760, 640);
    NSScrollView *scroll;
    NSTableColumn *brk, *arrow, *num, *src;
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.delegate = self;
    _window.releasedWhenClosed = NO;
    _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    _window.contentMinSize = NSMakeSize(520, 400);
    _window.title = @"Untitled";

    scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 76, 760, 564)];
    scroll.autoresizingMask = NSViewNotSizable;
    _editorScroll = scroll;
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.drawsBackground = YES;
    scroll.backgroundColor = NSColor.whiteColor;

    _table = [[NSTableView alloc] initWithFrame:scroll.bounds];
    _table.backgroundColor = NSColor.whiteColor;
    _table.headerView = [NSTableHeaderView new];
    _table.rowHeight = 24;
    _table.usesAlternatingRowBackgroundColors = NO;
    _table.gridStyleMask = NSTableViewSolidHorizontalGridLineMask;
    _table.selectionHighlightStyle = NSTableViewSelectionHighlightStyleNone;
    _table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    if (@available(macOS 11.0, *)) _table.style = NSTableViewStylePlain;
    _table.dataSource = self;
    _table.delegate = self;

    brk = [[NSTableColumn alloc] initWithIdentifier:@"brk"];
    brk.title = @"";
    brk.width = 36;
    brk.minWidth = 36;
    brk.maxWidth = 36;
    brk.resizingMask = NSTableColumnNoResizing;
    arrow = [[NSTableColumn alloc] initWithIdentifier:@"arrow"];
    arrow.title = @"";
    arrow.width = 20;
    arrow.minWidth = 20;
    arrow.maxWidth = 20;
    arrow.resizingMask = NSTableColumnNoResizing;
    num = [[NSTableColumn alloc] initWithIdentifier:@"num"];
    num.title = @"Line";
    num.width = 72;
    num.minWidth = 64;
    num.resizingMask = NSTableColumnNoResizing;
    src = [[NSTableColumn alloc] initWithIdentifier:@"src"];
    src.title = @"Source";
    src.width = 620;
    src.minWidth = 200;
    src.resizingMask = NSTableColumnAutoresizingMask;
    [_table addTableColumn:brk];
    [_table addTableColumn:arrow];
    [_table addTableColumn:num];
    [_table addTableColumn:src];
    scroll.documentView = _table;
    [_window.contentView addSubview:scroll];

    _historyScroll = [self makeStripScroll];
    _historyTable = [self makeStripTableInScroll:_historyScroll click:@selector(historyClicked:)];
    {
        NSTableColumn *pad = [[NSTableColumn alloc] initWithIdentifier:@"pad"];
        NSTableColumn *cmd = [[NSTableColumn alloc] initWithIdentifier:@"cmd"];
        pad.title = @"";
        pad.width = 127;
        pad.minWidth = 127;
        pad.maxWidth = 127;
        pad.resizingMask = NSTableColumnNoResizing;
        cmd.title = @"";
        cmd.width = 640;
        cmd.minWidth = 40;
        cmd.resizingMask = NSTableColumnAutoresizingMask;
        [_historyTable addTableColumn:pad];
        [_historyTable addTableColumn:cmd];
    }
    [_window.contentView addSubview:_historyScroll];

    _traceScroll = [self makeStripScroll];
    _traceTable = [self makeStripTableInScroll:_traceScroll click:@selector(traceClicked:)];
    {
        NSTableColumn *trace = [[NSTableColumn alloc] initWithIdentifier:@"trace"];
        trace.title = @"";
        trace.width = 760;
        trace.minWidth = 40;
        trace.resizingMask = NSTableColumnAutoresizingMask;
        [_traceTable addTableColumn:trace];
    }
    [_window.contentView addSubview:_traceScroll];

    _historyBar = [[NSView alloc] initWithFrame:NSMakeRect(0, kHistoryY, 760, kHistoryBarHeight)];
    _historyBar.translatesAutoresizingMaskIntoConstraints = YES;
    _historyBar.autoresizingMask = NSViewNotSizable;
    _historyBar.wantsLayer = YES;
    _historyBar.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.93 alpha:1].CGColor;
    _historyBar.hidden = YES;
    _historyDisclosure = [self stripButton:@"" action:@selector(toggleHistory:)];
    _historyDisclosure.imagePosition = NSImageOnly;
    _historyDisclosure.frame = NSMakeRect(2, 0, 26, kHistoryBarHeight);
    _historyTab = [self stripButton:@"History" action:@selector(selectHistoryTab:)];
    _traceTab = [self stripButton:@"Stack Trace" action:@selector(selectTraceTab:)];
    [self placeStripTab:_historyTab x:30];
    [self placeStripTab:_traceTab x:NSMaxX(_historyTab.frame) + 4];
    [_historyBar addSubview:_historyDisclosure];
    [_historyBar addSubview:_historyTab];
    [_historyBar addSubview:_traceTab];
    [self updateStripChrome];
    [_window.contentView addSubview:_historyBar];

    NSTextField *promptLabel = [NSTextField labelWithString:@"Immediate"];
    promptLabel.translatesAutoresizingMaskIntoConstraints = YES;
    promptLabel.frame = NSMakeRect(12, 44, 107, 20);
    promptLabel.autoresizingMask = NSViewMaxYMargin;
    promptLabel.font = [NSFont systemFontOfSize:13];
    promptLabel.textColor = NSColor.blackColor;
    promptLabel.alignment = NSTextAlignmentRight;
    [_window.contentView addSubview:promptLabel];

    _immediate = [NSTextField new];
    _immediate.translatesAutoresizingMaskIntoConstraints = YES;
    _immediate.identifier = @"immediate";
    _immediate.frame = NSMakeRect(127, 40, 621, 26);
    _immediate.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    _immediate.font = _font;
    _immediate.delegate = self;
    _immediate.bezeled = YES;
    _immediate.bezelStyle = NSTextFieldRoundedBezel;
    _immediate.drawsBackground = YES;
    _immediate.backgroundColor = NSColor.whiteColor;
    _immediate.textColor = NSColor.blackColor;
    _immediate.focusRingType = NSFocusRingTypeDefault;
    _immediate.editable = YES;
    _immediate.selectable = YES;
    _immediate.placeholderString = @"Command to run, or a line number to insert";
    ((NSTextFieldCell *)_immediate.cell).wraps = NO;
    ((NSTextFieldCell *)_immediate.cell).scrollable = YES;
    [_window.contentView addSubview:_immediate];

    _runButton = [self toolButton:@"Run" action:@selector(runProgram:) x:12];
    _pauseButton = [self toolButton:@"Pause" action:@selector(pauseProgram:) x:92];
    _stopButton = [self toolButton:@"Stop" action:@selector(stopProgram:) x:172];
    _stepButton = [self toolButton:@"Step" action:@selector(stepProgram:) x:252];
    [_window.contentView addSubview:_runButton];
    [_window.contentView addSubview:_pauseButton];
    [_window.contentView addSubview:_stopButton];
    [_window.contentView addSubview:_stepButton];

    _status = [StatusField labelWithString:@""];
    _status.translatesAutoresizingMaskIntoConstraints = YES;
    _status.frame = NSMakeRect(336, 12, 412, 22);
    _status.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    _status.alignment = NSTextAlignmentRight;
    _status.textColor = NSColor.blackColor;
    _status.font = [NSFont systemFontOfSize:12];
    _status.target = self;
    _status.action = @selector(statusClicked:);
    [_window.contentView addSubview:_status];
    [self updateButtons];
    [self layoutListingAnimated:NO];
}

- (void)windowDidResize:(NSNotification *)note {
    (void)note;
    [self layoutListingAnimated:NO];
}

- (NSScrollView *)makeStripScroll {
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, kHistoryY, 760, 0)];
    scroll.autoresizingMask = NSViewNotSizable;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSBezelBorder;
    scroll.drawsBackground = YES;
    scroll.backgroundColor = [NSColor colorWithCalibratedWhite:0.96 alpha:1];
    scroll.hidden = YES;
    return scroll;
}

- (NSTableView *)makeStripTableInScroll:(NSScrollView *)scroll click:(SEL)action {
    NSTableView *table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 760, 0)];
    NSClickGestureRecognizer *click = [[NSClickGestureRecognizer alloc] initWithTarget:self action:action];
    table.backgroundColor = scroll.backgroundColor;
    table.headerView = nil;
    table.rowHeight = kHistoryRowHeight;
    table.intercellSpacing = NSMakeSize(0, 0);
    table.selectionHighlightStyle = NSTableViewSelectionHighlightStyleSourceList;
    table.gridStyleMask = NSTableViewGridNone;
    table.allowsEmptySelection = YES;
    table.allowsMultipleSelection = NO;
    table.focusRingType = NSFocusRingTypeNone;
    table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    if (@available(macOS 11.0, *)) table.style = NSTableViewStylePlain;
    table.dataSource = self;
    table.delegate = self;
    [table addGestureRecognizer:click];
    scroll.documentView = table;
    return table;
}

- (NSButton *)stripButton:(NSString *)title action:(SEL)action {
    NSButton *button = [NSButton new];
    button.translatesAutoresizingMaskIntoConstraints = YES;
    button.autoresizingMask = NSViewNotSizable;
    button.bordered = NO;
    button.bezelStyle = NSBezelStyleShadowlessSquare;
    button.buttonType = NSButtonTypeMomentaryPushIn;
    button.imagePosition = title.length ? NSNoImage : NSImageOnly;
    button.alignment = NSTextAlignmentCenter;
    button.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
    button.title = title ?: @"";
    button.contentTintColor = NSColor.blackColor;
    button.target = self;
    button.action = action;
    button.refusesFirstResponder = YES;
    button.wantsLayer = YES;
    button.layer.cornerRadius = 3;
    return button;
}

- (void)placeStripTab:(NSButton *)tab x:(CGFloat)x {
    NSRect frame;
    [tab sizeToFit];
    frame = tab.frame;
    frame.origin.x = x;
    frame.origin.y = 1;
    frame.size.width += 16;
    frame.size.height = kHistoryBarHeight - 2;
    tab.frame = frame;
}

- (void)styleStripTab:(NSButton *)tab selected:(BOOL)selected {
    NSColor *bg = selected ? [NSColor colorWithCalibratedWhite:0.99 alpha:1]
                           : [NSColor colorWithCalibratedWhite:0.93 alpha:1];
    tab.layer.backgroundColor = bg.CGColor;
}

- (CGFloat)historyStripHeight {
    if (!_historyOpen || !_historyExpanded) return 0;
    return kHistoryVisibleRows * kHistoryRowHeight + 2;
}

- (void)updateStripChrome {
    NSString *symbol = _historyExpanded ? @"chevron.down" : @"chevron.right";
    NSImage *image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil];
    if (image) {
        NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration configurationWithPointSize:11 weight:NSFontWeightSemibold];
        image = [image imageWithSymbolConfiguration:config] ?: image;
        image.template = YES;
    }
    _historyDisclosure.image = image;
    [self styleStripTab:_historyTab selected:_stripTab == kStripHistory];
    [self styleStripTab:_traceTab selected:_stripTab == kStripTrace];
}

- (void)applyStripVisibility {
    BOOL body = _historyOpen && _historyExpanded;
    _historyBar.hidden = !_historyOpen;
    _historyScroll.hidden = !body || _stripTab != kStripHistory;
    _traceScroll.hidden = !body || _stripTab != kStripTrace;
}

- (IBAction)toggleHistory:(id)sender {
    (void)sender;
    if (!_historyOpen) return;
    _historyExpanded = !_historyExpanded;
    [self updateStripChrome];
    [self layoutListingAnimated:YES];
    if (_historyExpanded && _stripTab == kStripTrace) [self refreshTrace];
}

- (void)selectStripTab:(NSInteger)tab {
    BOOL opening = !_historyExpanded;
    _stripTab = tab;
    _historyExpanded = YES;
    [self updateStripChrome];
    [self layoutListingAnimated:opening];
    if (tab == kStripTrace) [self refreshTrace];
}

- (IBAction)selectHistoryTab:(id)sender {
    (void)sender;
    [self selectStripTab:kStripHistory];
}

- (IBAction)selectTraceTab:(id)sender {
    (void)sender;
    [self selectStripTab:kStripTrace];
}

- (void)layoutListingAnimated:(BOOL)animated {
    NSRect bounds = _window.contentView.bounds;
    CGFloat width = bounds.size.width;
    CGFloat height = bounds.size.height;
    CGFloat barH = _historyOpen ? kHistoryBarHeight : 0;
    CGFloat historyH = [self historyStripHeight];
    CGFloat editorY = _historyOpen ? (kHistoryY + barH + historyH) : 76;
    CGFloat editorH = height - editorY;
    NSRect editorFrame;
    NSRect contentFrame;
    NSRect barFrame;
    if (editorH < 48) editorH = 48;
    editorFrame = NSMakeRect(0, editorY, width, editorH);
    /* Open: bar sits on top of the body. Collapsed: historyH is 0, so the bar stays at the bottom. */
    barFrame = NSMakeRect(0, kHistoryY + historyH, width, barH);
    contentFrame = NSMakeRect(0, kHistoryY, width, historyH);
    [self applyStripVisibility];
    if (!animated) {
        _historyBar.frame = barFrame;
        _editorScroll.frame = editorFrame;
        _historyScroll.frame = contentFrame;
        _traceScroll.frame = contentFrame;
        [_historyTable sizeLastColumnToFit];
        [_traceTable sizeLastColumnToFit];
        return;
    }
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.16;
        self->_historyBar.animator.frame = barFrame;
        self->_editorScroll.animator.frame = editorFrame;
        self->_historyScroll.animator.frame = contentFrame;
        self->_traceScroll.animator.frame = contentFrame;
    } completionHandler:^{
        NSTableView *table;
        NSInteger last;
        [self->_historyTable sizeLastColumnToFit];
        [self->_traceTable sizeLastColumnToFit];
        if (!self->_historyExpanded) return;
        if (self->_stripTab == kStripTrace) {
            table = self->_traceTable;
            last = (NSInteger)self->_traceLines.count - 1;
        } else {
            table = self->_historyTable;
            last = (NSInteger)self->_history.count - 1;
        }
        if (last >= 0) [table scrollRowToVisible:last];
    }];
}

- (void)rememberImmediate:(NSString *)text {
    NSString *line = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSInteger last;
    if (line.length == 0) return;
    [_history addObject:line];
    if (!_historyOpen) {
        _historyOpen = YES;
        _historyExpanded = YES;
        _stripTab = kStripHistory;
        [self updateStripChrome];
        [self layoutListingAnimated:YES];
    }
    [_historyTable reloadData];
    last = (NSInteger)_history.count - 1;
    if (last >= 0) [_historyTable scrollRowToVisible:last];
}

- (void)placeImmediateText:(NSString *)text {
    NSText *editor;
    if (_immediate.enabled) [_window makeFirstResponder:_immediate];
    editor = _immediate.enabled ? _immediate.currentEditor : nil;
    if (editor) {
        editor.string = text;
        [editor setSelectedRange:NSMakeRange(text.length, 0)];
    } else {
        _immediate.stringValue = text;
    }
    [self highlightImmediate];
}

- (void)historyClicked:(id)sender {
    NSInteger row;
    NSString *line;
    NSPoint point;
    if (![sender isKindOfClass:[NSGestureRecognizer class]]) return;
    point = [(NSGestureRecognizer *)sender locationInView:_historyTable];
    row = [_historyTable rowAtPoint:point];
    if (row < 0 || row >= (NSInteger)_history.count) return;
    [_historyTable selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    line = _history[(NSUInteger)row];
    dispatch_async(dispatch_get_main_queue(), ^{
        [self placeImmediateText:line];
    });
}

- (NSString *)traceRowForLine:(uint16_t)number indent:(NSUInteger)indent {
    AsLine *ln = as_program_find(_program, number);
    NSString *src = [self textForLine:ln];
    NSString *pad = [@"" stringByPaddingToLength:indent withString:@" " startingAtIndex:0];
    if (src.length == 0) return [NSString stringWithFormat:@"%@%u", pad, number];
    return [NSString stringWithFormat:@"%@%u %@", pad, number, src];
}

- (void)refreshTrace {
    uint16_t numbers[128];
    int count = 0;
    int shown, i;
    uint16_t current;
    if (!_traceTable) return;
    [_traceLines removeAllObjects];
    [_traceNumbers removeAllObjects];
    as_machine_gosub_stack(_machine, numbers, 128, &count);
    shown = count;
    if (shown > 128) shown = 128;
    if (shown < 0) shown = 0;
    for (i = 0; i < shown; i++) {
        [_traceLines addObject:[self traceRowForLine:numbers[i] indent:(NSUInteger)i * 2]];
        [_traceNumbers addObject:@(numbers[i])];
    }
    current = as_machine_line(_machine);
    if (current != 0) {
        [_traceLines addObject:[self traceRowForLine:current indent:(NSUInteger)count * 2]];
        [_traceNumbers addObject:@(current)];
    }
    [_traceTable reloadData];
    if (_traceLines.count > 0) [_traceTable scrollRowToVisible:(NSInteger)_traceLines.count - 1];
}

- (void)showStackTrace {
    BOOL opening = !_historyOpen || !_historyExpanded;
    _stripTab = kStripTrace;
    _historyOpen = YES;
    _historyExpanded = YES;
    [self updateStripChrome];
    [self layoutListingAnimated:opening];
    [self refreshTrace];
}

- (void)noteExecutionStopped {
    if (_stripTab == kStripTrace && _historyOpen) [self refreshTrace];
}

- (void)traceClicked:(id)sender {
    NSInteger row;
    NSPoint point;
    uint16_t number;
    if (![sender isKindOfClass:[NSGestureRecognizer class]]) return;
    point = [(NSGestureRecognizer *)sender locationInView:_traceTable];
    row = [_traceTable rowAtPoint:point];
    if (row < 0 || row >= (NSInteger)_traceNumbers.count) return;
    [_traceTable selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    number = (uint16_t)_traceNumbers[(NSUInteger)row].unsignedIntValue;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self centerOnLine:number edit:NO];
    });
}

- (uint16_t)nextNumber {
    size_t n = as_program_count(_program);
    unsigned v;
    if (n == 0) return 10;
    v = as_program_at(_program, n - 1)->number;
    if (v > 63999u - 10u) return 63999;
    return (uint16_t)(v + 10u);
}

- (BOOL)parseNumber:(NSString *)s out:(int *)out {
    int n = 0;
    NSUInteger i;
    if (s.length == 0 || s.length > 5) return NO;
    for (i = 0; i < s.length; i++) {
        unichar c = [s characterAtIndex:i];
        if (c < '0' || c > '9') return NO;
        n = n * 10 + (c - '0');
    }
    if (n > 63999) return NO;
    *out = n;
    return YES;
}

- (NSString *)textForLine:(AsLine *)ln {
    char *text = NULL;
    NSString *s;
    if (!ln || as_detokenize(ln->tokens, ln->len, &text) != 0 || !text) return @"";
    s = [NSString stringWithUTF8String:text] ?: @"";
    free(text);
    return s;
}

- (NSAttributedString *)decorate:(NSString *)text errorText:(NSString **)errOut {
    return [self decorate:text errorText:errOut links:nil];
}

- (NSAttributedString *)decorate:(NSString *)text errorText:(NSString **)errOut links:(NSArray<NSValue *> *)links {
    AsTokenized t;
    size_t at = 0;
    int rc = AS_OK;
    NSMutableAttributedString *attr;
    NSUInteger slen;
    size_t i;
    NSDictionary *base = @{
        NSFontAttributeName: _font,
        NSForegroundColorAttributeName: NSColor.blackColor
    };
    if (errOut) *errOut = nil;
    as_tokenize(text.UTF8String ?: "", &t);
    if (t.len > 0) rc = as_check_line(t.bytes, t.len, &at);
    attr = [[NSMutableAttributedString alloc] initWithString:text ?: @"" attributes:base];
    slen = text.length;
    for (i = 0; i < t.source_len && i < slen; i++) {
        NSColor *c = NSColor.blackColor;
        if (t.klass[i] == AS_CLS_REMARK) {
            [attr addAttribute:NSFontAttributeName value:_remarkFont range:NSMakeRange(i, 1)];
            continue;
        }
        if (t.klass[i] == AS_CLS_KEYWORD) c = _keywordColor;
        else if (t.klass[i] == AS_CLS_VARIABLE) c = _variableColor;
        [attr addAttribute:NSForegroundColorAttributeName value:c range:NSMakeRange(i, 1)];
    }
    if (rc != AS_OK && t.len > 0) {
        NSUInteger loc = slen ? slen - 1 : 0;
        NSUInteger nlen = slen ? 1 : 0;
        if (at < t.len) {
            loc = t.src[at];
            nlen = t.src_n[at] ? t.src_n[at] : 1;
        }
        if (loc < slen && nlen > 0) {
            if (loc + nlen > slen) nlen = slen - loc;
            [attr addAttribute:NSUnderlineStyleAttributeName value:@(NSUnderlineStyleThick) range:NSMakeRange(loc, nlen)];
            [attr addAttribute:NSUnderlineColorAttributeName value:NSColor.systemRedColor range:NSMakeRange(loc, nlen)];
        }
        if (errOut) *errOut = [NSString stringWithFormat:@"?%s", as_error_text(rc)];
    }
    for (NSValue *value in links) {
        NSRange link = value.rangeValue;
        if (link.length > 0 && link.location != NSNotFound && NSMaxRange(link) <= attr.length) {
            [attr addAttribute:NSUnderlineStyleAttributeName value:@(NSUnderlineStyleSingle) range:link];
            [attr addAttribute:NSUnderlineColorAttributeName value:_keywordColor range:link];
        }
    }
    as_tokenized_free(&t);
    return attr;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    if (tableView == _historyTable) return (NSInteger)_history.count;
    if (tableView == _traceTable) return (NSInteger)_traceLines.count;
    NSInteger n = (NSInteger)as_program_count(_program);
    if (!as_machine_running(_machine)) n += 1;
    return n;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    if (tableView == _historyTable) {
        BOOL command = [column.identifier isEqualToString:@"cmd"];
        NSString *reuse = command ? @"cmd" : @"pad";
        HistoryLineField *field = [tableView makeViewWithIdentifier:reuse owner:self];
        NSString *line = @"";
        if (command && row >= 0 && row < (NSInteger)_history.count) line = _history[(NSUInteger)row];
        if (!field) {
            field = [HistoryLineField new];
            field.translatesAutoresizingMaskIntoConstraints = YES;
            field.identifier = reuse;
            field.bezeled = NO;
            field.bordered = NO;
            field.editable = NO;
            field.selectable = NO;
            field.font = _font;
            field.textColor = NSColor.blackColor;
            field.lineBreakMode = NSLineBreakByTruncatingTail;
            field.drawsBackground = NO;
            ((NSTextFieldCell *)field.cell).wraps = NO;
            ((NSTextFieldCell *)field.cell).scrollable = NO;
        }
        field.stringValue = line;
        return field;
    }
    if (tableView == _traceTable) {
        HistoryLineField *field = [tableView makeViewWithIdentifier:@"trace" owner:self];
        NSString *line = @"";
        if (row >= 0 && row < (NSInteger)_traceLines.count) line = _traceLines[(NSUInteger)row];
        if (!field) {
            field = [HistoryLineField new];
            field.translatesAutoresizingMaskIntoConstraints = YES;
            field.identifier = @"trace";
            field.bezeled = NO;
            field.bordered = NO;
            field.editable = NO;
            field.selectable = NO;
            field.font = _font;
            field.textColor = NSColor.blackColor;
            field.lineBreakMode = NSLineBreakByTruncatingTail;
            field.drawsBackground = NO;
            ((NSTextFieldCell *)field.cell).wraps = NO;
            ((NSTextFieldCell *)field.cell).scrollable = NO;
        }
        field.stringValue = line;
        return field;
    }
    BOOL running = as_machine_running(_machine);
    BOOL paused = as_machine_paused(_machine);
    size_t count = as_program_count(_program);
    BOOL phantom = (size_t)row >= count;
    AsLine *ln = phantom ? NULL : as_program_at(_program, (size_t)row);
    BOOL here = (running || paused) && ln && as_machine_on_line(_machine, ln->number);
    NSString *ident = column.identifier;

    if ([ident isEqualToString:@"brk"]) {
        NSButton *b = [tableView makeViewWithIdentifier:ident owner:self];
        if (!b) {
            b = [NSButton new];
            b.identifier = ident;
            b.title = @"";
            [b setButtonType:NSButtonTypeSwitch];
            b.target = self;
            b.action = @selector(toggleBreak:);
        }
        b.hidden = phantom;
        b.state = (ln && ln->breakpoint) ? NSControlStateValueOn : NSControlStateValueOff;
        b.enabled = !phantom;
        return b;
    }

    if ([ident isEqualToString:@"arrow"]) {
        NSImageView *iv = [tableView makeViewWithIdentifier:ident owner:self];
        if (!iv) {
            NSImage *image = [NSImage imageWithSystemSymbolName:@"arrowtriangle.right.fill"
                                        accessibilityDescription:@"Current line"];
            NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration configurationWithPointSize:12
                                                                                                 weight:NSFontWeightSemibold];
            iv = [NSImageView new];
            iv.identifier = ident;
            iv.imageAlignment = NSImageAlignCenter;
            iv.imageScaling = NSImageScaleProportionallyDown;
            iv.editable = NO;
            if (image) {
                image = [image imageWithSymbolConfiguration:config] ?: image;
                image.template = YES;
            }
            iv.image = image;
            iv.contentTintColor = NSColor.systemBlueColor;
        }
        iv.hidden = !(paused && here);
        return iv;
    }

    NSTextField *field = [tableView makeViewWithIdentifier:ident owner:self];
    if (!field) {
        field = [ident isEqualToString:@"src"] ? [SrcField new] : [NSTextField new];
        field.identifier = ident;
        field.bezeled = NO;
        field.bordered = NO;
        field.drawsBackground = YES;
        field.focusRingType = NSFocusRingTypeNone;
        field.font = _font;
        field.delegate = self;
        ((NSTextFieldCell *)field.cell).wraps = NO;
        ((NSTextFieldCell *)field.cell).scrollable = YES;
        field.lineBreakMode = NSLineBreakByClipping;
    }
    field.editable = !running;
    field.selectable = YES;
    field.textColor = NSColor.blackColor;
    if ([ident isEqualToString:@"num"]) {
        uint16_t number = phantom ? [self nextNumber] : ln->number;
        NSColor *bg = NSColor.whiteColor;
        field.stringValue = [NSString stringWithFormat:@"%u", number];
        field.alignment = NSTextAlignmentRight;
        if (here) bg = [NSColor colorWithCalibratedRed:0.80 green:0.88 blue:1 alpha:1];
        else if (!phantom && as_program_is_target(_program, number))
            bg = [NSColor colorWithCalibratedRed:1 green:0.95 blue:0.75 alpha:1];
        field.backgroundColor = bg;
    } else {
        NSString *text = phantom ? @"" : [self textForLine:ln];
        NSArray<NSValue *> *links = nil;
        field.alignment = NSTextAlignmentLeft;
        field.backgroundColor = here ? [NSColor colorWithCalibratedRed:0.93 green:0.96 blue:1 alpha:1] : NSColor.whiteColor;
        if ([NSEvent modifierFlags] & NSEventModifierFlagCommand) links = [self linkRangesInText:text];
        field.attributedStringValue = [self decorate:text errorText:NULL links:links];
        if ([field isKindOfClass:[SrcField class]]) ((SrcField *)field).linkRanges = links;
    }
    return field;
}

- (void)toggleBreak:(NSButton *)sender {
    NSInteger row = [_table rowForView:sender];
    if (row < 0 || (size_t)row >= as_program_count(_program)) return;
    as_program_set_break(_program, (size_t)row, sender.state == NSControlStateValueOn);
}

- (NSTextField *)fieldAtRow:(NSInteger)row ident:(NSString *)ident {
    NSView *rv;
    if (row < 0 || row >= _table.numberOfRows) return nil;
    rv = [_table rowViewAtRow:row makeIfNecessary:NO];
    for (NSView *v in rv.subviews) {
        if ([v.identifier isEqualToString:ident] && [v isKindOfClass:[NSTextField class]]) return (NSTextField *)v;
    }
    return nil;
}

- (void)scheduleReload {
    if (_reloadQueued) return;
    _reloadQueued = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        NSInteger row;
        self->_reloadQueued = NO;
        if (!self->_table) return;
        self->_reloading = YES;
        [self->_table reloadData];
        [self updateTitle];
        [self updateButtons];
        self->_reloading = NO;
        if (self->_pendingJump) {
            self->_focusRowValid = NO;
            self->_focusPhantom = NO;
            [self performPendingJump];
        } else if (self->_focusRowValid && !as_machine_running(self->_machine)) {
            self->_focusRowValid = NO;
            row = self->_focusRow;
            if (row >= self->_table.numberOfRows) row = self->_table.numberOfRows - 1;
            if (row >= 0) {
                [self->_table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
                [self->_table scrollRowToVisible:row];
                [self->_table editColumn:[self->_table columnWithIdentifier:@"src"] row:row withEvent:nil select:YES];
            }
        } else if (self->_focusPhantom && !as_machine_running(self->_machine)) {
            self->_focusPhantom = NO;
            row = (NSInteger)as_program_count(self->_program);
            if (row >= 0 && row < self->_table.numberOfRows) {
                [self->_table scrollRowToVisible:row];
                [self->_table editColumn:[self->_table columnWithIdentifier:@"src"] row:row withEvent:nil select:YES];
            }
        }
    });
}

- (void)controlTextDidChange:(NSNotification *)note {
    NSTextField *field = note.object;
    NSTextView *editor;
    NSString *err = nil;
    NSAttributedString *attr;
    NSRange sel;
    if (_highlighting || _reloading) return;
    if ([field.identifier isEqualToString:@"immediate"]) {
        [self highlightImmediate];
        return;
    }
    if (![field.identifier isEqualToString:@"src"]) return;
    _highlighting = YES;
    attr = [self decorate:field.stringValue errorText:&err];
    editor = (NSTextView *)field.currentEditor;
    if (editor) {
        sel = editor.selectedRange;
        [editor.textStorage setAttributedString:attr];
        if (sel.location <= editor.string.length) {
            if (sel.location + sel.length > editor.string.length) sel.length = editor.string.length - sel.location;
            [editor setSelectedRange:sel];
        }
        editor.typingAttributes = @{
            NSFontAttributeName: _font,
            NSForegroundColorAttributeName: NSColor.blackColor
        };
    } else {
        field.attributedStringValue = attr;
    }
    if (err) {
        _status.stringValue = err;
        _showingSyntax = YES;
    } else if (_showingSyntax) {
        _showingSyntax = NO;
        _status.stringValue = @"";
    }
    _highlighting = NO;
    if ([field isKindOfClass:[SrcField class]]) ((SrcField *)field).linkRanges = nil;
    [self refreshJumpLink];
}

- (NSInteger)currentProgramRow {
    NSResponder *responder = _window.firstResponder;
    NSInteger row = -1;
    if ([responder isKindOfClass:[NSTextView class]]) {
        id delegate = [(NSTextView *)responder delegate];
        if ([delegate isKindOfClass:[NSTextField class]] && delegate != _immediate)
            row = [_table rowForView:delegate];
    }
    if (row < 0) row = _table.selectedRow;
    if (row < 0 || (size_t)row >= as_program_count(_program)) return -1;
    return row;
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(deleteLine:))
        return !as_machine_running(_machine) && [self currentProgramRow] >= 0;
    return YES;
}

- (IBAction)deleteLine:(id)sender {
    NSInteger row;
    uint16_t number;
    (void)sender;
    if (as_machine_running(_machine)) return;
    row = [self currentProgramRow];
    if (row < 0) return;
    number = as_program_at(_program, (size_t)row)->number;
    _abandonEdit = YES;
    [_window makeFirstResponder:nil];
    _abandonEdit = NO;
    as_machine_edited(_machine);
    as_program_upsert(_program, number, NULL, 0, 0);
    _showingSyntax = NO;
    _focusPhantom = NO;
    _focusRow = row;
    _focusRowValid = YES;
    [self scheduleReload];
}

- (void)controlTextDidEndEditing:(NSNotification *)note {
    NSTextField *field = note.object;
    NSInteger row;
    if (_abandonEdit) return;
    if ([field.identifier isEqualToString:@"immediate"]) return;
    if (_reloading || _committing || _highlighting) return;
    row = [_table rowForView:field];
    if (row < 0) return;
    _committing = YES;
    if ([field.identifier isEqualToString:@"src"]) [self commitSource:field row:row];
    else if ([field.identifier isEqualToString:@"num"]) [self commitNumber:field row:row];
    _committing = NO;
}

- (void)commitSource:(NSTextField *)field row:(NSInteger)row {
    AsTokenized t;
    size_t count = as_program_count(_program);
    as_tokenize(field.stringValue.UTF8String ?: "", &t);
    if ((size_t)row >= count) {
        int n = 0;
        NSTextField *numf;
        if (t.len == 0) {
            as_tokenized_free(&t);
            return;
        }
        numf = [self fieldAtRow:row ident:@"num"];
        if (![self parseNumber:numf.stringValue out:&n]) n = [self nextNumber];
        as_machine_edited(_machine);
        as_program_upsert(_program, (uint16_t)n, t.bytes, t.len, 0);
        _focusPhantom = YES;
        _showingSyntax = NO;
        as_tokenized_free(&t);
        [self scheduleReload];
        return;
    }
    {
        AsLine *ln = as_program_at(_program, (size_t)row);
        uint16_t number = ln->number;
        int br = ln->breakpoint;
        int same = ln->tokens && ln->len == t.len && memcmp(ln->tokens, t.bytes, t.len) == 0;
        if (t.len == 0) {
            as_machine_edited(_machine);
            as_program_upsert(_program, number, NULL, 0, 0);
            _showingSyntax = NO;
            [self scheduleReload];
        } else if (!same) {
            as_machine_edited(_machine);
            as_program_upsert(_program, number, t.bytes, t.len, br);
            _showingSyntax = NO;
            [self scheduleReload];
        }
    }
    as_tokenized_free(&t);
}

- (void)commitNumber:(NSTextField *)field row:(NSInteger)row {
    int n = 0;
    AsLine *ln;
    uint16_t old;
    size_t len;
    int br;
    uint8_t *copy;
    if ((size_t)row >= as_program_count(_program)) return;
    if (![self parseNumber:field.stringValue out:&n]) {
        [self scheduleReload];
        return;
    }
    ln = as_program_at(_program, (size_t)row);
    if (ln->number == (uint16_t)n) return;
    old = ln->number;
    len = ln->len;
    br = ln->breakpoint;
    copy = (uint8_t *)malloc(len + 1);
    if (!copy) return;
    memcpy(copy, ln->tokens, len + 1);
    as_machine_edited(_machine);
    as_program_upsert(_program, (uint16_t)n, copy, len, br);
    if (old != (uint16_t)n) as_program_upsert(_program, old, NULL, 0, 0);
    free(copy);
    _showingSyntax = NO;
    [self scheduleReload];
}

- (void)applyAttributes:(NSAttributedString *)attr toField:(NSTextField *)field {
    NSTextView *editor = (NSTextView *)field.currentEditor;
    if (editor) {
        NSRange sel = editor.selectedRange;
        [editor.textStorage setAttributedString:attr];
        if (sel.location <= editor.string.length) {
            if (sel.location + sel.length > editor.string.length) sel.length = editor.string.length - sel.location;
            [editor setSelectedRange:sel];
        }
        editor.typingAttributes = @{
            NSFontAttributeName: _font,
            NSForegroundColorAttributeName: NSColor.blackColor
        };
    } else {
        field.attributedStringValue = attr;
    }
}

- (NSUInteger)immediateBody:(NSString *)text number:(int *)number valid:(BOOL *)valid {
    NSUInteger i = 0, n = text.length;
    int num = 0, digits = 0;
    *valid = YES;
    *number = -1;
    while (i < n && [text characterAtIndex:i] == ' ') i++;
    if (i >= n) return 0;
    if ([text characterAtIndex:i] < '0' || [text characterAtIndex:i] > '9') return 0;
    while (i < n) {
        unichar c = [text characterAtIndex:i];
        if (c < '0' || c > '9') break;
        digits++;
        num = num * 10 + (int)(c - '0');
        if (digits > 5 || num > 63999) *valid = NO;
        i++;
    }
    *number = num;
    while (i < n && [text characterAtIndex:i] == ' ') i++;
    return i;
}

- (void)highlightImmediate {
    NSString *text = _immediate.currentEditor.string ?: _immediate.stringValue ?: @"";
    int number = -1;
    BOOL valid = YES;
    NSUInteger bodyAt = [self immediateBody:text number:&number valid:&valid];
    NSDictionary *base = @{
        NSFontAttributeName: _font,
        NSForegroundColorAttributeName: NSColor.blackColor
    };
    NSMutableAttributedString *all = [[NSMutableAttributedString alloc] initWithString:text attributes:base];
    NSString *err = nil;
    _highlighting = YES;
    if (!valid && number >= 0) {
        NSUInteger loc = 0;
        while (loc < text.length && [text characterAtIndex:loc] == ' ') loc++;
        NSUInteger end = loc;
        while (end < text.length) {
            unichar c = [text characterAtIndex:end];
            if (c < '0' || c > '9') break;
            end++;
        }
        if (end > loc) {
            [all addAttribute:NSUnderlineStyleAttributeName value:@(NSUnderlineStyleThick) range:NSMakeRange(loc, end - loc)];
            [all addAttribute:NSUnderlineColorAttributeName value:NSColor.systemRedColor range:NSMakeRange(loc, end - loc)];
        }
        err = @"?SYNTAX ERROR";
    } else if (bodyAt < text.length) {
        NSAttributedString *rest = [self decorate:[text substringFromIndex:bodyAt] errorText:&err];
        [all replaceCharactersInRange:NSMakeRange(bodyAt, text.length - bodyAt) withAttributedString:rest];
    }
    [self applyAttributes:all toField:_immediate];
    if (err) {
        _status.stringValue = err;
        _showingSyntax = YES;
    } else if (_showingSyntax) {
        _showingSyntax = NO;
        _status.stringValue = @"";
    }
    _highlighting = NO;
}

- (void)enumerateSourceFields:(void (^)(SrcField *field, NSInteger row))block {
    if (!_table || !block) return;
    [_table enumerateAvailableRowViewsUsingBlock:^(NSTableRowView *rowView, NSInteger row) {
        for (NSView *view in rowView.subviews) {
            if (![view isKindOfClass:[SrcField class]]) continue;
            block((SrcField *)view, row);
            return;
        }
    }];
}

- (NSArray<NSValue *> *)linkRangesInText:(NSString *)text {
    AsJumpRef refs[48];
    size_t n, i;
    const char *utf;
    NSMutableArray<NSValue *> *ranges;
    if (text.length == 0) return nil;
    utf = text.UTF8String ?: "";
    if (strlen(utf) != text.length) return nil;
    n = as_jump_numbers(utf, refs, sizeof refs / sizeof refs[0]);
    if (n == 0) return nil;
    ranges = [NSMutableArray arrayWithCapacity:n];
    for (i = 0; i < n; i++) {
        if ((NSUInteger)refs[i].end > text.length || refs[i].begin >= refs[i].end) continue;
        [ranges addObject:[NSValue valueWithRange:NSMakeRange(refs[i].begin, refs[i].end - refs[i].begin)]];
    }
    return ranges.count ? ranges : nil;
}

- (void)showLinks:(NSArray<NSValue *> *)links onField:(NSTextField *)field {
    NSString *text;
    if (!field || _highlighting) return;
    text = field.currentEditor.string ?: field.stringValue ?: @"";
    _highlighting = YES;
    [self applyAttributes:[self decorate:text errorText:NULL links:links] toField:field];
    _highlighting = NO;
}

- (void)setLinks:(NSArray<NSValue *> *)links onField:(SrcField *)field {
    BOOL same = (field.linkRanges.count == 0 && links.count == 0) || [field.linkRanges isEqualToArray:links];
    if (same) return;
    field.linkRanges = links.count ? links : nil;
    [self showLinks:field.linkRanges onField:field];
    [field.window invalidateCursorRectsForView:field];
}

- (void)clearJumpLinks {
    _jumpsShown = NO;
    [self enumerateSourceFields:^(SrcField *field, NSInteger row) {
        (void)row;
        [self setLinks:nil onField:field];
    }];
}

- (BOOL)jumpHit:(uint16_t *)number range:(NSRange *)range row:(NSInteger *)rowOut field:(NSTextField **)fieldOut {
    NSPoint winPt, tablePt, fieldPt;
    NSInteger row, col;
    NSTextField *field;
    NSString *text;
    NSUInteger index;
    const char *utf;
    uint16_t num = 0;
    size_t begin = 0, end = 0;
    if (!([NSEvent modifierFlags] & NSEventModifierFlagCommand) || !_window || !_table) return NO;
    winPt = [_window convertPointFromScreen:[NSEvent mouseLocation]];
    tablePt = [_table convertPoint:winPt fromView:nil];
    row = [_table rowAtPoint:tablePt];
    col = [_table columnAtPoint:tablePt];
    if (row < 0 || col < 0) return NO;
    if (![_table.tableColumns[(NSUInteger)col].identifier isEqualToString:@"src"]) return NO;
    field = [_table viewAtColumn:col row:row makeIfNecessary:NO];
    if (![field isKindOfClass:[SrcField class]]) return NO;
    fieldPt = [field convertPoint:tablePt fromView:_table];
    if (!NSPointInRect(fieldPt, field.bounds)) return NO;
    index = [(SrcField *)field characterIndexForPoint:fieldPt];
    if (index == NSNotFound) return NO;
    text = field.currentEditor.string ?: field.stringValue ?: @"";
    utf = text.UTF8String ?: "";
    if (strlen(utf) != text.length || index >= text.length) return NO;
    if (!as_jump_number_at(utf, (size_t)index, &num, &begin, &end)) return NO;
    if (end < begin || end > text.length) return NO;
    *number = num;
    *range = NSMakeRange(begin, end - begin);
    *rowOut = row;
    *fieldOut = field;
    return YES;
}

- (void)refreshJumpLink {
    NSRange visible;
    if (_reloading || _highlighting || _committing || !_table) return;
    if (!([NSEvent modifierFlags] & NSEventModifierFlagCommand)) {
        if (_jumpsShown) [self clearJumpLinks];
        return;
    }
    visible = [_table rowsInRect:_table.visibleRect];
    _jumpsShown = YES;
    [self enumerateSourceFields:^(SrcField *field, NSInteger row) {
        NSArray<NSValue *> *links = nil;
        BOOL onScreen = visible.length > 0 && visible.location != NSNotFound && row >= (NSInteger)visible.location &&
                        row < (NSInteger)NSMaxRange(visible);
        if (onScreen) links = [self linkRangesInText:field.currentEditor.string ?: field.stringValue ?: @""];
        [self setLinks:links onField:field];
    }];
}

- (NSEvent *)handleJumpEvent:(NSEvent *)event {
    uint16_t number = 0;
    NSRange range = NSMakeRange(NSNotFound, 0);
    NSInteger row = -1;
    NSTextField *field = nil;
    if (event.type == NSEventTypeLeftMouseDown) {
        if (event.window == _window && [self jumpHit:&number range:&range row:&row field:&field]) {
            [self jumpToLine:number];
            return nil;
        }
        return event;
    }
    if ((event.type == NSEventTypeMouseMoved || event.type == NSEventTypeMouseExited) && event.window && event.window != _window) {
        if (_jumpsShown) [self clearJumpLinks];
        return event;
    }
    [self refreshJumpLink];
    return event;
}

- (BOOL)centerOnLine:(uint16_t)number edit:(BOOL)edit {
    size_t i, n = as_program_count(_program);
    for (i = 0; i < n; i++) {
        NSInteger row;
        NSRect rowRect, vis;
        CGFloat y;
        NSTextField *field;
        NSTextView *editor;
        if (as_program_at(_program, i)->number != number) continue;
        row = (NSInteger)i;
        [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
        rowRect = [_table rectOfRow:row];
        vis = [_table visibleRect];
        y = rowRect.origin.y - (vis.size.height - rowRect.size.height) / 2.0;
        if (y < 0) y = 0;
        [_table scrollPoint:NSMakePoint(0, y)];
        if (edit && !as_machine_running(_machine)) {
            [_table editColumn:[_table columnWithIdentifier:@"src"] row:row withEvent:nil select:NO];
            field = [self fieldAtRow:row ident:@"src"];
            editor = (NSTextView *)field.currentEditor;
            if (editor) [editor setSelectedRange:NSMakeRange(0, 0)];
        }
        return YES;
    }
    return NO;
}

- (void)performPendingJump {
    uint16_t number;
    if (!_pendingJump) return;
    number = _pendingJumpNumber;
    _pendingJump = NO;
    if (![self centerOnLine:number edit:YES]) {
        _showingSyntax = NO;
        _status.stringValue = [NSString stringWithFormat:@"No line %u", number];
    }
    [self refreshJumpLink];
}

- (void)jumpToLine:(uint16_t)number {
    NSResponder *responder = _window.firstResponder;
    _pendingJump = YES;
    _pendingJumpNumber = number;
    if ([responder isKindOfClass:[NSTextView class]]) {
        id delegate = [(NSTextView *)responder delegate];
        if ([delegate isKindOfClass:[NSTextField class]] && delegate != _immediate)
            [_window makeFirstResponder:nil];
    }
    if (!_reloadQueued) [self performPendingJump];
}

- (void)scrollToLine:(uint16_t)number {
    size_t i, n = as_program_count(_program);
    for (i = 0; i < n; i++) {
        if (as_program_at(_program, i)->number == number) {
            [_table scrollRowToVisible:(NSInteger)i];
            return;
        }
    }
}

- (void)statusClicked:(id)sender {
    int number = 0;
    (void)sender;
    if (!status_line_number(_status.stringValue, &number)) return;
    [self showStackTrace];
    [self centerOnLine:(uint16_t)number edit:NO];
}

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)commandSelector {
    if (control != _immediate || commandSelector != @selector(insertNewline:)) return NO;
    if ([self submitImmediate:control]) {
        textView.string = @"";
        [textView setSelectedRange:NSMakeRange(0, 0)];
    }
    return YES;
}

- (BOOL)submitImmediate:(id)sender {
    NSString *text;
    const char *utf;
    int kind = 0;
    int rc;
    int number = -1;
    BOOL valid = YES;
    (void)sender;
    if (_committing || as_machine_running(_machine)) return NO;
    text = _immediate.currentEditor.string ?: _immediate.stringValue ?: @"";
    if (text.length == 0) return NO;
    utf = text.UTF8String ?: "";
    [self immediateBody:text number:&number valid:&valid];
    _committing = YES;
    rc = as_machine_prompt(_machine, utf, &kind);
    if (rc) {
        _status.stringValue = [NSString stringWithFormat:@"?%s", as_error_text(rc)];
        _showingSyntax = YES;
        _committing = NO;
        return NO;
    }
    _showingSyntax = NO;
    if (kind == 2) {
        [self ensureRunWindowTakingKey:NO];
        as_machine_quantum(_machine, 0.008);
        [self syncTone];
        _wasRunning = as_machine_running(_machine);
        _shownLine = as_machine_line(_machine);
    }
    if (!as_machine_running(_machine)) {
        _reloading = YES;
        [_table reloadData];
        _reloading = NO;
        if (kind == 1 && number >= 0) [self scrollToLine:(uint16_t)number];
        [self updateTitle];
    }
    [self updateButtons];
    if (kind == 1 || kind == 2) [self rememberImmediate:text];
    if (kind == 1 || (kind == 2 && !as_machine_running(_machine) && !as_machine_paused(_machine) &&
                      as_machine_status(_machine)[0] == 0))
        _status.stringValue = @"";
    else
        [self refreshStatus];
    if (kind == 2 && !as_machine_running(_machine)) [self noteExecutionStopped];
    _committing = NO;
    return YES;
}

- (void)updateTitle {
    NSString *name = _fileURL.lastPathComponent ?: @"Untitled";
    if (as_program_dirty(_program)) name = [name stringByAppendingString:@" •"];
    _window.title = name;
}

- (void)updateButtons {
    BOOL running = as_machine_running(_machine);
    BOOL paused = as_machine_paused(_machine);
    _pauseButton.enabled = running;
    _stopButton.enabled = running || paused;
    _stepButton.enabled = !as_machine_waiting(_machine);
    _runButton.enabled = YES;
    _immediate.enabled = !running;
}

- (void)refreshStatus {
    const char *st;
    if (_showingSyntax) return;
    st = as_machine_status(_machine);
    if (as_machine_waiting(_machine) == AS_INPUT || as_machine_waiting(_machine) == AS_GET) {
        _status.stringValue = @"Waiting for input";
        return;
    }
    if (st && st[0]) _status.stringValue = [NSString stringWithUTF8String:st];
    else if (as_machine_paused(_machine)) _status.stringValue = @"Paused";
    else if (!as_machine_running(_machine) && !_showingSyntax) {
        /* Keep a message that was set by Stop. */
    }
}

- (void)ensureRunWindowTakingKey:(BOOL)takeKey {
    if (!_runWindow) {
        _runWindow = [RunWindow new];
        _runWindow.view.screen = _screen;
        _runWindow.view.keyDelegate = self;
    }
    if (_runWindow.window.isVisible) return;
    if (takeKey) [_runWindow show];
    else [_runWindow orderFront];
}

- (void)ensureRunWindow {
    [self ensureRunWindowTakingKey:YES];
}

- (void)syncTone {
    if (_runWindow) [_runWindow syncTone];
}

- (void)tick:(NSTimer *)timer {
    int running;
    (void)timer;
    _tick++;
    if (_screen->bell) {
        NSBeep();
        _screen->bell = 0;
    }
    if (_tick % 30 == 0) _screen->flash_phase ^= 1;
    running = as_machine_running(_machine);
    if (running) {
        uint16_t line;
        as_machine_quantum(_machine, 0.008);
        line = as_machine_line(_machine);
        if (!_wasRunning || line != _shownLine || !as_machine_running(_machine)) {
            _shownLine = line;
            _reloading = YES;
            [_table reloadData];
            _reloading = NO;
            [self updateButtons];
            [self refreshStatus];
            if (!as_machine_running(_machine)) [self noteExecutionStopped];
            if (as_machine_paused(_machine)) [self scrollToLine:line];
        }
        _wasRunning = as_machine_running(_machine);
    } else if (_wasRunning) {
        _wasRunning = NO;
        _reloading = YES;
        [_table reloadData];
        _reloading = NO;
        [self updateButtons];
        [self refreshStatus];
        [self noteExecutionStopped];
        if (as_machine_paused(_machine)) [self scrollToLine:as_machine_line(_machine)];
    }
    if (_runWindow.window.isVisible) [_runWindow.view setNeedsDisplay:YES];
    [self syncTone];
    if (!_reloading && !_highlighting && !_committing) [self refreshJumpLink];
}

- (void)runView:(RunView *)view typed:(int)ch {
    int waiting = as_machine_waiting(_machine);
    (void)view;
    as_machine_keydown(_machine, ch);
    if (waiting && !as_machine_waiting(_machine)) as_machine_quantum(_machine, 0.008);
    [_runWindow.view setNeedsDisplay:YES];
    if (!as_machine_running(_machine)) {
        _wasRunning = NO;
        _reloading = YES;
        [_table reloadData];
        _reloading = NO;
        [self updateButtons];
        [self noteExecutionStopped];
    }
    [self refreshStatus];
}

- (IBAction)runProgram:(id)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    [self ensureRunWindow];
    if (as_machine_paused(_machine)) as_machine_cont(_machine);
    else as_machine_run(_machine);
    [self syncTone];
    _wasRunning = as_machine_running(_machine);
    _shownLine = as_machine_line(_machine);
    _showingSyntax = NO;
    _reloading = YES;
    [_table reloadData];
    _reloading = NO;
    [self updateButtons];
    [self refreshStatus];
    [_runWindow.view setNeedsDisplay:YES];
}

- (IBAction)pauseProgram:(id)sender {
    (void)sender;
    as_machine_pause(_machine);
    [self syncTone];
    _wasRunning = NO;
    _reloading = YES;
    [_table reloadData];
    _reloading = NO;
    [self updateButtons];
    [self refreshStatus];
    [self noteExecutionStopped];
    if (as_machine_paused(_machine)) [self scrollToLine:as_machine_line(_machine)];
}

- (IBAction)stopProgram:(id)sender {
    (void)sender;
    as_machine_stop(_machine);
    [self syncTone];
    _wasRunning = NO;
    _showingSyntax = NO;
    _reloading = YES;
    [_table reloadData];
    _reloading = NO;
    [self updateButtons];
    [self refreshStatus];
    [self noteExecutionStopped];
}

- (IBAction)stepProgram:(id)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    [self ensureRunWindow];
    as_machine_step(_machine);
    as_machine_quantum(_machine, 1.0);
    [self syncTone];
    _wasRunning = as_machine_running(_machine);
    _shownLine = as_machine_line(_machine);
    _showingSyntax = NO;
    _reloading = YES;
    [_table reloadData];
    _reloading = NO;
    [self updateButtons];
    [self refreshStatus];
    [self showStackTrace];
    if (as_machine_paused(_machine)) [self scrollToLine:_shownLine];
    [_runWindow.view setNeedsDisplay:YES];
}

- (void)alert:(NSString *)msg {
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = msg.length ? msg : @"Error";
    [a addButtonWithTitle:@"OK"];
    [a runModal];
}

- (UTType *)abasType {
    return [UTType typeWithFilenameExtension:@"abas"] ?: UTTypeData;
}

- (BOOL)saveToURL:(NSURL *)url {
    uint8_t *data = NULL;
    size_t len = 0;
    char err[160];
    NSError *e = nil;
    int rc = as_program_save(_program, &data, &len, err, sizeof err);
    NSData *bytes;
    if (rc) {
        [self alert:err[0] ? [NSString stringWithUTF8String:err] : @"Could not save"];
        return NO;
    }
    bytes = [NSData dataWithBytes:data length:len];
    free(data);
    if (![bytes writeToURL:url options:NSDataWritingAtomic error:&e]) {
        [self alert:e.localizedDescription ?: @"Could not save"];
        return NO;
    }
    as_program_mark_clean(_program);
    _fileURL = url;
    [self updateTitle];
    return YES;
}

- (BOOL)saveDocument:(id)sender {
    (void)sender;
    if (_fileURL) return [self saveToURL:_fileURL];
    return [self saveDocumentAs:sender];
}

- (BOOL)saveDocumentAs:(id)sender {
    NSSavePanel *panel = [NSSavePanel savePanel];
    (void)sender;
    panel.allowedContentTypes = @[ [self abasType] ];
    panel.nameFieldStringValue = _fileURL.lastPathComponent ?: @"Program.abas";
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return NO;
    return [self saveToURL:panel.URL];
}

- (BOOL)confirmDirty {
    NSAlert *a;
    NSModalResponse r;
    if (!as_program_dirty(_program)) return YES;
    a = [[NSAlert alloc] init];
    a.messageText = @"Save changes to this program?";
    a.informativeText = @"Programs are saved as a tokenized AppleSoft image.";
    [a addButtonWithTitle:@"Save"];
    [a addButtonWithTitle:@"Don't Save"];
    [a addButtonWithTitle:@"Cancel"];
    r = [a runModal];
    if (r == NSAlertThirdButtonReturn) return NO;
    if (r == NSAlertSecondButtonReturn) {
        as_program_mark_clean(_program);
        return YES;
    }
    return [self saveDocument:nil];
}

- (void)resetScreen {
    as_screen_text_mode(_screen);
    as_screen_home(_screen);
    _screen->cursor_on = 0;
    if (_runWindow.view) [_runWindow.view setNeedsDisplay:YES];
}

- (IBAction)newDocument:(id)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    if (![self confirmDirty]) return;
    as_machine_stop(_machine);
    as_program_clear(_program);
    as_program_mark_clean(_program);
    _fileURL = nil;
    _showingSyntax = NO;
    _status.stringValue = @"";
    _wasRunning = NO;
    [self resetScreen];
    [self updateTitle];
    [_table reloadData];
    [self updateButtons];
}

- (IBAction)openDocument:(id)sender {
    NSOpenPanel *panel;
    NSData *data;
    char err[160];
    int rc;
    (void)sender;
    [_window makeFirstResponder:nil];
    if (![self confirmDirty]) return;
    panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[ [self abasType] ];
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return;
    data = [NSData dataWithContentsOfURL:panel.URL];
    if (!data) {
        [self alert:@"Could not read that file"];
        return;
    }
    [_window makeFirstResponder:nil];
    as_machine_stop(_machine);
    rc = as_program_load(_program, data.bytes, data.length, err, sizeof err);
    if (rc) {
        [self alert:err[0] ? [NSString stringWithUTF8String:err] : @"Not an AppleSoft program"];
        return;
    }
    as_program_mark_clean(_program);
    _fileURL = panel.URL;
    _showingSyntax = NO;
    _status.stringValue = @"";
    _wasRunning = NO;
    [self resetScreen];
    [self updateTitle];
    [_table reloadData];
    [self updateButtons];
}

- (IBAction)closeDocument:(id)sender {
    [_window performClose:sender];
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    return [self confirmDirty];
}

- (IBAction)renumber:(id)sender {
    int rc;
    (void)sender;
    if (as_machine_running(_machine)) return;
    [_window makeFirstResponder:nil];
    rc = as_program_renumber(_program, 10, 10);
    if (rc) {
        [self alert:[NSString stringWithUTF8String:as_error_text(rc)]];
        return;
    }
    as_machine_edited(_machine);
    _showingSyntax = NO;
    [_table reloadData];
    [self updateTitle];
    [self updateButtons];
}

@end
