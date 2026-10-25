#import "RunWindow.h"

#import "as_font.h"

#include <stdlib.h>

@implementation RunWindow {
    NSWindow *_window;
    RunView *_view;
}

- (NSWindow *)window { return _window; }
- (RunView *)view { return _view; }

- (instancetype)init {
    self = [super init];
    if (!self) return nil;
    NSRect frame = NSMakeRect(860, 80, 840, 620);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Output";
    _window.releasedWhenClosed = NO;
    _window.contentMinSize = NSMakeSize(280, 192);
    _window.backgroundColor = NSColor.blackColor;
    _window.acceptsMouseMovedEvents = YES;
    _view = [[RunView alloc] initWithFrame:_window.contentView.bounds];
    _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_window.contentView addSubview:_view];
    return self;
}

- (void)show {
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_view];
}

@end

static uint32_t hires_rgb(int idx) {
    switch (idx & 7) {
    case 1: return 0x1CC000;
    case 2: return 0xC847E0;
    case 3:
    case 7: return 0xFFFFFF;
    case 5: return 0xFF6A00;
    case 6: return 0x1B7FDB;
    default: return 0x000000;
    }
}

static uint32_t color_of(const AsScreen *s, int idx) {
    if (s->disp == AS_DISP_HGR || s->disp == AS_DISP_HGR2) return hires_rgb(idx);
    return as_lores_rgb(idx);
}

static void put_px(uint8_t *px, int w, int h, int x, int y, uint32_t rgb) {
    uint8_t *p;
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    p = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
    p[0] = (uint8_t)((rgb >> 16) & 255);
    p[1] = (uint8_t)((rgb >> 8) & 255);
    p[2] = (uint8_t)(rgb & 255);
    p[3] = 255;
}

static void fill_block(uint8_t *px, int w, int h, int x, int y, int bw, int bh, uint32_t rgb) {
    int dy, dx;
    for (dy = 0; dy < bh; dy++) {
        for (dx = 0; dx < bw; dx++) put_px(px, w, h, x + dx, y + dy, rgb);
    }
}

static void draw_text(uint8_t *px, int w, int h, const AsScreen *s, int row0, int nrows, int originY, int flash) {
    int r, x, gy, gx;
    for (r = 0; r < nrows; r++) {
        int ycell = row0 + r;
        if (ycell < 0 || ycell > 23) continue;
        for (x = 0; x < 40; x++) {
            AsCell cell = s->cell[ycell][x];
            int style = cell.style;
            int cursor = s->cursor_on && s->cur_x == x && s->cur_y == ycell && flash;
            uint32_t fg, bg;
            int ch;
            if (style == 2) style = flash ? 1 : 0;
            if (cursor) style = 1;
            fg = style ? 0x000000 : 0xFFFFFF;
            bg = style ? 0xFFFFFF : 0x000000;
            ch = cell.ch ? cell.ch : ' ';
            for (gy = 0; gy < 8; gy++) {
                for (gx = 0; gx < 7; gx++) {
                    int on = as_glyph_on(ch, gy, gx);
                    put_px(px, w, h, x * 7 + gx, originY + r * 8 + gy, on ? fg : bg);
                }
            }
        }
    }
}

