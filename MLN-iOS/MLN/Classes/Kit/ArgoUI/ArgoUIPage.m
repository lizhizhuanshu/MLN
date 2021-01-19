//
//  ArgoUIPage.m
//  ArgoUI
//
//  Created by sun-zt on 2021/1/19.
//

#import "ArgoUIPage.h"
#import "MLNUIStaticExporterMacro.h"
#import "MLNUIViewController.h"
#import "MLNUIKitHeader.h"
#import "MLNUIView.h"
#import "MLNUIKitInstanceFactory.h"
#import "MLNUIKitInstance.h"
#import "MLNUIKitInstanceHandlersManager.h"

@implementation ArgoUIPage

static int argoui_attach_UIPage(lua_State *L) {
    mlnui_luaui_check_begin();
    mlnui_luaui_checkstring_rt(L, -1);
    mlnui_luaui_checkudata_rt(L, -2);
    mlnui_luaui_check_end();
    MLNUILuaCore *luaCore = MLNUI_LUA_CORE(L);
    MLNUIViewController *kitViewController = (MLNUIViewController *)MLNUI_KIT_INSTANCE(luaCore).viewController;
    MLNUIView *superView = (MLNUIView *)[luaCore toNativeObject:-2 error:NULL];
    NSString *path = [luaCore toString:-1 error:NULL];
    
    MLNUIKitInstance *instance = [[MLNUIKitInstanceFactory defaultFactory] createKitInstanceWithLuaBundle:[luaCore currentBundle] rootView:superView viewController:kitViewController];
    if (kitViewController.regClasses && kitViewController.regClasses.count > 0) {
        [instance registerClasses:kitViewController.regClasses error:NULL];
    }
    
    MLNUIKitInstance *delegate = (MLNUIKitInstance *)luaCore.delegate;
    if (!delegate) {
        delegate = [kitViewController kitInstance];
    }
    instance.delegate = delegate.delegate;
    instance.instanceHandlersManager.errorHandler = delegate.instanceHandlersManager.errorHandler;
    
    
    if (![path hasSuffix:@".lua"]) {
        path = [NSString stringWithFormat:@"%@.lua", path];
    }
    
    NSError *error;
    [instance runWithEntryFile:path windowExtra:delegate.windowExtra error:&error];
    if (error) {
        MLNUIError(luaCore, @"%@", error);
        return 0;
    }
    objc_setAssociatedObject(superView, __func__, instance, OBJC_ASSOCIATION_RETAIN);
    lua_pushboolean(L, 1);
    return 1;
}


#pragma mark - setup to lua

LUAUI_EXPORT_STATIC_BEGIN(ArgoUIPage)

LUAUI_NEW_EXPORT_GLOBAL_C_FUNC(attachUIPage, argoui_attach_UIPage, ArgoUIPage)

LUAUI_EXPORT_STATIC_END(ArgoUIPage, ArgoUI, NO, NULL)

@end
