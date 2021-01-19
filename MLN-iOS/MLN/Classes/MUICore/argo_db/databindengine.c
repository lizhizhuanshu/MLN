//
// Created by XiongFangyu on 2020/6/4.
//

#include "databindengine.h"
#include <string.h>
#include "mln_lauxlib.h"
#include "utils.h"
#include "LuaIPC.h"
#include "argo_lua.h"

#define CheckThread(L) ((void) 0)

typedef struct DataBind {
    D_malloc alloc;
    /**
     * 存储key->lua_State(被观察者)
     * 1:1
     */
    Map *key_observable;
    /**
     * 储存key->List(lua_State)观察者
     * 1:n
     */
    Map *key_observer;
    /**
     * 存储lua_State->List(key)
     * 1:n
     * 每个虚拟机被观察的key和观察key，释放时使用
     */
    Map *vm_key;
} DataBind;

static DataBind *instance = NULL;

/**
 * 在虚拟机全局表中设定一个特殊表来记录观察者(OTK)
 * 记录方式为: key->{function, params}
 */
#define OBSERVER_TABLE_KEY "__OTK"
/**
 * 在虚拟机全局表中设定一个特殊表来记录观察者(OTK_IN)
 * 用于监听table内部修改
 * 记录方式为: key->{function, params}
 */
#define OBSERVER_TABLE_INNER_KEY "__OTK_IN"
/**
 * 针对需要观察的表，在虚拟机全局表中设定一个特殊表来记录(OATK)
 * 记录方式为: key->table
 */
#define OBSERVABLE_TABLE_KEY "__OATK"
/**
 * 记录当前表的key值
 */
#define OBSERVABLE_TABLE_FLAG "__OATF"
/**
 * 插入、删除操作移动table值时，忽略监听
 */
#define OBSERVER_TABLE_IGNORE_FLAG "__OTKT_F"
/**
 * 插入、删除、更新操作类型：insert:1, remove:2, updata:3
 */
#define OBSERVER_TABLE_CHANGE_TYPE_FLAG "__OTKT_TYPE_F"

#define LUA_INDEX_KEY "__index"
#define LUA_NEWINDEX_KEY "__newindex"
#define LUA_PAIRS_KEY "__pairs"
#define LUA_IPAIRS_KEY "__ipairs"
#define LUA_LEN_KEY "__len"

#define LUA_CHANGE_TYPE_INSERT 1
#define LUA_CHANGE_TYPE_REMOVE 2
#define LUA_CHANGE_TYPE_UPDATE 3
/**
 * watchTable回调函数type，返回change_key、type、old、new
 */
#define CALLBACK_PARAMS_TYPE 4

#ifdef J_API_INFO
#define CHECK_STACK_START(L) int _old_top = lua_gettop((L));
#define CHECK_STACK_END(L, l) if (lua_gettop((L)) - _old_top != l) \
    luaL_error((L), "%s top error, old: %d, new: %d",__FUNCTION__, _old_top, lua_gettop((L)));
#define CHECK_STACK_END_O(L, ot, l) if (lua_gettop((L)) - (ot) != l) \
    LOGE("o %s top error, old: %d, new: %d",__FUNCTION__, (ot), lua_gettop((L)));
#else
#define CHECK_STACK_START(L)
#define CHECK_STACK_END(L, l)
#define CHECK_STACK_END_O(L, ot, l)
#endif

#define IPC_RESULT(ret) const char *msg;\
                        switch (ret) {\
                            case IPC_MEM_ERROR:\
                                msg = "no memory";\
                                break;\
                            default:\
                                msg = "only support type nil|boolean|number|string|table";\
                                break;\
                        }

///<editor-fold desc="vm key 相关操作">

static int _str_equals(const void *a, const void *b) {
    const char *ba = (const char *) a;
    const char *bb = (const char *) b;
    while (*ba && *bb) {
        if (*ba != *bb) return 0;
        ba++;
        bb++;
    }
    if (*ba != *bb) return 0;
    return 1;
}

static void saveVmKey(lua_State *L, const char *key) {
    List *list = map_get(instance->vm_key, L);
    if (!list) {
        list = list_new(instance->alloc, 20, 0);
        if (!list) {
            luaL_error(L, "save vm(%p) key(%s) failed, no memory!", L, key);
            return;
        }
        list_set_equals(list, _str_equals);
        map_put(instance->vm_key, (void *) L, list);
    } else if (list_index(list, (void *) key) < list_size(list)) {
        return;
    }
    list_add(list, copystr(key));
}

/**
 * 1、从key_observable删除虚拟机相关缓存
 * 2、从key_observer删除虚拟机相关缓存
 */
int _freeTraverse(const void *value, void *ud) {
    /// step 1:
    if (instance->key_observable) {
        map_remove(instance->key_observable, value);
    }
    /// step 2:
    if (ud && instance->key_observer) {
        List *list = map_get(instance->key_observer, value);
        if (list) {
            list_remove_obj(list, ud);
            if (!list_size(list)) {
                map_remove(instance->key_observer, value);
                list_free(list);
            }
        }
    }
    instance->alloc((void *) value, (strlen(value) + 1) * sizeof(char), 0);
    return 0;
}

/**
 * 检查argo instance是否初始化
 */
static inline void _check_instance(lua_State *L) {
    if (!instance) {
        luaL_error(L, "argo databinding instance not init");
    }
}
///</editor-fold>

///<editor-fold desc="observable table">
///<editor-fold desc="callback">
/**
 * 存储原虚拟机和新旧数据栈位置
 */
typedef struct DataContainer {
    lua_State *L;
    int oldIndex;
    int newIndex;
    const char *key;
    const char *parent;
    int changeType;
} _DC;

