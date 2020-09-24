//
//  MLNUIWaterfallAutoAdapter.m
//  ArgoUI
//
//  Created by MOMO on 2020/9/22.
//

#import "MLNUIWaterfallAutoAdapter.h"
#import "MLNUIKitHeader.h"
#import "MLNUIViewExporterMacro.h"
#import "UIView+MLNUILayout.h"
#import "MLNUICollectionViewCell.h"
#import "MLNUIInternalWaterfallView.h"
#import "MLNUIWaterfallHeaderView.h"

@interface MLNUIWaterfallAutoAdapter ()<MLNUICollectionViewCellDelegate>

@end

@implementation MLNUIWaterfallAutoAdapter

#pragma mark - Override

- (Class)cellClass {
    return [MLNUICollectionViewAutoSizeCell class];
}

#pragma mark - MLNUIWaterfallLayoutDelegate

- (CGFloat)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)collectionViewLayout heightForItemAtIndexPath:(NSIndexPath *)indexPath {

    NSValue *sizeValue = [self.cachesManager layoutInfoWithIndexPath:indexPath];
    if (sizeValue) {
        CGSize size = [sizeValue CGSizeValue];
        if (!CGSizeEqualToSize(size, CGSizeZero)) {
            return size.height;
        }
    }
    
    NSString *reuseId = [self reuseIdentifierAtIndexPath:indexPath];
    [self registerCellClassIfNeed:collectionView reuseId:reuseId];
    
    MLNUICollectionViewAutoSizeCell *cell = [[MLNUICollectionViewAutoSizeCell alloc] init];
    cell.delegate = self;
    [cell pushContentViewWithLuaCore:self.mlnui_luaCore forNodeKey:indexPath];
    
    MLNUIBlock *initCallback = [self initedCellCallbackByReuseId:reuseId];
    [initCallback addLuaTableArgument:[cell getLuaTable]];
    [initCallback callIfCan];
    
    MLNUIBlock *reuseCallback = [self fillCellDataCallbackByReuseId:reuseId];
    [reuseCallback addLuaTableArgument:[cell getLuaTable]];
    [reuseCallback addIntArgument:(int)indexPath.section+1];
    [reuseCallback addIntArgument:(int)indexPath.item+1];
    [reuseCallback callIfCan];
    
    CGSize size = [cell calculSizeWithMaxWidth:MLNUIUndefined maxHeight:MLNUIUndefined];
    [self.cachesManager updateLayoutInfo:[NSValue valueWithCGSize:size] forIndexPath:indexPath];
    return size.height;
}

#pragma mark - MLNUICollectionViewCellDelegate

- (void)mlnuiCollectionViewCellShouldReload:(MLNUICollectionViewCell *)cell size:(CGSize)size {
    NSLog(@"========33====== size: %@", NSStringFromCGSize(size));
}

#pragma mark - UICollectionViewDelegateFlowLayout

- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)collectionViewLayout referenceSizeForHeaderInSection:(NSInteger)section {
    if (section != 0) { // WaterfallView 限制只有一个headerView
        return CGSizeMake(0, 0);
    }
    
    UIView *headerView = [MLNUIInternalWaterfallView headerViewInWaterfall:collectionView];
    if (!headerView) {
        BOOL isHeaderValid = [self headerIsValidWithWaterfallView:collectionView];
        if (section == 0 && isHeaderValid) {
            
            NSIndexPath *indexPath = [NSIndexPath indexPathForRow:0 inSection:section];
            
            [collectionView registerClass:[MLNUIWaterfallHeaderView class] forSupplementaryViewOfKind:UICollectionElementKindSectionHeader withReuseIdentifier:kMLNUIWaterfallHeaderViewReuseID];
            MLNUIWaterfallHeaderView *header = [collectionView dequeueReusableSupplementaryViewOfKind:UICollectionElementKindSectionHeader withReuseIdentifier:kMLNUIWaterfallHeaderViewReuseID forIndexPath:indexPath];
            
            [header pushContentViewWithLuaCore:self.mlnui_luaCore forNodeKey:indexPath];
            
            [self.initedHeaderCallback addLuaTableArgument:[header getLuaTable]];
            [self.initedHeaderCallback addIntArgument:(int)indexPath.section+1];
            [self.initedHeaderCallback addIntArgument:(int)indexPath.row+1];
            [self.initedHeaderCallback callIfCan];
            
            [self.reuseHeaderCallback addLuaTableArgument:[header getLuaTable]];
            [self.reuseHeaderCallback addIntArgument:(int)indexPath.section+1];
            [self.reuseHeaderCallback addIntArgument:(int)indexPath.row+1];
            [self.reuseHeaderCallback callIfCan];
            
            CGFloat width = CGRectGetWidth(collectionView.frame);
            CGSize size = [header calculSizeWithMaxWidth:width maxHeight:MLNUIUndefined];
            return CGSizeMake(0, size.height);
        }
        return CGSizeZero;
    } else {
        CGSize size = [headerView.mlnui_layoutNode calculateLayoutWithSize:CGSizeMake(CGRectGetWidth(collectionView.frame), CGFLOAT_MAX)];
        return CGSizeMake(0, size.height);
    }
}

#pragma mark - Export Lua

LUAUI_EXPORT_BEGIN(MLNUIWaterfallAutoAdapter)
LUAUI_EXPORT_END(MLNUIWaterfallAutoAdapter, WaterfallAutoAdapter, YES, "MLNUIWaterfallAdapter", NULL)

@end
