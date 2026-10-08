-- luci.model.cbi.padavan.wireless - 无线页（2.4G / 5G）
--
-- 表单直接编辑 UCI `padavan` 的 rt / wl 段，保持老毛子 nvram 变量兼容：
--     页面上的 5GHz SSID  ==  老毛子应用 `nvram get wl_ssid`
-- 保存时再由 luci.padavan.wifi 把值同步到 OpenWrt 原生
-- /etc/config/wireless（wifi-device / wifi-iface），最后 `wifi reload` 生效。
--
-- 字段目录来自 luci.padavan.map.wifi，与 libnvram 的命名映射保持一致。

local map = require "luci.padavan.map"
local wifi = require "luci.padavan.wifi"
local apply = require "luci.padavan.apply"

local m, s, o

m = Map("padavan", translate("无线"),
	translate("2.4GHz / 5GHz 无线配置。保存后写入 UCI <code>padavan</code> 的 " ..
		"<code>rt</code> / <code>wl</code> 段（老毛子 <code>nvram get rt_ssid</code> 可读），" ..
		"并同步到 OpenWrt 原生 <code>/etc/config/wireless</code> 后执行 <code>wifi reload</code>。"))

-- map.lua 的字段类型 -> LuCI datatype
local DATATYPE = {
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

-- 加密相关字段仅在选择 WPA/WPA2 个人版（psk）时有意义
local ENC_DEPS = {
	wpa_mode = { { "auth_mode", "psk" } },
	crypto   = { { "auth_mode", "psk" } },
	wpa_psk  = { { "auth_mode", "psk" } },
}

-- 读取某个频段当前提交后的值，供同步到 wireless 使用
local function read_band(uci, band)
	local v = {}
	for _, f in ipairs(map.wifi) do
		local val = uci:get(map.CONFIG, band.prefix, f.key)
		if val == nil then
			val = f.default
		end
		v[f.key] = val
	end
	return v
end

for _, band in ipairs(map.bands) do
	s = m:section(NamedSection, band.prefix, band.prefix, translate(band.label))
	s.addremove = false
	s.anonymous = false
	add_fields(s, map.wifi, ENC_DEPS)
end

-- 保存：先由 LuCI 提交 padavan，再同步到 OpenWrt wireless 并重载无线
function m.on_after_commit(self)
	local uci = self.uci or luci.model.uci.cursor()

	local values = {}
	for _, band in ipairs(map.bands) do
		values[band.prefix] = read_band(uci, band)
	end

	local ok, report = wifi.sync(uci, values)
	if not ok then
		m.message = translate("无线同步到 OpenWrt 失败：") .. tostring(report)
		return
	end

	apply.run("wireless")
end

return m
