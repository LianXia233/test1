-- luci.padavan.map - Padavan nvram <-> UCI 映射与字段目录
--
-- 设计约束：本模块为**纯 Lua**（只依赖 string/table），不 require 任何 luci.*
-- 模块，因此可脱离 LuCI 框架被单元测试直接加载（见 test/test_padavan.lua）。
--
-- 映射规则与 C 实现严格对齐（libnvram/src/nvram_uci_cli.c 的 name_to_path()）：
--   * nvram 名 = section_key  ->  padavan.section.key   （仅按第一个下划线切分）
--   * nvram 名 无下划线        ->  padavan.misc.<name>
--   * 合法字符集 [A-Za-z0-9_.-]，section 不得为空
--
-- 字段目录（M.wan / M.lan / M.bands）描述老毛子页面字段 -> UCI 的对应关系，
-- LuCI 的 CBI 与其保持一致，避免"页面写 A、libnvram 读 B"的漂移。

local M = {}

M.CONFIG = "padavan"
M.SECTION_MISC = "misc"

-- ------------------------------------------------------------------
-- 1. 名称映射
-- ------------------------------------------------------------------

-- 合法 nvram 名：非空，仅 [A-Za-z0-9_.-]
local function key_ok(name)
	if type(name) ~= "string" or name == "" then
		return false
	end
	return name:match("^[A-Za-z0-9_.%-]+$") ~= nil
end
M.key_ok = key_ok

-- nvram 名 -> section, option；非法返回 nil
function M.nvram_to_uci(name)
	if not key_ok(name) then
		return nil
	end

	local section, option = name:match("^([^_]+)_(.+)$")
	if section then
		return section, option
	end

	-- 无下划线（或以下划线开头导致 section 为空）归入 misc
	if name:sub(1, 1) == "_" then
		return nil
	end
	return M.SECTION_MISC, name
end

-- section, option -> nvram 名
function M.uci_to_nvram(section, option)
	if type(section) ~= "string" or type(option) ~= "string" then
		return nil
	end
	if section == "" or option == "" then
		return nil
	end
	if section == M.SECTION_MISC then
		return option
	end
	return section .. "_" .. option
end

-- section, option -> "padavan.section.option"
function M.path(section, option)
	if type(section) ~= "string" or type(option) ~= "string" then
		return nil
	end
	return M.CONFIG .. "." .. section .. "." .. option
end

-- nvram 名 -> "padavan.section.option"
function M.nvram_to_path(name)
	local section, option = M.nvram_to_uci(name)
	if not section then
		return nil
	end
	return M.path(section, option)
end

-- ------------------------------------------------------------------
-- 2. 字段目录
-- ------------------------------------------------------------------

-- type: string | password | bool | enum | ipaddr | netmask | integer
-- 老毛子变量名 = section .. "_" .. key（section = "wan"/"lan"/...）

M.WAN_PROTO = {
	{ "dhcp",  "自动获取 (DHCP)" },
	{ "static", "静态 IP" },
	{ "pppoe", "PPPoE 拨号" },
}

M.wan = {
	{ key = "proto",          label = "连接类型",     type = "enum",   values = M.WAN_PROTO, default = "dhcp" },
	{ key = "ipaddr",         label = "IP 地址",      type = "ipaddr", default = "" },
	{ key = "netmask",        label = "子网掩码",     type = "netmask", default = "" },
	{ key = "gateway",        label = "默认网关",     type = "ipaddr", default = "" },
	{ key = "dnsenable",      label = "自动获取 DNS", type = "bool",   default = "1" },
	{ key = "dns1",           label = "DNS 1",        type = "ipaddr", default = "" },
	{ key = "dns2",           label = "DNS 2",        type = "ipaddr", default = "" },
	{ key = "pppoe_username", label = "PPPoE 账号",   type = "string", default = "" },
	{ key = "pppoe_passwd",   label = "PPPoE 密码",   type = "password", default = "" },
	{ key = "mtu",            label = "MTU",          type = "integer", default = "1500" },
	{ key = "hwaddr",         label = "MAC 地址",     type = "string", default = "" },
}

