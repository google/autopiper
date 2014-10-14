-- Copyright 2014 Google Inc. All rights reserved.
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.


-- ScopedMap is a layered map from keys to values. It supports adding a new
-- scope and popping up one scope. When a key -> value mapping is added to the
-- map, it is added to the deepest (most recent) scope, and popping up one
-- scope discards all mappings in the deepest scope. As such, this is a simple
-- dynamic scope implementation.
ScopedMap = {}
function ScopedMap:new()
    local o = {
        _scopes = { {} } -- start with one top-level (global) scope
    }
    setmetatable(o, self)
    self.__index = self
    return o
end

function ScopedMap:get(key)
    for i = #self._scopes, 1, -1 do
        local s = self._scopes[i]
        local val = s[key]
        if val ~= nil then return val end
    end
    return nil
end

function ScopedMap:set(key, val)
    top_scope = self._scopes[#self._scopes]
    top_scope[key] = val
end

function ScopedMap:push()
    table.insert(self._scopes, {})
end

function ScopedMap:pop()
    if #self._scopes == 1 then return end
    table.remove(self._scopes)
end

-- CodeGen is a context object that is carried through user code and macros. It
-- serves as a sort of dynamic context, allowing mapping of variables to IR
-- statements (and phi insertion at join-points), overriding live-ins within a
-- scope, and overriding/hooking particular ops within a scope, in addition to
-- basic BB/statement output.

CodeGen = {}
function CodeGen:new()
    local o = {
        _bbs = {},
        _curbb = nil,
        _valnum = 1,
        _vars = ScopedMap:new(),    -- stack of override layers for variables
        _ops = ScopedMap:new(),     -- stack of override layers for ops
        _output = "",               -- IR output
    }

    for k,v in pairs(CodeGen.builtins) do
        o._ops:set(k, v)
    end

    setmetatable(o, self)
    self.__index = self
    return o
end

function CodeGen:ir()
    return self._output
end

--------------------------------------
-- valnum allocation (unique integers)
--------------------------------------
function CodeGen:get_valnum()
    local i = self._valnum
    self._valnum = self._valnum + 1
    return i
end

------------
-- BB output
------------
function CodeGen:current_bb()
    if self._curbb == nil then self._curbb = self:new_bb() end
    return self._curbb
end
function CodeGen:new_bb(entry)
    local valnum = self:get_valnum()
    local name = "bb_" .. valnum
    local bb = { name = name, entry = entry, stmts = {} }
    table.insert(self._bbs, bb)
    return bb
end
function CodeGen:finish_bb()
    if self._curbb == nil then return end
    self._output = self._output .. self:print_ir_bb(self._curbb)
    self._curbb = nil
end

-------------------
-- statement output
-------------------
function CodeGen:add_stmt(
        bb,
        params)
    -- Params that may be pased:
    --  opcode,     -- opcode as recognized by IR parser in backend
    --  width,      -- result width in bits
    --  named_item, -- port/array/reg name
    --  targets,    -- list of target BBs
    --  args,       -- list of stmts whose values are used as args
    --  timingvar,
    --  timingoffset
    params.valnum = self:get_valnum()
    local bb = self:current_bb()
    table.insert(bb.stmts, params)
    return params 
end

-------------
-- IR printer
-------------
function CodeGen:print_ir_bb(bb)
    local bb_entry = ""
    if bb.entry then bb_entry = "entry " end
    self:print_ir_text(bb_entry .. bb.name .. ":\n")
    for _,v in ipairs(bb.stmts) do print_ir_stmt(v) end
    print_ir_text("\n")
end

function CodeGen:print_ir_stmt(stmt)
    local out = ""
    function append(s) out = out .. s end

    append("%" .. stmt.valnum)
    if stmt.width ~= 0 then
        append("[" .. stmt.width .. "]")
    end
    append(" = " .. stmt.opcode)
    local have_arg = false
    if (stmt.named_item ~= nil) then
        append(" \"" .. stmt.named_item .. "\")")
        have_arg = true
    end
    for i,v in stmt.args do
        if have_arg then append(", ") else append(" ") end
        append("%" .. v.valnum)
        have_arg = true
    end
    for i,v in stmt.targets do
        if have_arg then append(", ") else append(" ") end
        append(v.name)
        have_arg = true;
    end
    if stmt.timingvar ~= nil then
        append(" @[" .. stmt.timingvar .. "+" .. stmt.timingoffset .. "]")
    end
    append("\n")
end

function CodeGen:print_ir_text(text)
    self._output = self._output .. text
end

-------------------
-- builtin stmt ops
-------------------

-- Note: no builtins for control-flow ops: these are wrapped by if() and
-- while().

CodeGen.builtins = {}

function CodeGen.builtins.expr(c, opcode, args)
    return c:add_stmt(c:current_bb(), { opcode = opcode, width = width, args = args })
end

-------------------
-- control-flow ops
-------------------

function