/**
 * 调用lua函数通知数据改变
 * @param l 目标虚拟机
 * @param ud _DC
 *
 * 1、OTK中查找对应lua函数
 * 2、复制旧值和新值
 * 3、调用函数
 */
static int _callbackTraverseReal(const void *l, void *ud, char *table_key) {
    lua_State *dest = (lua_State *) l;
    CheckThread(dest);
    _DC *dc = (_DC *) ud;
    CHECK_STACK_START(dc->L);

    int oldTop = lua_gettop(dest);

    /// step 1
    lua_getglobal(dest, table_key);
    if (!lua_istable(dest, -1)) {
        lua_settop(dest, oldTop);
        CHECK_STACK_END_O(dest, oldTop, 0);
        CHECK_STACK_END(dc->L, 0);
        return 0;
    }
    /// -1: OTK
    if (table_key == OBSERVER_TABLE_INNER_KEY) {
        lua_getfield(dest, -1, dc->parent);
        if (!lua_istable(dest, -1)) {
            lua_settop(dest, oldTop);
            CHECK_STACK_END_O(dest, oldTop, 0);
            CHECK_STACK_END(dc->L, 0);
            return 0;
        }
    } else {
        lua_getfield(dest, -1, dc->key);
        if (!lua_istable(dest, -1)) {
            lua_settop(dest, oldTop);
            CHECK_STACK_END_O(dest, oldTop, 0);
            CHECK_STACK_END(dc->L, 0);
            return 0;
        }
    }
    lua_remove(dest, -2);
    /// -1:{function, params}
    lua_rawgeti(dest, -1, 1);
    if (!lua_isfunction(dest, -1)) {
        lua_settop(dest, oldTop);
        CHECK_STACK_END_O(dest, oldTop, 0);
        CHECK_STACK_END(dc->L, 0);
        return 0;
    }
    /// -1: function, -2: {function, params}
    lua_rawgeti(dest, -2, 2);
    if (!lua_isnumber(dest, -1)) {
        lua_settop(dest, oldTop);
        CHECK_STACK_END_O(dest, oldTop, 0);
        CHECK_STACK_END(dc->L, 0);
        return 0;
    }
    int params = lua_tointeger(dest, -1);
    lua_pop(dest, 1);
    lua_remove(dest, -2);
    /// -1: function

    int oldIndexOffset = 1; //-1: function，同一虚拟机情况
    /// step 2
    if (table_key == OBSERVER_TABLE_INNER_KEY) {//function(type, key, new, old)
        lua_pushinteger(dest, dc->changeType);//-1:type, -2:function
        lua_pushstring(dest, dc->key);//-1:change_key, -2:type, -3:function
        oldIndexOffset += 2;
    }

    if (params > 0) {
        int ret;
        if (dc->L != dest) {
            ret = ipc_copy(dc->L, dc->newIndex, dest);
        } else {
            lua_pushvalue(dest, dc->newIndex);
            oldIndexOffset += 1;
            ret = IPC_OK;
        }
        if (ret != IPC_OK) {
            lua_settop(dest, oldTop);
            CHECK_STACK_END_O(dest, oldTop, 0);
            CHECK_STACK_END(dc->L, 0);
            IPC_RESULT(ret);
            luaL_error(dc->L, "callback by table key(\"%s\") failed, msg: %s, target(%s): %s",
                    table_key, msg, luaL_typename(dc->L, -1), luaL_tolstring(dc->L, -1, NULL));
            return 1;
        }
    }
    if (params > 1) {
        int ret;
        if (dc->L != dest) {
            ret = ipc_copy(dc->L, dc->oldIndex, dest);
        } else {
            lua_pushvalue(dest, dc->oldIndex - oldIndexOffset);//-1: new -2: function -3 *****
            ret = IPC_OK;
        }
        if (ret) {
            lua_settop(dest, oldTop);
            CHECK_STACK_END_O(dest, oldTop, 0);
            CHECK_STACK_END(dc->L, 0);
            IPC_RESULT(ret);
            luaL_error(dc->L, "callback by table key(\"%s\") failed, msg: %s, target(%s): %s",
                       table_key, msg, luaL_typename(dc->L, -1), luaL_tolstring(dc->L, -1, NULL));
            return 1;
        }
    }

    ///-1:old, -2:new, -3:change_key, -4:type, -5:function
    /// step 3
    lua_call(dest, params, 0);
    lua_settop(dest, oldTop);
    CHECK_STACK_END_O(dest, oldTop, 0);
    CHECK_STACK_END(dc->L, 0);
    return 0;
}

static int _callbackTraverse(const void *l, void *ud) {
    return _callbackTraverseReal(l, ud, OBSERVER_TABLE_KEY);
}

static int _callbackTableInnerTraverse(const void *l, void *ud) {
    return _callbackTraverseReal(l, ud, OBSERVER_TABLE_INNER_KEY);
}
// </editor-fold>

// <editor-fold desc="i/pairs start">
/**
 * oldtable被新表包装，无法i\pairs()
 * 这里在原表中插入代理方法。替换为oldtable
 */
static void insertFunction(lua_State *L, const char *method, lua_CFunction iter) {
    CHECK_STACK_START(L);
    if (!luaL_getmetafield(L, -2, method)) {  /* no metamethod? */
        lua_pushstring(L, method);
        lua_pushcfunction(L, iter);  /* will return generator, */
        lua_rawset(L, -3);
    }
    CHECK_STACK_END(L, 0);
}