static CGImageRef image_from(uint8_t *px, int w, int h, CGColorSpaceRef cs) {
    CGContextRef ctx = CGBitmapContextCreate(px, (size_t)w, (size_t)h, 8, (size_t)w * 4, cs,
                                             kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
    CGImageRef img = NULL;
    if (ctx) {
        img = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
    }
    return img;
}

static void draw_image(CGImageRef img, NSRect dest) {
    CGContextRef c;
    if (!img) return;
    c = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(c);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextDrawImage(c, dest, img);
    CGContextRestoreGState(c);
}

static NSRect fit_aspect(NSRect bounds, CGFloat w, CGFloat h) {
    CGFloat s, dw, dh;
    if (w < 1 || h < 1) return bounds;
    s = MIN(bounds.size.width / w, bounds.size.height / h);
    if (s >= 1.0) s = floor(s);
    dw = w * s;
    dh = h * s;
    return NSMakeRect(bounds.origin.x + (bounds.size.width - dw) / 2.0,
                      bounds.origin.y + (bounds.size.height - dh) / 2.0, dw, dh);
}

static void paint_buffer(uint8_t *px, int w, int h, NSRect dest, CGColorSpaceRef cs) {
    CGImageRef img = image_from(px, w, h, cs);
    draw_image(img, dest);
    if (img) CGImageRelease(img);
}

@implementation RunView {
    NSTrackingArea *_track;
    BOOL _btn0;
    BOOL _btn1;
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_track) [self removeTrackingArea:_track];
    _track = [[NSTrackingArea alloc] initWithRect:self.bounds
                                          options:NSTrackingMouseMoved | NSTrackingActiveAlways |
                                                  NSTrackingInVisibleRect
                                            owner:self
                                         userInfo:nil];
    [self addTrackingArea:_track];
}

- (void)applyMouse:(NSEvent *)e {
    NSPoint p;
    NSRect b;
    int x = 0, y = 0;
    if (!self.screen) return;
    p = [self convertPoint:e.locationInWindow fromView:nil];
    b = self.bounds;
    if (b.size.width > 1) x = (int)(p.x / b.size.width * 255.0);
    if (b.size.height > 1) y = (int)((b.size.height - p.y) / b.size.height * 255.0);
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    if (y < 0) y = 0;
    if (y > 255) y = 255;
    self.screen->pdl[0] = x;
    self.screen->pdl[1] = y;
    self.screen->btn0 = _btn0 ? 1 : 0;
    self.screen->btn1 = _btn1 ? 1 : 0;
}

- (void)mouseMoved:(NSEvent *)e { [self applyMouse:e]; }
- (void)mouseDragged:(NSEvent *)e { [self applyMouse:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self applyMouse:e]; }

- (void)mouseDown:(NSEvent *)e {
    [self.window makeFirstResponder:self];
    _btn0 = YES;
    [self applyMouse:e];
}

- (void)mouseUp:(NSEvent *)e {
    _btn0 = NO;
    [self applyMouse:e];
}

- (void)rightMouseDown:(NSEvent *)e {
    [self.window makeFirstResponder:self];
    _btn1 = YES;
    [self applyMouse:e];
}

- (void)rightMouseUp:(NSEvent *)e {
    _btn1 = NO;
    [self applyMouse:e];
}

- (void)keyDown:(NSEvent *)e {
    NSString *s;
    unichar c;
    if (e.modifierFlags & NSEventModifierFlagCommand) return;
    s = e.charactersIgnoringModifiers;
    if (s.length == 0 || !self.keyDelegate) return;
    c = [s characterAtIndex:0];
    if (c == NSDeleteCharacter || c == NSBackspaceCharacter) c = 127;
    else if (c == NSEnterCharacter || c == NSCarriageReturnCharacter || c == NSNewlineCharacter) c = '\r';
    if (c > 127) return;
    [self.keyDelegate runView:self typed:(int)c];
}

