//
//  MLNUILayoutEngine.m
//
//
//  Created by MoMo on 2018/10/24.
//

#import "MLNUILayoutEngine.h"
#import "MLNUIMainRunLoopObserver.h"
#import "MLNUISizeCahceManager.h"
#import "UIView+MLNUILayout.h"
#import "MLNUILayoutNode.h"

#define kMLNUIRunLoopBeforeWaitingLayoutOrder 0 // befor CATransaction(2000000)

@interface MLNUILayoutEngine ()
{
    MLNUISizeCahceManager *_sizeCacheManager;
}
@property (nonatomic, strong) NSMutableArray<MLNUILayoutNode *> *rootNodesPool;
@property (nonatomic, strong) NSMutableDictionary<id, MLNUILayoutNode *> *mutexRootNodesPool;
@property (nonatomic, strong) MLNUIMainRunLoopObserver *mainLoopObserver;

@end
@implementation MLNUILayoutEngine

- (instancetype)initWithLuaInstance:(MLNUIKitInstance *)luaInstance
{
    if (self = [super init]) {
        _luaInstance = luaInstance;
    }
    return self;
}

- (void)start
{
    if (!self.mainLoopObserver) {
        self.mainLoopObserver = [[MLNUIMainRunLoopObserver alloc] init];
        [self.mainLoopObserver beginForBeforeWaiting:kMLNUIRunLoopBeforeWaitingLayoutOrder repeats:YES callback:^{
            [self requestLayout];
        }];
    }
}

- (void)end
{
    [self.mainLoopObserver end];
}

- (void)addRootnode:(MLNUILayoutNode *)rootnode {
    if (rootnode.isRootNode && ![self.rootNodesPool containsObject:rootnode]) {
        [self.rootNodesPool addObject:rootnode];
    }
}

- (void)addRootNode:(MLNUILayoutNode *)rootNode forKey:(id)key {
    if (!rootNode || !key) return;
    MLNUILayoutNode *needRemoveNode = [self.mutexRootNodesPool objectForKey:key];
    if (needRemoveNode) {
        [self.rootNodesPool removeObject:needRemoveNode];
    }
    [self.mutexRootNodesPool setObject:rootNode forKey:key];
    [self.rootNodesPool addObject:rootNode];
}

- (void)removeRootNode:(MLNUILayoutNode *)rootnode {
    if (rootnode.isRootNode && [self.rootNodesPool containsObject:rootnode]) {
        [self.rootNodesPool removeObject:rootnode];
    }
}

- (void)removeRootNodeForKey:(id)key {
    if (!key) return;
    MLNUILayoutNode *needRemoveNode = [self.mutexRootNodesPool objectForKey:key];
    if (needRemoveNode) {
        [self.rootNodesPool removeObject:needRemoveNode];
        [self.mutexRootNodesPool removeObjectForKey:key];
    }
}

- (void)requestLayout {
    NSArray<MLNUILayoutNode *> *roots = [self.rootNodesPool copy];
    for (MLNUILayoutNode *rootnode in roots) {
        if (rootnode.isDirty) {
            [rootnode applyLayout];
        }
    }
}

#pragma mark - Getter
- (NSMutableArray<MLNUILayoutNode *> *)rootNodesPool
{
    if (!_rootNodesPool) {
        _rootNodesPool = [NSMutableArray array];
    }
    return _rootNodesPool;
}

- (NSMutableDictionary<id,MLNUILayoutNode *> *)mutexRootNodesPool {
    if (!_mutexRootNodesPool) {
        _mutexRootNodesPool = [NSMutableDictionary dictionary];
    }
    return _mutexRootNodesPool;
}

- (MLNUISizeCahceManager *)sizeCacheManager
{
    if (!_sizeCacheManager) {
        _sizeCacheManager = [[MLNUISizeCahceManager alloc] initWithInstance:self.luaInstance];
    }
    return _sizeCacheManager;
}

@end