static int ipairsaux(lua_State *L) {
    int i = luaL_checkint(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    i++;  /* next value */

    lua_pushinteger(L, i);
    lua_rawgeti(L, 1, i);
    return (lua_isnil(L, -1)) ? 1 : 2;
}

static int luaB_next(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);  /* create a 2nd argument if there isn't one */

    if (lua_next(L, 1))
        return 2;
    else {
        lua_pushnil(L);
        return 1;
    }
}

static int pairsmeta(lua_State *L, int iszero, lua_CFunction iter) {
    lua_getmetatable(L, 1);
    lua_pushcfunction(L, iter);
    lua_getfield(L, -2, LUA_INDEX_KEY);

    if (iszero) lua_pushinteger(L, 0);  /* and initial value */
    else lua_pushnil(L);

    // -1: key -2: oldtable -3: func -4: metatable -5: source table
    return 3;
}

/**
 *   代理i/pairs方法
 */
static int luaB_pairs(lua_State *L) {
    return pairsmeta(L, 0, luaB_next);
}

static int luaB_ipairs(lua_State *L) {
    return pairsmeta(L, 1, ipairsaux);
}
///</editor-fold>

///<editor-fold desc="mock other function">
/**
 * __newindex对应的lua函数
 * 参数为: table,参数名称,值
 * upvalue: parentkey
 * 1、拿到原值
 * 2、设置新值
 * 3、查找callback，并回调
 */
static int __newindexCallback(lua_State *L) {
    CHECK_STACK_START(L);
    /// step 1
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, LUA_INDEX_KEY);

    /// -1: source table -2:metatable
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    /// -1: source data ; -2: source table -3:metatable

    /// step 2
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_settable(L, -4);
    /// -1: source data ; -2: source table -3:metatable

    lua_getfield(L, -3,
                 OBSERVER_TABLE_IGNORE_FLAG);//-1:flag -2: source data ; -3: source table -4:metatable
    if (lua_toboolean(L, -1) == 1) {
        lua_pop(L, 4);
        CHECK_STACK_END(L, 0);
        return 0;
    }
    lua_pop(L, 1);

    int changeType = 0;
    lua_getfield(L, -3,
                 OBSERVER_TABLE_CHANGE_TYPE_FLAG);//-1:changeType -2: source data ; -3: source table -4:metatable
    if (lua_isnumber(L, -1)) {
        changeType = lua_tointeger(L, -1);//操作类型
    }
    lua_pop(L, 1);

    /// step 3
    int idx = lua_upvalueindex(1);
    const char *parent = luaL_checkstring(L, idx);
    const size_t LEN = 200;
    char key[LEN];
    lua_pushvalue(L, 2);
    join_3string(parent, ".", lua_tostring(L, -1), key, LEN);
    lua_pop(L, 1);
    List *list = map_get(instance->key_observer, key);

    if (list) {
        _DC ud = {L, -1, 3, key, parent, changeType};
        list_traverse(list, _callbackTraverse, &ud);
    }

    //判断是否有监听parent字段，
    list = map_get(instance->key_observer, parent);
    if (list) {
        _DC ud = {L, -1, 3, key, parent, changeType};
        list_traverse(list, _callbackTableInnerTraverse, &ud);
    }
    lua_pop(L, 3);
    CHECK_STACK_END(L, 0);

    return 0;
}

/**
 * __len对应的lua函数
 * 参数为: table
 * 1、拿到原值
 * 2、获取长度
 */
static int __lenFunction(lua_State *L) {
    CHECK_STACK_START(L);
    /// step 1
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, LUA_INDEX_KEY);
    lua_remove(L, -2);
    /// -1: source table
    /// step 2
    size_t len = lua_objlen(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, len);
    CHECK_STACK_END(L, 1);

    return 1;
}
///</editor-fold>

// <editor-fold desc="observable table">
/**
 * 使用新表代替旧表，设置flag、设置__index为原表、__newindex为callback函数，设置metatable
 * 遍历子节点，并将所有table都设置上
 */
static void createObservableTable(lua_State *L, const char *key, int tableIndex, int check) {
    CHECK_STACK_START(L);
    if (check) {
        lua_getfield(L, tableIndex, OBSERVABLE_TABLE_FLAG);
        if (!lua_isnil(L, -1)) {
            lua_pop(L, 1);
            CHECK_STACK_END(L, 0);
            luaL_error(L, "不能将已经绑定过的table");
            return;
        }
        lua_pop(L, 1);
    }

    /// 遍历子节点，若子节点是table，则将它改成可观察表
    int realIndex = tableIndex < 0 ? lua_gettop(L) + tableIndex + 1 : tableIndex;
    const int SIZE = 300;
    int keyType;
    char childKey[SIZE] = {0};
    lua_Number kn;
    lua_Integer ki;
    lua_pushnil(L);
    while (lua_next(L, realIndex)) {
        if (lua_istable(L, -1)) {
            keyType = lua_type(L, -2);
            if (keyType == LUA_TNUMBER) {
                kn = lua_tonumber(L, -2);
                ki = lua_tointeger(L, -2);
                if (kn == ki) {
                    format_string(childKey, SIZE, "%s.%d", key, ki);
                } else {
                    format_string(childKey, SIZE, "%s.%f", key, kn);
                }
            } else {
                join_3string(key, ".", lua_tostring(L, -2), childKey, SIZE);
            }
            lua_pushvalue(L, -2);
            /// -1: key, -2 value table, -3: key
            createObservableTable(L, childKey, -2, 1);
            /// -1: new talbe, -2: key, -3: value table, -4: key
            lua_rawset(L, realIndex);
        }
        lua_pop(L, 1);
    }

    /// 创建需要返回的table
    lua_newtable(L);
    /// 创建metatable
    /// { __OATF = true, __index = oldtable, __newindex = callback, __len = __lenFunction}
    lua_createtable(L, 0, 3);

    /// __OATF = true
    lua_pushstring(L, OBSERVABLE_TABLE_FLAG);
    lua_pushboolean(L, 1);
    lua_rawset(L, -3);
    /// __index = oldtable
    lua_pushstring(L, LUA_INDEX_KEY);
    lua_pushvalue(L, realIndex);
    lua_rawset(L, -3);
    /// __newindex = callback
    lua_pushstring(L, LUA_NEWINDEX_KEY);
    lua_pushstring(L, key);
    lua_pushcclosure(L, __newindexCallback, 1);
    lua_rawset(L, -3);
    /// __len = __lenFunction
    lua_pushstring(L, LUA_LEN_KEY);
    lua_pushcfunction(L, __lenFunction);
    lua_rawset(L, -3);

    //插入ipairs、pairs方法. lbaselib.c的相关方法会掉到这里
    insertFunction(L, LUA_IPAIRS_KEY, luaB_ipairs);
    insertFunction(L, LUA_PAIRS_KEY, luaB_pairs);

    /// -1: metatable -2: table
    lua_setmetatable(L, -2);
    CHECK_STACK_END(L, 1);
}

