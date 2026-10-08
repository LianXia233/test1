-- luci.padavan.apply - 保存生效（UCI 提交 + 通知/重载）
--
-- 职责：把 LuCI 页面保存下来的配置真正"生效"：
--   1) 提交 UCI（padavan / wireless）
--   2) 通过 rc-ng 的 ubus 事件总线发出 notify（对齐 M3 notify_bus.c：
--        ubus call padavan notify '{"action":"restart_<service>"}'）
--   3) 必要时调用 OpenWrt 原生重载（network reload / wifi reload）
--
-- 可测试性：命令执行器可注入（M.set_exec），默认走 luci.sys.call，
-- 无 LuCI 环境时回退 os.execute；单元测试注入记录器即可断言命令序列。

local map = require "luci.padavan.map"

local M = {}

-- 默认执行器：优先 luci.sys.call，回退 os.execute
local function default_exec(cmd)
	local ok, sys = pcall(require, "luci.sys")
	if ok and type(sys) == "table" and type(sys.call) == "function" then
		return sys.call(cmd)
	end
	return os.execute(cmd)
end

local exec = default_exec

-- 注入执行器（传 nil 恢复默认）；测试用
function M.set_exec(fn)
	exec = fn or default_exec
end

M.default_exec = default_exec

local function notify(action)
	return 'ubus call padavan notify \'{"action":"' .. action .. '"}\''
end
M.notify = notify

-- 作用域 -> 生效命令序列（顺序即执行顺序）
M.RELOAD = {
	wan = {
		notify("restart_wan"),
		"/etc/init.d/network reload",
	},
	lan = {
		notify("restart_wan"),
		"/etc/init.d/network reload",
		notify("restart_dnsmasq"),
	},
	dhcp = {
		notify("restart_dnsmasq"),
	},
	-- wan + lan + dhcp 合并生效：网络页保存后一次触发，避免重复 reload
	network = {
		notify("restart_wan"),
		"/etc/init.d/network reload",
		notify("restart_dnsmasq"),
	},
	wireless = {
		"/sbin/wifi reload",
	},
	firewall = {
		notify("restart_firewall"),
	},
}

-- 执行一个作用域（或作用域数组）的生效命令。
-- 返回 ok(bool), executed(命令列表), failed(失败命令或 nil)
function M.run(scope)
	local scopes = type(scope) == "table" and scope or { scope }
	local executed = {}

	for _, s in ipairs(scopes) do
		local cmds = M.RELOAD[s]
		if not cmds then
			return false, executed, "未知作用域: " .. tostring(s)
		end
		for _, cmd in ipairs(cmds) do
			executed[#executed + 1] = cmd
			local rc = exec(cmd)
			-- os.execute 返回 (true|nil, "exit"|"signal", code)，成功需显式判断
			if rc == nil or rc == false then
				return false, executed, cmd
			end
		end
	end

	return true, executed, nil
end

-- 提交 UCI 配置段并触发生效。
-- uci: luci.model.uci 实例（需实现 :commit）
-- configs: {"padavan", "wireless", ...}
-- scope: 生效作用域
function M.commit_and_apply(uci, configs, scope)
	if uci and type(uci.commit) == "function" then
		for _, c in ipairs(configs or {}) do
			uci:commit(c)
		end
	end
	return M.run(scope)
end

M.map = map

return M
