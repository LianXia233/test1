-- luci.padavan.wifi - 老毛子无线配置 <-> OpenWrt /etc/config/wireless 同步
--
-- 无线页表单编辑的是 UCI padavan 的 rt / wl 段（保持老毛子 nvram 变量兼容：
--   `nvram get wl_ssid` == 页面上的 5GHz SSID），保存时再同步到 OpenWrt
-- 原生的 wireless 配置并执行 `wifi reload`，让 MTK wifi 真正生效。
--
-- 本模块不依赖 luci.*，uci 游标以参数注入，便于单元测试。

local map = require "luci.padavan.map"

local M = {}

M.CONFIG = "wireless"

-- 按频段找（必要时创建）wifi-device 段
function M.ensure_device(uci, band)
	local found
	uci:foreach(M.CONFIG, "wifi-device", function(s)
		if not found and tostring(s.band or "") == band.band then
			found = s[".name"]
			return false
		end
	end)
	if found then
		return found
	end

	local name, err = uci:section(M.CONFIG, "wifi-device", band.radio, { band = band.band })
	if name == false or name == nil then
		return nil, err
	end
	return band.radio
end

-- 按设备找 AP 模式 wifi-iface 段（不创建）
function M.find_iface(uci, device)
	local found
	uci:foreach(M.CONFIG, "wifi-iface", function(s)
		if not found and s.device == device and (s.mode == nil or s.mode == "ap") then
			found = s[".name"]
			return false
		end
	end)
	return found
end

-- 按设备找（必要时创建）AP 模式 wifi-iface 段；命名段固定为 padavan_<band>
function M.ensure_iface(uci, device, band)
	local found = M.find_iface(uci, device)
	if found then
		return found
	end

	local name = "padavan_" .. band.id
	local rv, err = uci:section(M.CONFIG, "wifi-iface", name, { device = device, mode = "ap" })
	if rv == false or rv == nil then
		return nil, err
	end
	return name
end

-- padavan 无线值 -> OpenWrt wireless
-- values: { rt = { ssid=, closed=, auth_mode=, wpa_mode=, crypto=, wpa_psk=, channel=, HT_BW=, radio= }, wl = {...} }
-- 返回 ok, report
function M.sync(uci, values)
	local report = {}

	for _, band in ipairs(map.bands) do
		local v = values[band.prefix]
		if v then
			local device, err = M.ensure_device(uci, band)
			if not device then
				return false, err
			end
			local iface
			iface, err = M.ensure_iface(uci, device, band)
			if not iface then
				return false, err
			end

			local enc = map.encryption_to_openwrt(v.auth_mode, v.wpa_mode, v.crypto) or "psk2"

			uci:set(M.CONFIG, iface, "ssid", v.ssid or "")
			uci:set(M.CONFIG, iface, "hidden", (tostring(v.closed) == "1") and "1" or "0")
			uci:set(M.CONFIG, iface, "encryption", enc)
			if enc == "none" then
				uci:delete(M.CONFIG, iface, "key")
			else
				uci:set(M.CONFIG, iface, "key", v.wpa_psk or "")
			end

			local ch = tonumber(v.channel or "0") or 0
			uci:set(M.CONFIG, device, "channel", (ch > 0) and tostring(ch) or "auto")

			local htmode = map.htmode_to_openwrt(band.id, v.HT_BW)
			if htmode then
				uci:set(M.CONFIG, device, "htmode", htmode)
			end

			uci:set(M.CONFIG, device, "disabled", (tostring(v.radio) == "1") and "0" or "1")

			report[band.prefix] = { device = device, iface = iface, encryption = enc }
		end
	end

	uci:commit(M.CONFIG)
	return true, report
end

-- OpenWrt wireless -> padavan 无线值（迁移 / 首次初始化用）
function M.pull(uci)
	local out = {}

	for _, band in ipairs(map.bands) do
		local device = M.ensure_device(uci, band)
		local iface = device and M.find_iface(uci, device)
		if iface then
			local enc = uci:get(M.CONFIG, iface, "encryption")
			local auth, wpa, crypto = map.openwrt_to_encryption(enc)
			local ch = uci:get(M.CONFIG, device, "channel")
			if ch == nil or ch == "auto" or ch == "" then
				ch = "0"
			end

			out[band.prefix] = {
				ssid      = uci:get(M.CONFIG, iface, "ssid") or "",
				closed    = (uci:get(M.CONFIG, iface, "hidden") == "1") and "1" or "0",
				auth_mode = auth or "psk",
				wpa_mode  = wpa or "2",
				crypto    = crypto or "aes",
				wpa_psk   = uci:get(M.CONFIG, iface, "key") or "",
				channel   = tostring(ch),
				HT_BW     = map.openwrt_to_htmode(band.id, uci:get(M.CONFIG, device, "htmode")) or "0",
				radio     = (uci:get(M.CONFIG, device, "disabled") == "1") and "0" or "1",
			}
		end
	end

	return out
end

return M