/**
 * 在虚拟机中的OATK表中找到对应key的表，并放入栈顶
 */
static inline void getObservableTable(lua_State *L, const char *key) {
    CHECK_STACK_START(L);
    lua_getglobal(L, OBSERVABLE_TABLE_KEY);
    if (!lua_istable(L, -1)) {
        if (!lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        CHECK_STACK_END(L, 1);
        /// 栈顶是nil
        return;
    }
    /// -1: OATK表
    lua_getfield(L, -1, key);
    lua_remove(L, -2);
    CHECK_STACK_END(L, 1);
}
// </editor-fold>
// </editor-fold>

/**
 * 针对观察者，在虚拟机全局表中设定一个特殊表来记录(OTK)
 * 记录方式为: key->{function, type}
 * 并且记录key对应的虚拟机 (key_observer)
 * 记录方式为: key->List(lua_State)
 *
 * @param key model名称
 * @param type 类型，取值范围[0,2] (对应后续回调参数个数) | CALLBACK_PARAMS_TYPE(watchTable回调函数type，返回change_key、type、old、new)
 * @param functionIndex lua回调函数栈位置
 * @param table_key 两种类型 OBSERVER_TABLE_KEY|OBSERVER_TABLE_INNER_KEY
 * @type 返回参数数量
 */
static void
DB_Watch_Inner(lua_State *L, const char *key, int type, int functionIndex, const char *table_key) {
    CHECK_STACK_START(L);
    /// 记录回调
    lua_getglobal(L, table_key);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 5);
        lua_pushvalue(L, -1);
        lua_setglobal(L, table_key);
    }
    /*OTK[key] = {callback, type}*/
    /// stack: OTK
    lua_createtable(L, 2, 0);
    lua_pushvalue(L, functionIndex);
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, type);
    lua_rawseti(L, -2, 2);
    /// -1: {function, type}, -2: OTK
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
    /// stack: none

    /*记录观察者虚拟机 (instance->key_observer[key]).add(L)*/
    /// stack: OTK
    List *list = map_get(instance->key_observer, key);
    if (!list) {
        list = list_new(instance->alloc, 5, 0);
        if (!list) {
            CHECK_STACK_END(L, 0);
            luaL_error(L, "cannot watch \"%s\" because no memory");
            return;
        }
        map_put(instance->key_observer, (void *) copystr(key), list);
    } else if (list_index(list, L) < list_size(list)) {
        /// 已存储
        CHECK_STACK_END(L, 0);
        return;
    }
    list_add(list, L);
    /*记录虚拟机中所有的key值 map[L] = List(keys)*/
    saveVmKey(L, key);
    CHECK_STACK_END(L, 0);
}

/**
 * 查找OATK表中，找到key对应的表，并放入栈顶
 * 正确返回，栈顶为key(nil) -2为table
 * 如果未找到，返回NULL，并将错误信息以string的形式push到L栈顶
 */
static lua_State *DB_findTarget(lua_State *L, const char *key) {
    const size_t SIZE = 100;
    char realKey[SIZE] = {0};
    size_t index = 0;
    size_t keyLen = strlen(key);
    /// step 1
    char *dot = strchr(key, '.');
    if (dot) {
        size_t len = dot - key;
        index = len + 1;
        memcpy(realKey, key, len);
    } else {
        memcpy(realKey, key, keyLen);
    }
    lua_State *target = (lua_State *) map_get(instance->key_observable, (void *) realKey);
    if (!target) {
        lua_pushfstring(L, "key \"%s\"(from key \"%s\") has no binding data", realKey, key);
        return NULL;
    }
    CheckThread(target);
    CHECK_STACK_START(target);
    /// step 2
    getObservableTable(target, (const char *) realKey);
    if (!lua_istable(target, -1)) {
        lua_pushfstring(L, "binding data \"%s\" is not a table, but a \"%s\"",
                        (const char *) realKey,
                        luaL_typename(target, -1));
        lua_pop(target, 1);
        CHECK_STACK_END(target, 0);
        return NULL;
    }
    /// 多级情况,index= '.'后第一位
    if (index) {
        size_t startIndex = index;
        int numKey;
        while (index < keyLen) {
            /// 多级的中间key，取得中间key对应的table
            if (key[index] == '.') {
                realKey[index - startIndex] = '\0';
                /// 数组类型
                if (string_to_int(realKey, &numKey)) {
                    lua_pushinteger(target, numKey);
                    lua_gettable(target, -2);
                } else {
                    lua_getfield(target, -1, realKey);
                }
                lua_remove(target, -2);
                if (!lua_istable(target, -1)) {
                    char temp[SIZE] = {0};
                    memcpy(temp, key, index);
                    lua_pushfstring(L, "binding data \"%s\" is not a table, but a \"%s\"",
                                    temp,
                                    luaL_typename(target, -1));
                    lua_pop(target, 1);
                    CHECK_STACK_END(target, 0);
                    return NULL;
                }

                startIndex = ++index;
                continue;
            }
            realKey[index - startIndex] = key[index];
            index++;
        }
        /// 多级中最后一级key
        realKey[index - startIndex] = '\0';

        /// 数组类型
        if (string_to_int(realKey, &numKey)) {
            lua_pushinteger(target, numKey);
        } else {
            lua_pushstring(target, realKey);
        }
        /// -1: key -2:table
        CHECK_STACK_END(target, 2);
        return target;
    } else {
        lua_pushnil(target);
        CHECK_STACK_END(target, 2);
        return target;
    }
}

