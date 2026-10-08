-- luci.controller.padavan - Padavan 管理界面路由
--
-- 页面结构对齐老毛子习惯（n56u_ribbon_fixed 的菜单层级）：
--   一级「Padavan」 -> 系统状态 / 网络(内部+外部) / 无线
-- 首屏即系统状态页（老毛子首页的负载/CPU/内存/运行时长 + 接口状态）。
--
-- 数据来源约定（docs/重写架构设计.md 5 节）：
--   系统状态 -> /proc + ubus（只读）
--   网络     -> UCI padavan（映射规则见 luci.padavan.map，与 libnvram 一致）
--   无线     -> UCI wireless（OpenWrt 原生）+ padavan（nvram 兼容副本）

module("luci.controller.padavan", package.seeall)

local map = require "luci.padavan.map"
local status = require "luci.padavan.status"
local apply = require "luci.padavan.apply"

function index()
	if not nixio.fs.access("/etc/config/padavan") then
		return
	end

	entry({"admin", "padavan"}, firstchild(), _("Padavan"), 60).dependent = false

	entry({"admin", "padavan", "status"}, call("action_status"), _("系统状态"), 10)
	entry({"admin", "padavan", "network"}, cbi("padavan/network"), _("网络"), 20)
	entry({"admin", "padavan", "wireless"}, cbi("padavan/wireless"), _("无线"), 30)

	entry({"admin", "padavan", "status_data"}, call("action_status_data")).leaf = true
	entry({"admin", "padavan", "apply"}, call("action_apply")).leaf = true
end

-- 接口状态：优先 ubus network.interface dump，失败时返回空表（页面仍可渲染）
local function collect_interfaces()
	local out = {}
	local ok, jsonc = pcall(require, "luci.jsonc")
	if not ok then
		return out
	end
	local raw = luci.sys.exec("ubus call network.interface dump 2>/dev/null")
	local parsed = jsonc.parse(raw or "")
	out = status.parse_interfaces(parsed)

	-- 补充无线状态（读 UCI padavan 兼容副本，供页面显示 SSID / 开关）
	local uci = luci.model.uci.cursor()
	for _, b in ipairs(map.bands) do
		local radio = uci:get("padavan", b.prefix, "radio")
		local ssid = uci:get("padavan", b.prefix, "ssid")
		out[#out + 1] = {
			name = b.prefix,
			up = (radio == "1"),
			proto = "wifi",
			wireless = true,
			band = b.label,
			ssid = ssid,
		}
	end

	return out
end

local function collect_status()
	local uci = luci.model.uci.cursor()
	return {
		hostname  = uci:get("padavan", "misc", "hostname") or "Padavan",
		productid = uci:get("padavan", "misc", "productid") or "",
		timezone  = uci:get("padavan", "misc", "timezone") or "",
		data      = status.collect({ interfaces = collect_interfaces() }),
	}
end

function action_status()
	local ctx = collect_status()
	luci.template.render("padavan/status", ctx)
end

-- 状态页 AJAX 轮询（老毛子 state.js 每 2 秒拉一次 system_status_data.asp）
function action_status_data()
	local ok, err = pcall(function()
		local ctx = collect_status()
		luci.http.prepare_content("application/json")
		luci.http.write_json(ctx)
	end)
	if not ok then
		luci.http.status(500, "status collect failed")
		luci.http.write_json({ error = tostring(err) })
	end
end

-- 通用生效入口（供页面 AJAX 调用或排障）
function action_apply()
	local scope = luci.http.formvalue("scope") or "network"
	local ok, executed, failed = apply.run(scope)
	luci.http.prepare_content("application/json")
	luci.http.write_json({ ok = ok, executed = executed, failed = failed })
end
