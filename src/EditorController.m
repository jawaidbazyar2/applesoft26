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

@implementation AppController {
    AsProgram *_program;
    AsScreen *_screen;
    AsMachine *_machine;
    NSWindow *_window;
    NSTableView *_table;
    NSTextField *_status;
    NSTextField *_immediate;
    NSButton *_runButton;
    NSButton *_pauseButton;
    NSButton *_stopButton;
    NSButton *_stepButton;
    NSTimer *_timer;
    NSFont *_font;
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
}

- (void)applicationWillFinishLaunching:(NSNotification *)note {
    (void)note;
    _program = as_program_new();
    _screen = as_screen_new();
    _machine = as_machine_new(_program, _screen);
    _font = [NSFont fontWithName:@"Menlo" size:13] ?: [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
    _keywordColor = [NSColor colorWithCalibratedRed:0.08 green:0.22 blue:0.62 alpha:1];
    _variableColor = [NSColor colorWithCalibratedRed:0.55 green:0.12 blue:0.38 alpha:1];
    [self buildMenus];
    [self buildWindow];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    (void)note;
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
    NSTableColumn *brk, *num, *src;
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.delegate = self;
    _window.releasedWhenClosed = NO;
    _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    _window.contentMinSize = NSMakeSize(520, 240);
    _window.title = @"Untitled";

    scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 76, 760, 564)];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
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
    [_table addTableColumn:num];
    [_table addTableColumn:src];
    scroll.documentView = _table;
    [_window.contentView addSubview:scroll];

    NSTextField *promptLabel = [NSTextField labelWithString:@"Immediate"];
    promptLabel.translatesAutoresizingMaskIntoConstraints = YES;
    promptLabel.frame = NSMakeRect(12, 44, 84, 20);
    promptLabel.autoresizingMask = NSViewMaxYMargin;
    promptLabel.font = [NSFont systemFontOfSize:13];
    promptLabel.textColor = NSColor.blackColor;
    promptLabel.alignment = NSTextAlignmentRight;
    [_window.contentView addSubview:promptLabel];

    _immediate = [NSTextField new];
    _immediate.translatesAutoresizingMaskIntoConstraints = YES;
    _immediate.identifier = @"immediate";
    _immediate.frame = NSMakeRect(104, 40, 644, 26);
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

    _status = [NSTextField labelWithString:@""];
    _status.translatesAutoresizingMaskIntoConstraints = YES;
    _status.frame = NSMakeRect(336, 12, 412, 22);
    _status.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    _status.alignment = NSTextAlignmentRight;
    _status.textColor = NSColor.blackColor;
    _status.font = [NSFont systemFontOfSize:12];
    [_window.contentView addSubview:_status];
    [self updateButtons];
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
    as_tokenized_free(&t);
    return attr;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    NSInteger n = (NSInteger)as_program_count(_program);
    if (!as_machine_running(_machine)) n += 1;
    return n;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
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

    NSTextField *field = [tableView makeViewWithIdentifier:ident owner:self];
    if (!field) {
        field = [NSTextField new];
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
        field.alignment = NSTextAlignmentLeft;
        field.backgroundColor = here ? [NSColor colorWithCalibratedRed:0.93 green:0.96 blue:1 alpha:1] : NSColor.whiteColor;
        field.attributedStringValue = [self decorate:text errorText:NULL];
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
        if (self->_focusPhantom && !as_machine_running(self->_machine)) {
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
}

- (void)controlTextDidEndEditing:(NSNotification *)note {
    NSTextField *field = note.object;
    NSInteger row;
    if ([field.identifier isEqualToString:@"immediate"]) {
        NSNumber *movement = note.userInfo[@"NSTextMovement"];
        if (movement.integerValue == NSReturnTextMovement) [self submitImmediate:field];
        return;
    }
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
    NSUInteger i = 0, digitAt, n = text.length;
    int num = 0, digits = 0;
    *valid = YES;
    *number = -1;
    while (i < n && [text characterAtIndex:i] == ' ') i++;
    if (i >= n) return 0;
    if ([text characterAtIndex:i] < '0' || [text characterAtIndex:i] > '9') return 0;
    digitAt = i;
    while (i < n) {
        unichar c = [text characterAtIndex:i];
        if (c < '0' || c > '9') break;
        digits++;
        num = num * 10 + (int)(c - '0');
        if (digits > 5 || num > 63999) *valid = NO;
        i++;
    }
    *number = num;
    (void)digitAt;
    while (i < n && [text characterAtIndex:i] == ' ') i++;
    return i;
}

- (void)highlightImmediate {
    NSString *text = _immediate.stringValue ?: @"";
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

- (void)scrollToLine:(uint16_t)number {
    size_t i, n = as_program_count(_program);
    for (i = 0; i < n; i++) {
        if (as_program_at(_program, i)->number == number) {
            [_table scrollRowToVisible:(NSInteger)i];
            return;
        }
    }
}

- (void)submitImmediate:(id)sender {
    NSString *text;
    const char *utf;
    int kind = 0;
    int rc;
    int number = -1;
    BOOL valid = YES;
    (void)sender;
    if (_committing || as_machine_running(_machine)) return;
    text = _immediate.stringValue ?: @"";
    if (text.length == 0) return;
    utf = text.UTF8String ?: "";
    [self immediateBody:text number:&number valid:&valid];
    _committing = YES;
    rc = as_machine_prompt(_machine, utf, &kind);
    if (rc) {
        _status.stringValue = [NSString stringWithFormat:@"?%s", as_error_text(rc)];
        _showingSyntax = YES;
        _committing = NO;
        return;
    }
    _showingSyntax = NO;
    _immediate.stringValue = @"";
    if (kind == 2) {
        [self ensureRunWindow];
        as_machine_quantum(_machine, 0.008);
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
    if (kind == 1 || (kind == 2 && !as_machine_running(_machine) && !as_machine_paused(_machine) &&
                      as_machine_status(_machine)[0] == 0))
        _status.stringValue = @"";
    else
        [self refreshStatus];
    _committing = NO;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!as_machine_running(self->_machine))
            [self->_window makeFirstResponder:self->_immediate];
    });
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
    if (as_machine_waiting(_machine)) {
        _status.stringValue = @"Waiting for input";
        return;
    }
    if (st && st[0]) _status.stringValue = [NSString stringWithUTF8String:st];
    else if (as_machine_paused(_machine)) _status.stringValue = @"Paused";
    else if (!as_machine_running(_machine) && !_showingSyntax) {
        /* Keep a message that was set by Stop. */
    }
}