/**
 * 保存：在原表中保存flag
 * metaIndex < 0
 */
static void saveFlagInMetaTable(lua_State *target, const char *flag, int value, int metaIndex) {
    CHECK_STACK_START(target);
    ///-1:metatable
    lua_pushstring(target, flag);//操作类型保存

    if (strcmp(flag, OBSERVER_TABLE_IGNORE_FLAG) == 0) {
        lua_pushboolean(target, value);
    } else if (strcmp(flag, OBSERVER_TABLE_CHANGE_TYPE_FLAG) == 0) {
        lua_pushinteger(target, value);
    } else {
        lua_pop(target, 1);
        CHECK_STACK_END(target, 0);
        return;
    }
    lua_settable(target, metaIndex - 2);
    CHECK_STACK_END(target, 0);
}

/**
 * 移除：在原表中的flag
 */
static void removeFlagInMetaTable(lua_State *target, const char *flag, int metaIndex) {
    CHECK_STACK_START(target);
    ///-1:metatable
    lua_pushstring(target, flag);//移除操作类型
    if (strcmp(flag, OBSERVER_TABLE_IGNORE_FLAG) == 0) {
        lua_pushboolean(target, 0);
    } else if (strcmp(flag, OBSERVER_TABLE_CHANGE_TYPE_FLAG)==0) {
        lua_pushnil(target);
    } else {
        lua_pop(target, 1);
        CHECK_STACK_END(target, 0);
        return;
    }

    lua_settable(target, metaIndex - 2);
    CHECK_STACK_END(target, 0);
}

/**
 * DB_Insert方法中，对插入的位置元素赋值，并替换元表
 */
static inline void replaceMetaTable(lua_State *L, lua_State *target, const char *key, int insertindex) {
    CHECK_STACK_START(target);
    ///-1:value -2:metatable -3: childtable -4:table
    if (lua_istable(L, -1)) {//更新table，替换为observableTable
        createObservableTable(target, key, -1, 0);
        ///-1:newTable -2:value -3:metatable -4: childtable -5:table
        lua_pushinteger(target,
                        insertindex);//-1:insertindex -2:newTable -3:value -4:metatable -5:childtable -6:table
        lua_pushvalue(target,
                      -2);//-1:newTable -2:insertindex -3:newTable -4:value -5:metatable -6:childtable -7:table
        lua_settable(target, -6);
        lua_pop(target, 2);//-1:metatable -2:childtable -3:table
    } else {
        lua_pushinteger(target,
                        insertindex);//-1:insertindex -2:value -3:metatable -4:childtable -5:table
        lua_pushvalue(target,
                      -2);//-1:value -2:insertindex -3:value -4:metatable -5:childtable -6:table
        lua_settable(target, -5);
        lua_pop(target, 1);
    }
    CHECK_STACK_END(target, 0);
}

// <editor-fold desc="instance 操作">
/**
 * 1、从vm_key中查找虚拟机相关缓存
 * 2、遍历缓存key，并从两个表中删除
 * 3、释放list
 */
void DB_Close(lua_State *L) {
    if (!instance) return;
    List *list = map_remove(instance->vm_key, L);
    if (!list)
        return;
    CHECK_STACK_START(L);
    list_traverse(list, _freeTraverse, L);
    list_free(list);
    CHECK_STACK_END(L, 0);
}

/**
 * 1、将key和虚拟机关联起来放入 key_observable 中
 * 2、在虚拟机中将key和table关联，并替换table，放入栈顶
 */
void DB_Bind(lua_State *L, const char *key, int valueIndex) {
    _check_instance(L);
    if (strchr(key, '.')) {
        luaL_error(L, "cannot has '.' in key \"%s\"", key);
    }
    /// step 1
    char *copy_str = copystr(key);
    lua_State *old = (lua_State *) map_put(instance->key_observable, (void *) copy_str, L);
    if (old) {
        instance->alloc(copy_str, (strlen(copy_str) + 1) * sizeof(char), 0);
        /// 如果是覆盖的情况
        if (old != L) {
            luaL_error(L, "key \"%s\" has already bind data", key);
            return;
        }
    } else {
        saveVmKey(L, key);
    }
    ///step 2
    /**
     * 针对需要观察的表，在虚拟机全局表中设定一个特殊表来记录(OATK)，并将新表放入栈顶
     * 记录方式为: key->table
     * 1、先使用新表代替旧表，设置flag、设置__index为原表、__newindex为callback函数，设置metatable
     * 2、在全局表中记录key->table
     */
    CHECK_STACK_START(L);
    /// step 1
    lua_getfield(L, valueIndex, OBSERVABLE_TABLE_FLAG);
    if (!lua_isnil(L, -1)) {
        /// 这个table已经被绑定了，不用管
        lua_pop(L, 1);
        lua_pushvalue(L, valueIndex);
        CHECK_STACK_END(L, 1);
        return;
    }
    lua_pop(L, 1);
    createObservableTable(L, key, valueIndex, 0);
    ///-1: newtable

    /// step 2
    lua_getglobal(L, OBSERVABLE_TABLE_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 5);
        lua_pushvalue(L, -1);
        lua_setglobal(L, OBSERVABLE_TABLE_KEY);
    }
    /// -1: OATK表, -2:newtable
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
    CHECK_STACK_END(L, 1);
}

