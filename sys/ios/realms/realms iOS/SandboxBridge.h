//
//  SandboxBridge.h
//  realms iOS
//

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface SandboxBridge : NSObject <MTKViewDelegate>

- (nullable instancetype)initWithMetalKitView:(MTKView *)view NS_SWIFT_NAME(init(metalKitView:));

- (void)setGameplayReady:(BOOL)ready NS_SWIFT_NAME(setGameplayReady(_:));
- (void)setMoveRight:(float)right forward:(float)forward NS_SWIFT_NAME(setMoveRight(_:forward:));
- (void)addLookDeltaX:(float)dx deltaY:(float)dy NS_SWIFT_NAME(addLookDeltaX(_:deltaY:));
- (void)setVerticalInput:(float)vertical NS_SWIFT_NAME(setVerticalInput(_:));
- (void)setDroneThrottle:(float)throttle yaw:(float)yaw pitch:(float)pitch roll:(float)roll NS_SWIFT_NAME(setDroneThrottle(_:yaw:pitch:roll:));
- (void)sendCommand:(int32_t)command NS_SWIFT_NAME(sendCommand(_:));
// Status slots 0...13 retain their original meaning; 14 is stance,
// 15 and 16 are player yaw and pitch in radians.
- (NSInteger)pollStatus:(float *)outValues count:(NSInteger)count NS_SWIFT_NAME(pollStatus(_:count:));
// Packed x, y, z, yaw tuples for render-only remote players.
- (void)setRemotePlayers:(const float * _Nullable)values count:(NSInteger)count NS_SWIFT_NAME(setRemotePlayers(_:count:));
- (void)resetInput NS_SWIFT_NAME(resetInput());
- (void)flushSave NS_SWIFT_NAME(flushSave());

@end

NS_ASSUME_NONNULL_END