- (void)drawClassic:(AsScreen *)s cs:(CGColorSpaceRef)cs flash:(int)flash {
    int cellH = (s->disp == AS_DISP_GR) ? 4 : 1;
    int grow = s->gh;
    int textRows = 0;
    int bw, bh, y;
    uint8_t *px;
    if (s->mixed && s->disp != AS_DISP_HGR2) {
        textRows = 4;
        if (s->disp == AS_DISP_GR) grow = s->gh > 8 ? s->gh - 8 : 0;
        else grow = s->gh > 32 ? 160 : s->gh;
    }
    if (grow > s->gh) grow = s->gh;
    bw = (s->disp == AS_DISP_GR) ? 280 : (s->gw > 280 ? s->gw : 280);
    bh = grow * cellH + textRows * 8;
    if (bw < 1 || bh < 1) return;
    px = (uint8_t *)calloc((size_t)bw * (size_t)bh, 4);
    if (!px) return;
    if (s->pix) {
        for (y = 0; y < grow; y++) {
            int x;
            for (x = 0; x < s->gw; x++) {
                uint32_t rgb = color_of(s, s->pix[y * s->gw + x]);
                if (cellH == 1 && bw == s->gw) put_px(px, bw, bh, x, y, rgb);
                else fill_block(px, bw, bh, x * 7, y * cellH, 7, cellH, rgb);
            }
        }
    }
    if (textRows) draw_text(px, bw, bh, s, 20, 4, grow * cellH, flash);
    paint_buffer(px, bw, bh, fit_aspect(self.bounds, bw, bh), cs);
    free(px);
}

- (void)drawExtended:(AsScreen *)s cs:(CGColorSpaceRef)cs flash:(int)flash {
    int gw = s->gw > 0 ? s->gw : 1;
    int gh = s->gh > 0 ? s->gh : 1;
    int textH = (s->mixed ? 32 : 0);
    CGFloat scale, totalH, maxW, x0, y0;
    NSRect gfxRect, textRect;
    uint8_t *px;
    int y, x;
    scale = MIN(self.bounds.size.width / (CGFloat)(gw > 280 ? gw : 280),
                self.bounds.size.height / (CGFloat)(gh + textH));
    if (scale >= 1.0) scale = floor(scale);
    if (scale < 0.05) scale = 0.05;
    totalH = (gh + textH) * scale;
    maxW = (gw > 280 ? gw : 280) * scale;
    x0 = self.bounds.origin.x + (self.bounds.size.width - maxW) / 2.0;
    y0 = self.bounds.origin.y + (self.bounds.size.height - totalH) / 2.0;
    gfxRect = NSMakeRect(x0 + (maxW - gw * scale) / 2.0, y0 + textH * scale, gw * scale, gh * scale);
    px = (uint8_t *)calloc((size_t)gw * (size_t)gh, 4);
    if (!px) return;
    if (s->pix) {
        for (y = 0; y < gh; y++) {
            for (x = 0; x < gw; x++) put_px(px, gw, gh, x, y, color_of(s, s->pix[y * gw + x]));
        }
    }
    paint_buffer(px, gw, gh, gfxRect, cs);
    free(px);
    if (!s->mixed) return;
    px = (uint8_t *)calloc(280u * 32u, 4);
    if (!px) return;
    draw_text(px, 280, 32, s, 20, 4, 0, flash);
    textRect = NSMakeRect(x0 + (maxW - 280 * scale) / 2.0, y0, 280 * scale, 32 * scale);
    paint_buffer(px, 280, 32, textRect, cs);
    free(px);
}

- (void)drawRect:(NSRect)dirty {
    CGColorSpaceRef cs;
    AsScreen *s;
    (void)dirty;
    [[NSColor blackColor] setFill];
    NSRectFill(self.bounds);
    s = self.screen;
    if (!s) return;
    cs = CGColorSpaceCreateDeviceRGB();
    if (!cs) return;
    if (s->disp == AS_DISP_TEXT || !s->graphics_on) {
        uint8_t *px = (uint8_t *)calloc(280u * 192u, 4);
        if (px) {
            draw_text(px, 280, 192, s, 0, 24, 0, s->flash_phase);
            paint_buffer(px, 280, 192, fit_aspect(self.bounds, 280, 192), cs);
            free(px);
        }
    } else if (s->disp == AS_DISP_GR80 || s->disp == AS_DISP_H640) {
        [self drawExtended:s cs:cs flash:s->flash_phase];
    } else {
        [self drawClassic:s cs:cs flash:s->flash_phase];
    }
    CGColorSpaceRelease(cs);
}

@end