/**
 * 监听key对应的value
 */
void DB_Watch(lua_State *L, const char *key, int type, int functionIndex) {
    _check_instance(L);
    DB_Watch_Inner(L, key, type, functionIndex, OBSERVER_TABLE_KEY);
}

/**
 * 监听table 内部
 * function(type, key, new, old)
 */
void DB_WatchTable(lua_State *L, const char *key, int functionIndex) {
    _check_instance(L);
    DB_Watch_Inner(L, key, CALLBACK_PARAMS_TYPE, functionIndex, OBSERVER_TABLE_INNER_KEY);
}

/**
 * 释法监听table
 */
void DB_UnWatch(lua_State *L, const char *key) {
    _check_instance(L);
    CHECK_STACK_START(L);
    //移除key_observer的keyList中的虚拟机缓存
    if (instance->key_observer) {
        List *list = map_get(instance->key_observer, key);
        if (list) {
            list_remove_obj(list, L);
            if (!list_size(list)) {
                map_remove(instance->key_observer, key);
                list_free(list);
            }
        }
    }
    //移除vm_key中的key缓存
    List *keyList = map_get(instance->vm_key, L);
    if (keyList) {
        size_t index = list_index(keyList, (void *) key);
        if (index < list_size(keyList)) {//key存在
            void *realkey = list_get(keyList, index);
            list_remove(keyList, index);

            instance->alloc(realkey, (strlen(realkey) + 1) * sizeof(char), 0);
        }

        if (!list_size(keyList)) {
            map_remove(instance->vm_key, L);
            list_free(keyList);
        }
    }
    CHECK_STACK_END(L, 0);
}

/**
 * 1、查找被观察数据，key值可能需要拆分
 * 2、若查找到，通过lua rpc将数据复制到被观察虚拟机中，并设置值
 */
void DB_Update(lua_State *L, const char *key, int valueIndex) {
    _check_instance(L);
    CHECK_STACK_START(L);
    lua_State *target = DB_findTarget(L, key);
    if (!target) {
        lua_error(L);
        return;
    }
    CheckThread(target);
#ifdef J_API_INFO
    int _tot = lua_gettop(target) - 2;
#endif
    ///target -1: key -2:table
    if (lua_isnil(target, -1)) {
        lua_pop(target, 2);
        CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
        luaL_error(L, "cannot update \"%s\"(first level) binding data!", key);
        return;
    }

    lua_getmetatable(target, -2);//target -1:metatable -2: key -3:table
    saveFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, LUA_CHANGE_TYPE_UPDATE, -1);
    lua_pop(target, 1);
    ///target -1:key -2:table
    if (L == target) {
        lua_pushvalue(L, valueIndex);
    } else {
        int ret = ipc_copy(L, valueIndex, target);
        if (ret != IPC_OK) {
            lua_pop(target, 2);
            CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
            CHECK_STACK_END_O(target, _tot, 0);
#endif
            IPC_RESULT(ret);
            luaL_error(L, "update by key(\"%s\") failed, msg: %s, target(%s): %s",
                       key, msg, luaL_typename(target, -1), luaL_tolstring(target, -1, NULL));
            return;
        }
    }

    //-1:value -2: key -3:table
    if (lua_istable(L, -1)) {//更新table，替换为observableTable
        lua_pushvalue(target, -2);//-1:key -2:value -3: key -4:table
        createObservableTable(target, key, -2, 0);
        ///-1:newTable -2:key -3:value -4: key -5:table
        lua_settable(target, -5);//-1:value -2: key -3:table
        lua_pop(target, 2);
    } else {
        lua_settable(target, -3);
    }

    ///-1:table
    lua_getmetatable(target, -1);//-1:metatable -2:table
    removeFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, -1);

    lua_pop(target, 2);
    CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
    CHECK_STACK_END_O(target, _tot, 0);
#endif
}

/**
 * 1、查找被观察数据，key值可能需要拆分
 * 2、若查找到，通过lua rpc将数据复制到被观察虚拟机中
 */
void DB_Get(lua_State *L, const char *key) {
    _check_instance(L);
    CHECK_STACK_START(L);
    lua_State *target = DB_findTarget(L, key);
    if (!target) {
        lua_error(L);
        return;
    }
    CheckThread(target);
#ifdef J_API_INFO
    int _tot = lua_gettop(target) - 2;
#endif
    /// -1: realKey -2:table
    if (lua_isnil(target, -1)) {
        lua_pop(target, 1);
        lua_pushvalue(target, -1);
    } else {
        lua_gettable(target, -2);
    }
    /// -1: value -2:table
    ///table 特殊处理，提取出metatable的__index
    if (lua_istable(target, -1)
        && lua_getmetatable(target, -1)) {
        /// -1:metatable -2: value -3: table
        lua_getfield(target, -1, LUA_INDEX_KEY);
        lua_remove(target, -2);//remove metatalbe
        lua_remove(target, -2);//remove value
    }
    /// -1: value -2:table
    if (target != L) {//不同虚拟机
        int ret = ipc_copy(target, -1, L);
        if (ret != IPC_OK) {
            lua_pop(target, 2);
            CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
            CHECK_STACK_END_O(target, _tot, 0);
#endif
            IPC_RESULT(ret);
            luaL_error(L,
                    "get by key(\"%s\") failed, msg: %s, target(%s): %s",
                    key, msg, luaL_typename(target, -1), luaL_tolstring(target, -1, NULL));
            return;
        }
        lua_pop(target, 2);
        CHECK_STACK_END(L, 1);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
    } else {
        lua_remove(L, -2); // -1: childtable
        CHECK_STACK_END(L, 1);
    }
}