M.lan = {
	{ key = "ipaddr",  label = "LAN IP 地址", type = "ipaddr",  default = "192.168.31.1" },
	{ key = "netmask", label = "子网掩码",    type = "netmask", default = "255.255.255.0" },
	{ key = "stp",     label = "STP 生成树",  type = "bool",    default = "1" },
}

M.dhcp = {
	{ key = "enable", label = "启用 DHCP",    type = "bool",    default = "1" },
	{ key = "start",  label = "地址池起始",   type = "ipaddr",  default = "192.168.31.100" },
	{ key = "end",    label = "地址池结束",   type = "ipaddr",  default = "192.168.31.200" },
	{ key = "lease",  label = "租期(秒)",     type = "integer", default = "86400" },
}

-- 无线字段（每个频段一套，老毛子变量名前缀 = prefix）
-- openwrt: 该字段在 OpenWrt /etc/config/wireless 中的落点
--   device -> wifi-device.<opt>，iface -> wifi-iface.<opt>
M.WIFI_AUTH = {
	{ "open",   "开放系统 (Open)" },
	{ "shared", "共享密钥 (WEP)" },
	{ "psk",    "WPA/WPA2 个人版" },
}

M.WIFI_WPA_MODE = {
	{ "1", "WPA" },
	{ "2", "WPA2" },
	{ "3", "WPA/WPA2 混合" },
}

M.WIFI_CRYPTO = {
	{ "aes",      "AES" },
	{ "tkip+aes", "TKIP+AES" },
}

M.WIFI_HT_BW = {
	{ "0", "20 MHz" },
	{ "1", "20/40 MHz" },
	{ "2", "20/40/80 MHz" },
	{ "3", "20/40/80/160 MHz" },
}

M.wifi = {
	{ key = "radio",     label = "启用无线", type = "bool",  default = "1",   openwrt = nil },
	{ key = "ssid",      label = "SSID",     type = "string", default = nil,  openwrt = { "iface", "ssid" } },
	{ key = "closed",    label = "隐藏 SSID", type = "bool",  default = "0",   openwrt = { "iface", "hidden" } },
	{ key = "auth_mode", label = "认证方式", type = "enum",  default = "psk", values = M.WIFI_AUTH,     openwrt = nil },
	{ key = "wpa_mode",  label = "WPA 版本", type = "enum",  default = "2",   values = M.WIFI_WPA_MODE, openwrt = nil },
	{ key = "crypto",    label = "加密算法", type = "enum",  default = "aes", values = M.WIFI_CRYPTO,   openwrt = nil },
	{ key = "wpa_psk",   label = "无线密码", type = "password", default = "", openwrt = { "iface", "key" } },
	{ key = "channel",   label = "信道 (0=自动)", type = "integer", default = "0", openwrt = { "device", "channel" } },
	{ key = "HT_BW",     label = "频宽",     type = "enum",  default = "1",   values = M.WIFI_HT_BW,    openwrt = { "device", "htmode" } },
	{ key = "TxPower",   label = "发射功率 (%)", type = "integer", default = "100", openwrt = nil },
}

-- 频段 <-> 老毛子前缀 <-> ImmortalWrt 无线设备（radio 名以实机 dts 为准，此处给默认值）
M.bands = {
	{ id = "2g", prefix = "rt", label = "2.4GHz", band = "2g", radio = "radio0" },
	{ id = "5g", prefix = "wl", label = "5GHz",   band = "5g", radio = "radio1" },
}

-- 老毛子 HT_BW -> OpenWrt wifi-device.htmode
-- 目标平台为 WiFi6（MT7981B / MT7987A），统一用 HE 系列；老设备可改为 HT/VHT。
M.HTMODE = {
	["2g"] = { ["0"] = "HE20", ["1"] = "HE40", ["2"] = "HE40", ["3"] = "HE40" },
	["5g"] = { ["0"] = "HE20", ["1"] = "HE40", ["2"] = "HE80", ["3"] = "HE160" },
}

function M.htmode_to_openwrt(band_id, htbw)
	local t = M.HTMODE[band_id]
	return t and t[tostring(htbw or "")]
end

function M.openwrt_to_htmode(band_id, htmode)
	local t = M.HTMODE[band_id]
	if not t or type(htmode) ~= "string" then
		return nil
	end
	for k, v in pairs(t) do
		if v == htmode then
			return k
		end
	end
	return nil
