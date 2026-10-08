-- luci.model.cbi.padavan.network - 网络页（WAN / LAN / DHCP）
--
-- 字段与 UCI 落点由 luci.padavan.map 统一描述，保证与 libnvram 的
-- `section_key -> padavan.section.key` 映射一致：
--     页面写 padavan.lan.ipaddr  ==  老毛子应用 `nvram get lan_ipaddr`
--
-- 保存生效：CBI 提交后由 on_after_commit 触发 luci.padavan.apply 的
-- "network" 作用域（notify restart_wan + network reload + restart_dnsmasq）。

local map = require "luci.padavan.map"
local apply = require "luci.padavan.apply"

local m, s, o

m = Map("padavan", translate("网络"),
	translate("内部网络 (LAN) 与 外部网络 (WAN) 配置。保存后写入 UCI <code>padavan</code>，" ..
		"并通过 rc-ng 事件总线通知服务重载；老毛子应用可用 <code>nvram get</code> 直接读取。"))

-- map.lua 的字段类型 -> LuCI datatype
local DATATYPE = {
	ipaddr  = "ip4addr",
	netmask = "ipmask4",
	integer = "uinteger",
	string  = "string",
}

-- 把字段目录渲染成 CBI 表单
local function add_fields(section, fields, deps)
	for _, f in ipairs(fields) do
		local opt

		if f.type == "enum" then
			opt = section:option(ListValue, f.key, translate(f.label))
			for _, v in ipairs(f.values) do
				opt:value(v[1], translate(v[2]))
			end
		elseif f.type == "bool" then
			opt = section:option(Flag, f.key, translate(f.label))
			opt.enabled  = "1"
			opt.disabled = "0"
		else
			opt = section:option(Value, f.key, translate(f.label))
			if f.type == "password" then
				opt.password = true
			end
			opt.datatype = DATATYPE[f.type]
		end

		opt.default = f.default
		opt.rmempty = (f.default == nil or f.default == "")
		if f.description then
			opt.description = f.description
		end

		for _, d in ipairs(deps and deps[f.key] or {}) do
			opt:depends(d[1], d[2])
		end
	end
end

-- ------------------------- 外部网络 (WAN) -------------------------
s = m:section(NamedSection, "wan", "wan", translate("外部网络 (WAN)"))
s.addremove = false
s.anonymous = false

add_fields(s, map.wan, {
	ipaddr   = { { "proto", "static" } },
	netmask  = { { "proto", "static" } },
	gateway  = { { "proto", "static" } },
	dns1     = { { "dnsenable", "0" } },
	dns2     = { { "dnsenable", "0" } },
	pppoe_username = { { "proto", "pppoe" } },
	pppoe_passwd   = { { "proto", "pppoe" } },
})

o = s:option(Value, "pppoe_idletime", translate("PPPoE 空闲断线 (秒)"))
o.datatype = "uinteger"
o.default = "0"
o:depends("proto", "pppoe")

-- ------------------------- 内部网络 (LAN) -------------------------
s = m:section(NamedSection, "lan", "lan", translate("内部网络 (LAN)"))
s.addremove = false
s.anonymous = false
add_fields(s, map.lan)

s = m:section(NamedSection, "dhcp", "dhcp", translate("DHCP 服务器"))
s.addremove = false
s.anonymous = false
add_fields(s, map.dhcp, {
	start = { { "enable", "1" } },
	["end"] = { { "enable", "1" } },
	lease = { { "enable", "1" } },
})

function m.on_after_commit(self)
	apply.run("network")
end

return m
