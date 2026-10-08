-- test_padavan.lua - luci-app-padavan 纯 Lua 单元测试
--
-- 不依赖 LuCI 运行时：直接加载 luasrc/padavan/*.lua 中的纯逻辑模块
-- （map / apply / wifi / status），uci 游标与命令执行器均由测试注入。
--
-- 运行： ./test/run.sh   或   lua5.1 test/test_padavan.lua

-- 让 require "luci.padavan.map" 解析到 <src>/padavan/map.lua。
-- LuCI 安装后 luasrc 的内容位于 /usr/lib/lua/luci，源码树里省去了 luci 这一层，
-- 因此这里用一个自定义 searcher 把 luci.* 前缀剥掉再查文件。
local SRC = os.getenv("PADAVAN_LUCI_SRC") or "luasrc"

local function luci_searcher(name)
	local rel = name:gsub("%.", "/"):gsub("^luci/", "")
	local path = SRC .. "/" .. rel .. ".lua"
	local fh = io.open(path, "r")
	if not fh then
		return nil
	end
	local src = fh:read("*a")
	fh:close()
	return loadstring(src, "@" .. path)
end

local loaders = package.loaders or package.searchers
table.insert(loaders, 1, luci_searcher)

-- ------------------------------------------------------------------
-- 极简测试框架
-- ------------------------------------------------------------------
local passed, failed = 0, 0

local function ok(cond, msg)
	if cond then
		passed = passed + 1
		print("PASS: " .. msg)
	else
		failed = failed + 1
		print("FAIL: " .. msg)
	end
end

local function eq(got, want, msg)
	if got == want then
		passed = passed + 1
		print("PASS: " .. msg)
	else
		failed = failed + 1
		print(string.format("FAIL: %s (got %s, want %s)", msg, tostring(got), tostring(want)))
	end
end

-- ------------------------------------------------------------------
-- 假 uci 游标（只实现被 wifi.lua 用到的子集）
-- ------------------------------------------------------------------
local function new_uci()
	local u = { data = {}, commits = {} }

	local function sec(config, name)
		u.data[config] = u.data[config] or {}
		u.data[config][name] = u.data[config][name] or {}
		return u.data[config][name]
	end

	function u:get(config, section, option)
		local c = u.data[config]
		local s = c and c[section]
		return s and s[option]
	end

	function u:set(config, section, option, value)
		sec(config, section)[option] = value
	end

	function u:delete(config, section, option)
		local c = u.data[config]
		local s = c and c[section]
		if s then
			s[option] = nil
		end
	end

	function u:section(config, stype, name, values)
		local sname = name or string.format("cfg%02d", #(u.data[config] or {}) + 1)
		local s = sec(config, sname)
		s[".type"] = stype
		for k, v in pairs(values or {}) do
			s[k] = v
		end
		return sname
	end

	function u:foreach(config, stype, fn)
		local c = u.data[config]
		if not c then
			return
		end
		for name, s in pairs(c) do
			if s[".type"] == stype then
				local view = { [".name"] = name }
				for k, v in pairs(s) do
					view[k] = v
				end
				if fn(view) == false then
					return
				end
			end
		end
	end

	function u:commit(config)
		u.commits[#u.commits + 1] = config
	end

	return u
end

local function has(t, needle)
	for _, v in ipairs(t) do
		if v == needle then
			return true
		end
	end
	return false
end

-- ------------------------------------------------------------------
-- 加载被测模块
-- ------------------------------------------------------------------
local map = require "luci.padavan.map"
local apply = require "luci.padavan.apply"
local wifi = require "luci.padavan.wifi"
local status = require "luci.padavan.status"

print("== luci-app-padavan 单元测试（src=" .. SRC .. "）==")

-- ------------------------------------------------------------------
-- 1. map：nvram <-> uci 命名映射
-- ------------------------------------------------------------------
print("\n[1] map 命名映射")
do
	local s, o = map.nvram_to_uci("wan_ipaddr")
	eq(s, "wan", "nvram_to_uci 有下划线 -> section=wan")
	eq(o, "ipaddr", "nvram_to_uci 有下划线 -> option=ipaddr")

	s, o = map.nvram_to_uci("hostname")
	eq(s, "misc", "nvram_to_uci 无下划线 -> section=misc")
	eq(o, "hostname", "nvram_to_uci 无下划线 -> option=hostname")

	eq(map.nvram_to_uci(""), nil, "nvram_to_uci 空名 -> nil")
	eq(map.nvram_to_uci("_leading"), nil, "nvram_to_uci 下划线开头 -> nil")
	eq(map.nvram_to_uci("bad name"), nil, "nvram_to_uci 非法字符 -> nil")

	eq(map.uci_to_nvram("wan", "ipaddr"), "wan_ipaddr", "uci_to_nvram 普通段")
	eq(map.uci_to_nvram("misc", "hostname"), "hostname", "uci_to_nvram misc 段")
	eq(map.uci_to_nvram("", "x"), nil, "uci_to_nvram 空段 -> nil")

	eq(map.path("wan", "ipaddr"), "padavan.wan.ipaddr", "path 拼接")
	eq(map.nvram_to_path("lan_ipaddr"), "padavan.lan.ipaddr", "nvram_to_path 往返")

	eq(map.field_nvram("wan", { key = "ipaddr" }), "wan_ipaddr", "field_nvram")
	eq(map.band("2g").prefix, "rt", "band(2g).prefix")
	eq(map.band("wl").id, "5g", "band(wl).id")
	eq(map.band("nope"), nil, "band 未知 -> nil")
end

-- ------------------------------------------------------------------
-- 2. map：加密 / 频宽归一化
-- ------------------------------------------------------------------
print("\n[2] map 归一化")
do
	eq(map.encryption_to_openwrt("psk", "2", "aes"), "psk2", "加密 psk+WPA2+AES -> psk2")
	eq(map.encryption_to_openwrt("psk", "3", "tkip+aes"), "psk-mixed+tkip+aes",
		"加密 psk+混合+TKIP/AES")
	eq(map.encryption_to_openwrt("open", "0", ""), "none", "开放 -> none")
	eq(map.encryption_to_openwrt("psk", "9", "aes"), nil, "未知加密组合 -> nil")

	local a, w, c = map.openwrt_to_encryption("psk2")
	eq(a, "psk", "openwrt_to_encryption auth")
	eq(w, "2", "openwrt_to_encryption wpa_mode")
	eq(c, "aes", "openwrt_to_encryption crypto")
	eq(map.openwrt_to_encryption("bogus"), nil, "openwrt_to_encryption 未知 -> nil")

	eq(map.htmode_to_openwrt("5g", "3"), "HE160", "2g/5g 频宽 -> HE160")
	eq(map.htmode_to_openwrt("2g", "1"), "HE40", "2.4G 频宽 -> HE40")
	eq(map.openwrt_to_htmode("5g", "HE80"), "2", "HE80 -> 5G HT_BW=2")
	eq(map.htmode_to_openwrt("9g", "1"), nil, "未知频段 -> nil")
end

-- ------------------------------------------------------------------
-- 3. map：字段校验
-- ------------------------------------------------------------------
print("\n[3] map 校验")
do
	local ipf = { key = "ipaddr", label = "IP", type = "ipaddr" }
	ok(map.validate(ipf, "192.168.1.1"), "合法 IP 通过")
	ok(not map.validate(ipf, "999.1.1.1"), "非法 IP 被拒")
	ok(map.validate(ipf, ""), "空 IP 允许")

	local bf = { key = "stp", label = "STP", type = "bool" }
	ok(map.validate(bf, "1"), "bool=1 通过")
	ok(not map.validate(bf, "2"), "bool=2 被拒")

	local ef = { key = "proto", label = "类型", type = "enum",
		values = { { "dhcp", "自动" }, { "pppoe", "拨号" } } }
	ok(map.validate(ef, "pppoe"), "enum 合法值通过")
	ok(not map.validate(ef, "static"), "enum 非法值被拒")

	local inf = { key = "mtu", label = "MTU", type = "integer" }
	ok(map.validate(inf, "1500"), "整数通过")
	ok(not map.validate(inf, "15x"), "非整数被拒")

	local okall, errs = map.validate_all({ ipf, bf }, { ipaddr = "10.0.0.1", stp = "1" })
	ok(okall, "validate_all 全通过")
	eq(#errs, 0, "validate_all 无错误")

	local okbad, errs2 = map.validate_all({ ipf, bf }, { ipaddr = "x", stp = "9" })
	ok(not okbad, "validate_all 检出错误")
	eq(#errs2, 2, "validate_all 错误计数")
end

-- ------------------------------------------------------------------
-- 4. apply：生效命令序列
-- ------------------------------------------------------------------
print("\n[4] apply 生效")
do
	local log = {}
	apply.set_exec(function(cmd)
		log[#log + 1] = cmd
		return true
	end)

	local okr, ex, f = apply.run("wireless")
	ok(okr, "run(wireless) 成功")
	eq(#ex, 1, "wireless 仅 1 条命令")
	eq(ex[1], "/sbin/wifi reload", "wireless 执行 wifi reload")

	log = {}
	local okn, exn = apply.run("network")
	ok(okn, "run(network) 成功")
	eq(#exn, 3, "network 3 条命令")
	eq(exn[1], apply.notify("restart_wan"), "network 先通知 restart_wan")
	eq(exn[3], apply.notify("restart_dnsmasq"), "network 通知 restart_dnsmasq")

	local okbad, _, fbad = apply.run("no_such_scope")
	ok(not okbad, "未知作用域失败")
	eq(fbad, "未知作用域: no_such_scope", "未知作用域错误信息")

	apply.set_exec(function() return false end)
	local okf, _, ff = apply.run("wireless")
	ok(not okf, "执行失败可检出")
	eq(ff, "/sbin/wifi reload", "失败命令返回")

	-- commit_and_apply 提交配置并触发生效
	local u = new_uci()
	local cmds = {}
	apply.set_exec(function(cmd) cmds[#cmds + 1] = cmd; return true end)
	local okc, exc = apply.commit_and_apply(u, { "padavan", "wireless" }, "wireless")
	ok(okc, "commit_and_apply 成功")
	eq(#u.commits, 2, "commit_and_apply 提交 2 个配置")
	ok(has(u.commits, "wireless"), "提交包含 wireless")
	eq(exc[1], "/sbin/wifi reload", "commit_and_apply 触发 wifi reload")

	apply.set_exec(nil)
end

-- ------------------------------------------------------------------
-- 5. wifi：padavan <-> OpenWrt wireless 同步
-- ------------------------------------------------------------------
print("\n[5] wifi 同步")
do
	local u = new_uci()
	local values = {
		rt = { ssid = "Padavan-2.4G", closed = "0", auth_mode = "psk", wpa_mode = "2",
			crypto = "aes", wpa_psk = "secret123", channel = "6", HT_BW = "1", radio = "1" },
		wl = { ssid = "Padavan-5G", closed = "1", auth_mode = "psk", wpa_mode = "3",
			crypto = "tkip+aes", wpa_psk = "secret456", channel = "0", HT_BW = "3", radio = "1" },
	}

	local okr, report = wifi.sync(u, values)
	ok(okr, "wifi.sync 成功")
	eq(report.rt.device, "radio0", "2.4G 设备名")
	eq(report.wl.device, "radio1", "5G 设备名")

	eq(u:get("wireless", "radio0", "band"), "2g", "2.4G wifi-device band")
	eq(u:get("wireless", "radio0", "channel"), "6", "2.4G 信道")
	eq(u:get("wireless", "radio0", "htmode"), "HE40", "2.4G 频宽 htmode")
	eq(u:get("wireless", "radio0", "disabled"), "0", "2.4G 未禁用")

	eq(u:get("wireless", "padavan_2g", "ssid"), "Padavan-2.4G", "2.4G SSID")
	eq(u:get("wireless", "padavan_2g", "encryption"), "psk2", "2.4G 加密")
	eq(u:get("wireless", "padavan_2g", "key"), "secret123", "2.4G 密码")
	eq(u:get("wireless", "padavan_2g", "hidden"), "0", "2.4G 未隐藏")

	eq(u:get("wireless", "radio1", "channel"), "auto", "5G 自动信道")
	eq(u:get("wireless", "radio1", "htmode"), "HE160", "5G 频宽 160M")
	eq(u:get("wireless", "padavan_5g", "ssid"), "Padavan-5G", "5G SSID")
	eq(u:get("wireless", "padavan_5g", "encryption"), "psk-mixed+tkip+aes", "5G 混合加密")
	eq(u:get("wireless", "padavan_5g", "hidden"), "1", "5G 隐藏 SSID")

	ok(has(u.commits, "wireless"), "wifi.sync 提交 wireless")

	-- 幂等：再次同步不应产生重复段
	wifi.sync(u, values)
	local dev_n, iface_n = 0, 0
	u:foreach("wireless", "wifi-device", function() dev_n = dev_n + 1 end)
	u:foreach("wireless", "wifi-iface", function() iface_n = iface_n + 1 end)
	eq(dev_n, 2, "重复同步后 wifi-device 仍为 2")
	eq(iface_n, 2, "重复同步后 wifi-iface 仍为 2")

	-- 开放网络：删除 key，加密为 none
	wifi.sync(u, {
		rt = { ssid = "Open", closed = "0", auth_mode = "open", wpa_mode = "0",
			crypto = "", wpa_psk = "", channel = "0", HT_BW = "0", radio = "0" },
	})
	eq(u:get("wireless", "padavan_2g", "encryption"), "none", "开放网络加密 none")
	eq(u:get("wireless", "padavan_2g", "key"), nil, "开放网络删除 key")
	eq(u:get("wireless", "radio0", "disabled"), "1", "radio=0 时 wifi-device 禁用")

	-- pull：OpenWrt -> padavan
	local pv = wifi.pull(u)
	eq(pv.wl.ssid, "Padavan-5G", "pull 5G SSID")
	eq(pv.wl.auth_mode, "psk", "pull 5G 认证方式")
	eq(pv.wl.wpa_mode, "3", "pull 5G WPA 版本")
	eq(pv.wl.crypto, "tkip+aes", "pull 5G 加密算法")
	eq(pv.wl.HT_BW, "3", "pull 5G 频宽")
	eq(pv.wl.closed, "1", "pull 5G 隐藏标记")
end

-- ------------------------------------------------------------------
-- 6. status：/proc 解析与采集
-- ------------------------------------------------------------------
print("\n[6] status 采集")
do
	local cpu = status.parse_cpu("cpu  100 20 30 400 50 5 6 0 0 0\ncpu0 1 2 3 4 5 6 7 8 9 0\n")
	eq(cpu.user, 100, "parse_cpu user")
	eq(cpu.idle, 400, "parse_cpu idle")
	eq(cpu.busy, 100 + 20 + 30 + 5 + 6, "parse_cpu busy 汇总")
	eq(cpu.total, 100 + 20 + 30 + 400 + 50 + 5 + 6, "parse_cpu total 汇总")
	eq(status.parse_cpu("nope"), nil, "parse_cpu 非法输入 -> nil")

	local mi = status.parse_meminfo("MemTotal:  512000 kB\nMemFree:  100000 kB\nMemAvailable: 300000 kB\n")
	eq(mi.memtotal, 512000, "parse_meminfo MemTotal")
	eq(mi.memfree, 100000, "parse_meminfo MemFree")

	eq(status.parse_loadavg("0.15 0.10 0.05 1/100 1234"), "0.15 0.10 0.05", "parse_loadavg")
	eq(status.parse_uptime("12345.67 999.0"), 12345.67, "parse_uptime")
	eq(status.parse_uptime(nil), 0, "parse_uptime 空 -> 0")

	local up = status.split_uptime(90061) -- 1d 1h 1m 1s
	eq(up.days, 1, "split_uptime days")
	eq(up.hours, 1, "split_uptime hours")
	eq(up.minutes, 1, "split_uptime minutes")
	eq(up.seconds, 1, "split_uptime seconds")

	local ifs = status.parse_interfaces({
		interface = {
			{ interface = "wan", up = true, proto = "dhcp",
			  ["ipv4-address"] = { { address = "10.0.0.2", mask = 24 } } },
			{ interface = "lan", up = false },
		},
	})
	eq(#ifs, 2, "parse_interfaces 条目数")
	eq(ifs[1].name, "wan", "parse_interfaces 名称")
	eq(ifs[1].up, true, "parse_interfaces up")
	eq(ifs[1].address, "10.0.0.2", "parse_interfaces 地址")
	eq(ifs[1].mask, 24, "parse_interfaces 掩码")

	-- collect：注入假 /proc 数据源
	local fixture = {
		["/proc/meminfo"] = "MemTotal: 1000000 kB\nMemFree: 200000 kB\nMemAvailable: 400000 kB\nCached: 100000 kB\nBuffers: 50000 kB\nSwapTotal: 100 kB\nSwapFree: 40 kB\n",
		["/proc/loadavg"] = "0.50 0.40 0.30 1/1 1",
		["/proc/uptime"] = "7200.0 100.0",
		["/proc/stat"] = "cpu  10 0 20 300 0 0 0 0 0 0\n",
	}
	local data = status.collect({
		read = function(p) return fixture[p] end,
		interfaces = { { name = "wan", up = true } },
	})
	eq(data.ram.total, 1000000, "collect ram.total")
	eq(data.ram.used, 1000000 - 400000, "collect ram.used")
	eq(data.ram.cached, 100000 + 0, "collect ram.cached")
	eq(data.swap.total, 100, "collect swap.total")
	eq(data.swap.used, 60, "collect swap.used")
	eq(data.lavg, "0.50 0.40 0.30", "collect lavg")
	eq(data.uptime.days, 0, "collect uptime.days")
	eq(data.uptime.hours, 2, "collect uptime.hours")
	eq(#data.interfaces, 1, "collect interfaces 透传")
	eq(data.cpu.busy, 30, "collect cpu.busy")
end

-- ------------------------------------------------------------------
print(string.format("\n== 结果：%d 通过, %d 失败 ==", passed, failed))
os.exit(failed == 0 and 0 or 1)