end

-- 按 id 或前缀取频段
function M.band(id_or_prefix)
	for _, b in ipairs(M.bands) do
		if b.id == id_or_prefix or b.prefix == id_or_prefix then
			return b
		end
	end
	return nil
end

-- 目录字段 -> nvram 变量名 / UCI 路径
function M.field_nvram(section, field)
	return M.uci_to_nvram(section, field.key)
end

-- ------------------------------------------------------------------
-- 3. 无线加密参数归一化（老毛子 <-> OpenWrt）
-- ------------------------------------------------------------------

-- 老毛子：(auth_mode, wpa_mode, crypto) -> OpenWrt encryption 字符串
M.ENCRYPTION = {
	{ auth_mode = "open",   wpa_mode = "0", crypto = "",        openwrt = "none" },
	{ auth_mode = "shared", wpa_mode = "0", crypto = "",        openwrt = "wep-shared" },
	{ auth_mode = "psk",    wpa_mode = "1", crypto = "aes",     openwrt = "psk" },
	{ auth_mode = "psk",    wpa_mode = "1", crypto = "tkip+aes", openwrt = "psk+tkip+aes" },
	{ auth_mode = "psk",    wpa_mode = "2", crypto = "aes",     openwrt = "psk2" },
	{ auth_mode = "psk",    wpa_mode = "2", crypto = "tkip+aes", openwrt = "psk2+tkip+aes" },
	{ auth_mode = "psk",    wpa_mode = "3", crypto = "aes",     openwrt = "psk-mixed" },
	{ auth_mode = "psk",    wpa_mode = "3", crypto = "tkip+aes", openwrt = "psk-mixed+tkip+aes" },
}

-- -> OpenWrt encryption；未知组合返回 nil
function M.encryption_to_openwrt(auth_mode, wpa_mode, crypto)
	auth_mode = auth_mode or "open"
	wpa_mode = wpa_mode or "0"
	crypto = crypto or "aes"

	for _, e in ipairs(M.ENCRYPTION) do
		if e.auth_mode == auth_mode and e.wpa_mode == wpa_mode and e.crypto == crypto then
			return e.openwrt
		end
	end
	return nil
end

-- OpenWrt encryption -> auth_mode, wpa_mode, crypto；未知返回 nil
function M.openwrt_to_encryption(enc)
	if type(enc) ~= "string" then
		return nil
	end
	for _, e in ipairs(M.ENCRYPTION) do
		if e.openwrt == enc then
			return e.auth_mode, e.wpa_mode, e.crypto
		end
	end
	return nil
end

-- ------------------------------------------------------------------
-- 4. 校验
-- ------------------------------------------------------------------

local function ip_ok(s)
	if type(s) ~= "string" then return false end
	local a, b, c, d = s:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
	if not a then return false end
	for _, n in ipairs({ a, b, c, d }) do
		if tonumber(n) > 255 then return false end
	end
	return true
end
M.ip_ok = ip_ok

-- 单字段值校验；bool/enum 按目录约束，整数必须可解析
function M.validate(field, value)
	if not field then
		return false, "未知字段"
	end
	value = value or ""
	if field.type == "bool" then
		if value ~= "0" and value ~= "1" then
			return false, field.label .. " 取值必须为 0/1"
		end
	elseif field.type == "enum" then
		local hit = false
		for _, v in ipairs(field.values or {}) do
			if type(v) == "table" and v[1] == value then
				hit = true
				break
			elseif v == value then
				hit = true
				break
			end
		end
		if not hit then
			return false, field.label .. " 取值非法: " .. tostring(value)
		end
	elseif field.type == "ipaddr" or field.type == "netmask" then
		if value ~= "" and not ip_ok(value) then
			return false, field.label .. " 不是合法 IP"
		end
	elseif field.type == "integer" then
		if value ~= "" and not value:match("^%d+$") then
			return false, field.label .. " 必须为整数"
		end
	end
	return true
end

-- 整组字段校验，返回 ok, {错误列表}
function M.validate_all(fields, values)
	local errs = {}
	for _, f in ipairs(fields) do
		local ok, msg = M.validate(f, values[f.key])
		if not ok then
			errs[#errs + 1] = msg
		end
	end
	return #errs == 0, errs
end

return M