- (void)ensureRunWindow {
    if (!_runWindow) {
        _runWindow = [RunWindow new];
        _runWindow.view.screen = _screen;
        _runWindow.view.keyDelegate = self;
    }
    [_runWindow show];
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
        }
        _wasRunning = as_machine_running(_machine);
    } else if (_wasRunning) {
        _wasRunning = NO;
        _reloading = YES;
        [_table reloadData];
        _reloading = NO;
        [self updateButtons];
        [self refreshStatus];
    }
    if (_runWindow.window.isVisible) [_runWindow.view setNeedsDisplay:YES];
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
    }
    [self refreshStatus];
}

- (IBAction)runProgram:(id)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    [self ensureRunWindow];
    if (as_machine_paused(_machine)) as_machine_cont(_machine);
    else as_machine_run(_machine);
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
    _wasRunning = NO;
    _reloading = YES;
    [_table reloadData];
    _reloading = NO;
    [self updateButtons];
    [self refreshStatus];
}

- (IBAction)stopProgram:(id)sender {
    (void)sender;
    as_machine_stop(_machine);
    _wasRunning = NO;
    _showingSyntax = NO;
    _reloading = YES;
    [_table reloadData];
    _reloading = NO;
    [self updateButtons];
    [self refreshStatus];
}

- (IBAction)stepProgram:(id)sender {
    (void)sender;
    [_window makeFirstResponder:nil];
    [self ensureRunWindow];
    as_machine_step(_machine);
    as_machine_quantum(_machine, 1.0);
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