/**
 * 1、查找被观察数据，key值可能需要拆分
 * 2、若查找到，通过lua rpc将数据复制到被观察虚拟机中
 */
void DB_Insert(lua_State *L, const char *key, int insertindex, int valueIndex) {
    _check_instance(L);
    CHECK_STACK_START(L);
    lua_State *target = DB_findTarget(L, key);
    if (!target) {
        lua_error(L);
        return;
    }
    CheckThread(target);
#ifdef J_API_INFO
    int _tot = lua_gettop(target) - 2;
#endif
    /// -1: realKey -2:table
    if (lua_isnil(target, -1)) {
        lua_pop(target, 2);
        CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
        luaL_error(L, "cannot insert \"%s\"(first level) binding data!", key);
        return;
    }
    lua_gettable(target, -2);

    /// -1: childtable -2:table
    int curlen = luaL_len(target, -1);
    if (insertindex < 0 || insertindex > curlen) {//末尾插入
        insertindex = curlen + 1;
        lua_getmetatable(target, -1);//-1:metatable -2: childtable -3:table
        saveFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, LUA_CHANGE_TYPE_INSERT, -1);
        if (L == target) {
            lua_pushvalue(L, valueIndex);
        } else {
            int ret = ipc_copy(L, valueIndex,
                               target);//-1:value -2:metatable -3: childtable -4:table
            if (ret != IPC_OK) {
                lua_pop(target, 3);
                CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
                CHECK_STACK_END_O(target, _tot, 0);
#endif
                IPC_RESULT(ret);
                luaL_error(L, "insert by key(\"%s\") failed, msg: %s, target(%s): %s",
                           key, msg, luaL_typename(target, -1), luaL_tolstring(target, -1, NULL));
                return;
            }
        }

        replaceMetaTable(L, target, key, insertindex);//替换元表，监听table
        ///-1:metatable -2:childtable -3:table
        removeFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, -1);
        lua_pop(target, 3);
        CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
        return;
    }

    lua_getmetatable(target, -1);//-1:metatable -2: childtable -3:table
    saveFlagInMetaTable(target, OBSERVER_TABLE_IGNORE_FLAG, LUA_CHANGE_TYPE_INSERT, -1);
    lua_pop(target, 1);//-1: childtable -2:table

    /// -1: childtable -2:table
    for (int i = curlen + 1; i > insertindex; --i) {

        //向后移动值
        lua_pushinteger(target, i);//-1:toIndex -2: childtable -3:table
        lua_pushinteger(target, i - 1);//-1:fromIndex -2:toIndex -3: childtable -4:table
        lua_gettable(target, -3);//-1:value -2:toIndex -3: childtable -4:table
        lua_settable(target, -3);// -1: childtable -2:table

        //遍历到insert位置，赋值
        if (i - 1 == insertindex) {
            lua_getmetatable(target, -1);//-1:metatable -2: childtable -3:table
            removeFlagInMetaTable(target, OBSERVER_TABLE_IGNORE_FLAG, -1);
            saveFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, LUA_CHANGE_TYPE_INSERT,
                                -1);
            if (L == target) {
                lua_pushvalue(L, valueIndex);
            } else {
                ///-1:metatable -2: childtable -3:table
                int ret = ipc_copy(L, valueIndex,
                                   target);//-1:value -2:metatable -3: childtable -4:table
                if (ret != IPC_OK) {
                    lua_pop(target, 3);
                    CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
                    CHECK_STACK_END_O(target, _tot, 0);
#endif
                    IPC_RESULT(ret);
                    luaL_error(L, "insert by key(\"%s\") failed, msg: %s", key, msg);
                }
            }
            replaceMetaTable(L, target, key, insertindex);//替换元表，监听table
            removeFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, -1);

            lua_pop(target, 1);//-1: childtable -2:table
        }
    }
    lua_pop(target, 2);
    CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
    CHECK_STACK_END_O(target, _tot, 0);
#endif
}

/**
 * 1、查找被观察数据，key值可能需要拆分
 * 2、若查找到，通过lua rpc将数据复制到被观察虚拟机中
 */
