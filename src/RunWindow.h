#import <Cocoa/Cocoa.h>

#import "as_screen.h"

@class RunView;

@protocol RunViewKeyDelegate <NSObject>
- (void)runView:(RunView *)view typed:(int)ch;
@end

@interface RunView : NSView
@property (nonatomic, assign) AsScreen *screen;
@property (nonatomic, weak) id<RunViewKeyDelegate> keyDelegate;
@end

@interface RunWindow : NSObject
@property (nonatomic, strong, readonly) NSWindow *window;
@property (nonatomic, strong, readonly) RunView *view;
- (void)show;
@end
