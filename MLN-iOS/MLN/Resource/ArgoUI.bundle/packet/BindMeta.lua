---
--- Generated by EmmyLua(https://github.com/EmmyLua)
--- Created by sun.
--- DateTime: 2020-05-25 19:18
---

local __remove = "__removew"
local __watch = "__watch"
local __get = "__get"
local __path = "__path"
local __ishook = "__ishook"
local __kvoname = "__kvoname" -- path
local __set = "__set"
local __asize = "__asize" --获取数组大小
local __vv = "__vv" -- 实际存的值
local __ignore = "__ignore" -- watch忽略
local __greal = "__greal" -- 获取值并缓存
local __rreal = "__rreal" -- 移除缓存
local __ci = "__ci" -- cell item

-- debug
--local __ck = "__ck" -- 当前key
--local __pk = "__pk" -- 前一个key
--local __bt = "__bt"
local WATCH = "watch" -- prevew中使用
local FOREACH = "forEach"
local __b_G = "G_G"
local __b_G_ = "G_G."

local _kpathCache = {} -- {path = MetaTab }
local _watchCache = {} -- {watchid1 = {path, func1}, watchid2 = {path, func2}, ...}
local _debugpwacths = {} --preview {path = {func1, funct2, ...}}
local _ckeyTabCaches = {} -- ckey对应缓存
local debug_preview_open = false
local debug_preview_watch = false
_watchIds = {} -- watch remove
_cellBinds = {} -- list cell binds
local _foreachCaches = {} -- 用于foreach中的缓存

__open_combine_data__ = true --- 开启list cell 一次性获取全部item数据
__open_cell_data__ = false --- 是否开启cell data 获取方法

-- 创建弱引用表
function createWeakT(mode) --"k" / "v" / "kv"
    return setmetatable({}, { __mode = mode})
end

-------------------------------------------------------------------------------------------------------------
------------- 空表初始化               --------------------------
-----------------------------------------------------------------------------------------------------------
local _emptyTab = {}
setmetatable(_emptyTab, {
    __index = function(t, k)
        if k == __get then return nil end
        if k == __asize then
            BindMetaPush(_foreachCaches)
            return 0
        end
        if k == __path then return "" end
        return _emptyTab
    end,
    __newindex = function(t, k, v) end,
    __ishook = true,
    __kvoname = "",
    __ck = "",
    __pk = "",
    __vv = nil,
})
-------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------

-- 用于缓存值不统一引起的 - list时 一维二维相互转换引起key不同
local __e_ = {}
local function bindMeta_ckeyGet(cKey)
    if cKey == nil then return __e_ end
    return _ckeyTabCaches[cKey] or __e_
end
local function bindMeta_ckeyPut(cKey, mt)
    if cKey == nil then return end
    if _ckeyTabCaches[cKey] == nil then
        _ckeyTabCaches[cKey] = createWeakT("k")
    end
    _ckeyTabCaches[cKey][mt] = true
end


--- 拼接 path -
local function bindMeta_path(k1, k2, force)
    -- preview模式下会前面会多个 G. 所以需要删除
    if force or (debug_preview_open == false and debug_preview_watch) then
        if string.len(k1) > 2 and string.sub(k1, 1, 2) == __b_G_ then
            k1 = string.sub(k1, 3)
        end
    end
    if k2 then
        return k1 .. "." .. k2
    end
    return k1
end

--- 批量设置元表 -- 不做上下文连接
local function bindMeta_batchSetMeta(t, path, ck, pk)
    local mapt = {}
    for _k, _v in pairs(t) do
        if type(_v) == "table" then
            bindMeta_batchSetMeta(_v, bindMeta_path(path , _k), _k, ck)
        else
            BindMeta(bindMeta_path(path , _k), {}, _v, _k, ck, true)
        end
    end
    return BindMeta(path, mapt, t, ck, pk, true)
end

local function bindMeta_getAndCacheTab(mt, iscache, isCell)
    BindMetaPush(_foreachCaches)
    local t = mt.__vv
    if iscache and t ~= nil then
        if mt.__bt then return t end
    else
        if isCell and __open_cell_data__ then
            local bind = string.sub(mt.__kvoname, 1, #mt.__kvoname - #(tostring(mt.__pk) .. tostring(mt.__ck)) - 2)
            t = DataBinding:getCellData(bind, mt.__pk, mt.__ck)
        else
            t = DataBinding:get(bindMeta_path(mt.__kvoname))
        end
    end

    if t and type(t) == "table" then
        bindMeta_batchSetMeta(t, bindMeta_path(mt.__kvoname), mt.__ck, mt.__pk)
    else
        BindMeta(bindMeta_path(mt.__kvoname), nil, t, mt.__ck, mt.__pk, true)
    end
    return t
end

--- 数组获取
local function bindMeta_getArraySize(mt)
    local t = bindMeta_getAndCacheTab(mt, true)
    if t == nil then return 0 end
    return #t
end

local function bindMeta_getWatchPath(keypath, ck)
    if type(ck) == "number" then
        local pt = _kpathCache[string.sub(keypath, 1,#keypath - #tostring(ck) -1)]
        if pt then
            local mt = getmetatable(pt)
            return bindMeta_getWatchPath(mt.__kvoname, mt.__ck)
        end
    end
    return keypath
end

local function bindMeta_update(path, v, cKey)
    for _v, _ in pairs(bindMeta_ckeyGet(cKey)) do --同属性key修改先置为空 - 使用时需要本地读取
        _v.__vv = nil
    end
    DataBinding:update(path, v)
end

local function bindMeta_watch(mt, v)
    local k = bindMeta_path(bindMeta_getWatchPath(mt.__kvoname, mt.__ck))
    if k == nil then return end
    local w_id = DataBinding:watch(k, v)
    if w_id then
        _watchCache[w_id] = {k, v}
        BindMetaAdd(_watchIds, w_id, false)
    end
    return w_id
end

-----------------------------------------------------------------------------------------------------------
----------------------  原表操作  __index/__newindex/__call                --------------------------
-----------------------------------------------------------------------------------------------------------
-- __index
local function bindMeta__index(t, k)
    if k == nil then
        return _emptyTab
    end
    if type(k) == "table" then
        k = k.__get
    elseif type(k) == "number" and k <= 0 then
        return _emptyTab
    end

    local mt = getmetatable(t)
    if k == __kvoname or k == __ishook then
        return mt[k]
    end
    if k == __vv then
        return mt.__vv
    end
    --print("to Get::" .. mt.__kvoname, k)
    if k == __get then -- get
        local temp_path = bindMeta_path(mt.__kvoname)
        if BindMetaGet(_cellBinds) then BindMetaAdd(_cellBinds, temp_path, true) end -- 存储 list 数据绑定path
        local temp_v = mt.__vv
        if temp_v ~= nil then return temp_v end -- 有缓存先使用缓存内容
        --print("to Get::" .. mt.__kvoname, k)
        temp_v = DataBinding:get(temp_path)
        if (#_foreachCaches) > 0 then
            mt.__vv = temp_v
            BindMetaGet(_foreachCaches)[temp_path] = true
        end
        return temp_v
    elseif k == __path then
        return bindMeta_path(mt.__kvoname)
    elseif k == __asize then
        --return DataBinding:arraySize(bindMeta_path(mt.__kvoname)) or 0
        return bindMeta_getArraySize(mt)
    elseif k == __remove or k == __rreal or k == __greal or k == __watch then
        mt.__opname = k
        return t
    elseif k == __ci then
        ---@see BindMetaWatchListCell() -- 结束
        BindMetaPush(_cellBinds)
        local temp_v = nil
        if __open_combine_data__ then
            temp_v = bindMeta_getAndCacheTab(mt, true, true)
        end
        return BindMeta(mt.__kvoname,
                {row={__get=mt.__ck}, section={__get=mt.__pk}}, temp_v, mt.__ck, mt.__pk)
    end
    if debug_preview_watch then
        if k == WATCH or k == FOREACH then
            mt.__opname = k
            return t
        end
    end
    return BindMeta(bindMeta_path(mt.__kvoname,  k), nil, nil, k, mt.__ck)
end

-- __newindex
local function bindMeta__newindex(t, k, v)
    if k == nil or k == __vv or k == __ignore then return end
    local mt = getmetatable(t)
    if k == __watch then -- watch
        bindMeta_watch(mt, v)
        return
    end

    if k == __set then k = nil end

    if debug_preview_open then
        rawset(t, k, v)
        if type(v) == "table" then
            --- 自动添加原表
            local meta = BindMeta(bindMeta_path(mt.__kvoname , k), v, nil, k, mt.__ck)
            for _k, _v in pairs(v) do
                bindMeta__newindex(meta, _k, _v)
            end
        end
        return
    end

    local path = bindMeta_path(mt.__kvoname, k)
    if debug_preview_watch then
        -- mock顶层数据
        if mt.__kvoname == __b_G then
            DataBinding:mock(k, v)
            for _, _t in pairs(_watchCache) do
                _t[2](DataBinding:get(_t[1]), nil)
            end
            return
        end
        if type(v) == "table" and v.__ishook then
            v = v.__get
        end
        bindMeta_update(path, v, k)
        for _, __f in pairs(_debugpwacths[path] or {}) do
            __f(v)
        end
        return
    end
    bindMeta_update(path, v, k)
end

-- __call
local function bindMeta__call(t, ...)
    local mt = getmetatable(t)
    local op = mt.__opname
    if not op then
        assert(true, "bind meta call error...")
        return
    end
    mt.__opname = nil

    if op == __greal then -- bind中缓存相关
        bindMeta_getAndCacheTab(mt, false)
        return
    end
    if op == __rreal then
        BindMetaPopForach()
        return
    end

    local size = select("#", ...)
    if size == 0 then
        return
    end

    local p1 = select(1, ...)
    if op == __remove then
        -- remove watch
        for _id, _t in pairs(_watchCache) do
            if _t[2] == p1 then
                DataBinding:removeObserver(_id)
                _watchCache[_id] = nil
            end
        end
        return
    elseif op == __watch then
        return bindMeta_watch(mt,p1)
    end

    if debug_preview_watch then
        if size == 2 and op == WATCH then
            -- debug prevew watch
            local k = bindMeta_path(mt.__kvoname,  p1, true)
            local v = _debugpwacths[k]
            if not v then
                v = {}
                _debugpwacths[k] = v
            end
            v[#v + 1] = select(2, ...)
        elseif size == 1 and op == FOREACH then
            for __k, __v in pairs(t) do
                p1(__v, __k)
            end
        end
    end
end

function bindMeta_setmetable(o, kpath, v, cKey, preKey, batch)
    _kpathCache[kpath] = o
    local meta = getmetatable(o)
    local mt = {
        __index = bindMeta__index,
        __newindex = bindMeta__newindex,
        __call = bindMeta__call,
        __ishook = true,
        __kvoname = kpath,
        __mt = meta,
        __ck = cKey,
        __pk = preKey,
        __vv = v,
        __bt = batch,
    }
    setmetatable(o, mt)
    if (#_foreachCaches) > 0 then
        BindMetaGet(_foreachCaches)[kpath] = true
    end
    bindMeta_ckeyPut(cKey, mt)
end
-------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------------
------------------------------         hook table insert/remove                 --------------------------
-----------------------------------------------------------------------------------------------------------
if hook_table_insert == nil then
    hook_table_insert = table.insert
    table.insert = function(t, ...)
        assert(t, "insert table must not be nil ")
        if debug_preview_open == false and t.__ishook then
            if select('#', ...) == 1 then
                DataBinding:insert(bindMeta_path(t.__kvoname), -1, select(1, ...))
                return
            end
            DataBinding:insert(bindMeta_path(t.__kvoname), select(1, ...), select(2, ...))
            return
        end
        hook_table_insert(t, ...)
    end
    hook_table_remove = table.remove
    table.remove = function(t, ...)
        assert(t, "remove table must not be nil ")
        if debug_preview_open == false and t.__ishook then
            if select('#', ...) == 0 then
                DataBinding:remove(bindMeta_path(t.__kvoname), -1)
                return
            end
            DataBinding:remove(bindMeta_path(t.__kvoname), select(1, ...))
            return
        end
        hook_table_remove(t, ...)
    end
end
-------------------------------------------------------------------------------------------------------------
-------------------------------------------------------------------------------------------------------------


----------------------------------------------------------------------------------------------------------------
--------------------------------         主入口方法  初始化                 --------------------------
-------------------------------------------------------------------------------------------------------------
---@param kpath string path
---@param o table 初始化对象
---@param v void  id是否默认值
---@param cKey string/number 当前keypath
---@param preKey string/number 上一级keypath
---@param batch boolean 是否是批量修改的meta
function BindMeta(kpath, o, v, cKey, preKey, batch)
    kpath = kpath or ""
    --assert(type(kpath) == "string", "kpath must be string...")

    if o then
        if not o.__ishook then
            bindMeta_setmetable(o, kpath, v, cKey, preKey, batch)
        end
        return o
    end
    o = _kpathCache[kpath]
    if o == nil then
        o = {}
        bindMeta_setmetable(o, kpath, v, cKey, preKey, batch)
    end
    return o;
end
-------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------


----------------------------------------------------------------------------------------------------------
----------------------------         清空操作                       --------------------------
---------------------------------------------------------------------------------------------------------
function BindMetaClear()
    _kpathCache = {}
    _debugpwacths = {}
    for _id, _ in pairs(_watchCache) do
        DataBinding:removeObserver(_id)
    end
    _watchCache = {}
    _ckeyTabCaches = {}
    _foreachCaches = {}
    _watchIds = {}
    _cellBinds = {}
end

--- 删除监听
function BindMetaRemoveWatchs(t)
    if not t or type(t) ~= "table" then
        --- 删除单个watch
        DataBinding:removeObserver(t)
        return
    end
    for _, id in ipairs(t) do
        _watchCache[id] = nil
        DataBinding:removeObserver(id)
    end
end

--- 删除foreach的keypath缓存目录
function BindMetaPopForach()
    local t = BindMetaPop(_foreachCaches)
    if t then
        local _t
        for k, _ in pairs(t) do
            _t = _kpathCache[k]
            if _t then
                _kpathCache[k] = nil
                _t = getmetatable(_t)
                if _ckeyTabCaches[_t.__ck] then
                    _ckeyTabCaches[_t.__ck][_t] = nil
                end
            end
        end
    end
end

----------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------



----------------------------------------------------------------------------------------------------------
----------------------------         建立 list cell 绑定属性                     --------------------------
---------------------------------------------------------------------------------------------------------
--local function _getNumberLength(num)
--    if num < 10 then
--        return 1
--    elseif num < 100 then
--        return 2
--    elseif num < 1000 then
--        return 3
--    elseif num < 10000 then
--        return 4
--    elseif num < 100000 then
--        return 5
--    elseif num < 1000000 then
--        return 6
--    end
--    return 7
--end

local function _getNextSplitType(v, index)
    if string.len(v) < index then
        return 0, 0
    end
    if string.byte(v, index) == 46 then -- .
        return string.byte(v, index + 1) - 48, index
    end
    return _getNextSplitType(v, index +1)
end
local function _getNextSplitIndex(v, index)
    if string.len(v) <= index then
        return index
    end
    if string.byte(v, index) == 46 then -- .
        return index
    end
    return _getNextSplitIndex(v, index +1)
end

-- 设置 list cell 绑定数据
-- https://git.wemomo.com/sun_109/LuaParser_JavaCode/-/issues/340
function BindMetaWatchListCell(source, section, row)
    if __open_combine_data__ then
        BindMetaPopForach()
    end
    local paths = BindMetaPop(_cellBinds)
    if not paths then return end

    local s_path = bindMeta_path(getmetatable(source).__kvoname);
    local ret, map = {}, {}
    local s_len = string.len(s_path)
    --local s_r_len = 1
    if section == -1 then -- 适配viewpager
        --s_r_len = _getNumberLength(row)
        section = 1
        --else
        --    s_r_len = _getNumberLength(section) + _getNumberLength(row)
    end
    local c1, c2, c2i, s_index, vl = 0, 0, 0, 0, 0
    local key = ""
    for _, v in ipairs(paths) do
        s_index = s_len
        vl = string.len(v)
        if vl >= s_index and string.sub(v,1, s_index) == s_path then
            if vl > (s_index + 3) then
                c1 = string.byte(v, s_index + 2) - 48
                c2, c2i = _getNextSplitType(v, s_index + 3)
                if section > 0 and c1 >= 0 and c1 <= 9 and c2 >= 0 and c2 <= 9 then
                    s_index = c2i
                end
                key = string.sub(v, _getNextSplitIndex(v,s_index + 2) + 1 )
            else
                key = nil
            end
        else
            key = v
        end
        if key and map[key] ~= true then --去重
            ret[#ret + 1] = key
            map[key] = true
        end
    end
    DataBinding:bindCell(s_path,section,row, ret)
end
----------------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------


-------------------------------------------------------------------------------------------------------
--------------------------                   栈操作                           --------------------------
-------------------------------------------------------------------------------------------------------
function BindMetaPush(t)
    if t then
        t[#t + 1] = {}
    end
end

function BindMetaPop(t)
    if t and #t > 0 then
        local v = t[#t]
        t[#t] = nil
        return v
    end
    return nil
end

function BindMetaGet(t)
    if t and #t > 0 then
        return t[#t]
    end
    return nil
end

function BindMetaAdd(t, v, current)
    if current then
        local _t = BindMetaGet(t)
        if _t then
            _t[#_t + 1] = v
        end
    else
        for _, _v in ipairs(t) do
            _v[#_v + 1] = v
        end
    end
end
-------------------------------------------------------------------------------------------------------
-------------------------------------------------------------------------------------------------------

function BindMetaCreateFindGID(IDs) -- 创建全局表_G查找view变量名方法
    local gmt = getmetatable(_G)
    if gmt ~= nil then
        local __indexret = gmt.__index
        gmt.__index = function(t,k)
            if IDs[k] then return IDs[k] end -- 使用比较少 所以没必要变量保存
            if __indexret ~= nil then
                if type(__indexret) == "table" then
                    return __indexret[k]
                end
                return __indexret(t, k)
            end
            return nil
        end
    else
        setmetatable(_G, {__index = function(t,k) return IDs[k] end})
    end
end



-------------------------------------------------------------------------------------------------------
--------------------------      DEBUG / 在preview中使用                      --------------------------
-------------------------------------------------------------------------------------------------------
function BindMetaPreviewStart()
    debug_preview_open = true
    debug_preview_watch = true
end

function BindMetaPreviewEnd()
    debug_preview_open = false
    _kpathCache = {}
    _ckeyTabCaches = {}
end
-------------------------------------------------------------------------------------------------------
-------------------------------------------------------------------------------------------------------