void DB_Remove(lua_State *L, const char *key, int removeIndex) {
    _check_instance(L);
    CHECK_STACK_START(L);
    lua_State *target = DB_findTarget(L, key);
    if (!target) {
        lua_error(L);
        return;
    }
    CheckThread(target);
#ifdef J_API_INFO
    int _tot = lua_gettop(target) - 2;
#endif
    /// -1: realKey -2:table
    if (lua_isnil(target, -1)) {
        lua_pop(target, 2);
        CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
        luaL_error(L, "cannot remove \"%s\"(first level) binding data!", key);
        return;
    }
    lua_gettable(target, -2);

    lua_getmetatable(target, -1);//-1:metatable -2: childtable -3:table
    saveFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, LUA_CHANGE_TYPE_REMOVE, -1);

    ///-1:metatable -2: childtable -3:table
    int curlen = luaL_len(target, -2);

    lua_pushinteger(target, removeIndex);//-1:removeIndex -2:metatable -3: childtable -4:table
    lua_pushnil(target);//-1:nil -2:removeIndex -3:metatable -4: childtable -5:table
    lua_settable(target, -4);// -1:metatable -2: childtable -3:table

    removeFlagInMetaTable(target, OBSERVER_TABLE_CHANGE_TYPE_FLAG, -1);
    lua_pop(target, 1);//-1: childtable -2:table

    if (removeIndex == curlen) {//末尾移除
        lua_pop(target, 2);
        CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
        return;
    }

    lua_getmetatable(target, -1);//-1:metatable -2: childtable -3:table
    saveFlagInMetaTable(target, OBSERVER_TABLE_IGNORE_FLAG, LUA_CHANGE_TYPE_INSERT, -1);

    lua_pop(target, 1);//-1: childtable -2:table

    /// -1: childtable -2:table
    for (int i = removeIndex; i <= curlen; ++i) {
        if (i + 1 <= curlen) {//非最后
            lua_pushinteger(target, i);     //-1:toIndex -2:childtable -3:table
            lua_pushinteger(target, i + 1); //-1:fromIndex -2:toIndex -3:childtable -4:table
            lua_gettable(target, -3);       //-1:value -2:toIndex -3:childtable -4:table

            lua_settable(target, -3);       // -1: childtable -2:table

        } else {//最后一个
            lua_pushinteger(target, i); //-1:removeIndex -2:childtable -3:table
            lua_pushnil(target);        //-1:nil -2:removeIndex -3:childtable -4:table
            lua_settable(target, -3);
        }
    }

    lua_getmetatable(target, -1);//-1:metatable -2: childtable -3:table
    removeFlagInMetaTable(target, OBSERVER_TABLE_IGNORE_FLAG, -1);
    lua_pop(target, 3);
    CHECK_STACK_END(L, 0);
#ifdef J_API_INFO
    CHECK_STACK_END_O(target, _tot, 0);
#endif
}

/**
 * 1、查找被观察数据，key值可能需要拆分
 * 2、若查找到，通过lua rpc将数据复制到被观察虚拟机中
 */
void DB_Len(lua_State *L, const char *key) {
    _check_instance(L);
    CHECK_STACK_START(L);
    lua_State *target = DB_findTarget(L, key);
    if (!target) {
        lua_error(L);
        return;
    }
    CheckThread(target);
#ifdef J_API_INFO
    int _tot = lua_gettop(target) - 2;
#endif
    /// -1: realKey -2:table
    if (lua_isnil(target, -1)) {
        lua_pop(target, 1);
        lua_pushvalue(target, -1);
    } else {
        lua_gettable(target, -2);
    }

    /// -1: value -2:table
    ///table 特殊处理，提取出metatable的__index
    if (lua_istable(target, -1)
        && lua_getmetatable(target, -1)) {
        /// -1:metatable -2: value -3: table
        lua_getfield(target, -1, LUA_INDEX_KEY);
        lua_remove(target, -2);//remove metatalbe
        lua_remove(target, -2);//remove value
    }
    
    /// -1: childtable -2:table
    int len = luaL_len(target, -1);

    if (target != L) {//不同虚拟机
        lua_pushinteger(L, len);//-1:len
        lua_pop(target, 2);
        CHECK_STACK_END(L, 1);
#ifdef J_API_INFO
        CHECK_STACK_END_O(target, _tot, 0);
#endif
    } else {
        lua_pop(L, 2);
        lua_pushinteger(L, len);// -1:len
        CHECK_STACK_END(L, 1);
    }
}
// </editor-fold>

// <editor-fold desc="instance free 操作">
/**
 * 释放map中key值内存
 */
static void _free_key(void *key) {
    if (instance) {
        const char *s = (const char *) key;
        size_t len = strlen(s) + 1;
        instance->alloc(key, len * sizeof(char), 0);
    } else {
        free(key);
    }
}

/**
 * 释放map中list值内存
 */
static void _free_list(void *v) {
    List *l = (List *) v;
    list_free(l);
}

static unsigned int _vm_hash(const void *vm) {
    return (unsigned int) vm;
}

static void _free_vm_list(void *v) {
    List *l = (List *) v;
    list_traverse(l, _freeTraverse, NULL);
    list_free(l);
}

/**
 * 释放instance
 */
void DataBindFree() {
    if (instance) {
        if (instance->key_observable) {
            map_free(instance->key_observable);
        }
        if (instance->key_observer) {
            map_free(instance->key_observer);
        }
        if (instance->vm_key) {
            map_free(instance->vm_key);
        }
        instance->alloc(instance, sizeof(DataBind), 0);
        instance = NULL;
    }
}
// </editor-fold>

int DataBindInit(D_malloc m) {
    if (instance)
        return 0;
    instance = (DataBind *) m(NULL, 0, sizeof(DataBind));
    if (!instance) {
        return 1;
    }
    instance->alloc = m;
    Map *temp = map_new(m, 10);
    if (!temp || map_ero(temp)) {
        if (temp) {
            map_free(temp);
        }
        DataBindFree();
        return 1;
    }
    map_set_free(temp, _free_key, NULL);
    instance->key_observable = temp;

    temp = map_new(m, 50);
    if (!temp || map_ero(temp)) {
        if (temp) {
            map_free(temp);
        }
        DataBindFree();
        return 1;
    }
    map_set_free(temp, _free_key, _free_list);
    instance->key_observer = temp;

    temp = map_new(m, 10);
    if (!temp || map_ero(temp)) {
        if (temp) {
            map_free(temp);
        }
        DataBindFree();
        return 1;
    }
    map_set_hash(temp, _vm_hash);
    map_set_equals(temp, NULL);
    map_set_free(temp, NULL, _free_vm_list);
    instance->vm_key = temp;

    return 0;
}
