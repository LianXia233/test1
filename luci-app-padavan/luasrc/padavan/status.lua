-- luci.padavan.status - 系统状态数据采集（供系统状态页使用）
--
-- 老毛子的状态栏（show_banner / system_status_data.asp）会周期性拉取
-- CPU / 内存 / 负载 / 运行时长，其 CPU 占用是**两次采样求差**在浏览器端计算的
-- （见 n56u_ribbon_fixed/state.js 的 setSystemInfo）。重写版保持同一语义：
-- 控制器只返回原始计数器，前端按差值算占用率。
--
-- 模块化设计：解析函数全部为纯函数（输入文本/JSON，输出表），
-- 数据来源可通过参数注入，便于单元测试（不依赖真实 /proc 与 ubus）。

local M = {}

M.PROC_STAT = "/proc/stat"
M.PROC_MEMINFO = "/proc/meminfo"
M.PROC_UPTIME = "/proc/uptime"
M.PROC_LOADAVG = "/proc/loadavg"

local function default_read(path)
	local fh = io.open(path, "r")
	if not fh then
		return nil
	end
	local data = fh:read("*a")
	fh:close()
	return data
end

M.default_read = default_read

-- ------------------------------------------------------------------
-- 纯解析函数
-- ------------------------------------------------------------------

-- /proc/meminfo -> { memtotal = <kB>, memfree = <kB>, ... }
function M.parse_meminfo(text)
	local t = {}
	if type(text) ~= "string" then
		return t
	end
	for line in text:gmatch("[^\n]+") do
		local key, val = line:match("^([%a_%(%)]+):%s*(%d+)")
		if key then
			t[key:lower()] = tonumber(val)
		end
	end
	return t
end

-- /proc/loadavg -> "0.00 0.01 0.05"
function M.parse_loadavg(text)
	if type(text) ~= "string" then
		return nil
	end
	local a, b, c = text:match("^(%S+)%s+(%S+)%s+(%S+)")
	if not a then
		return nil
	end
	return a .. " " .. b .. " " .. c
end

-- /proc/uptime -> 秒（浮点）
function M.parse_uptime(text)
	if type(text) ~= "string" then
		return 0
	end
	return tonumber(text:match("^(%S+)")) or 0
end

-- 秒 -> { days, hours, minutes, seconds }
function M.split_uptime(sec)
	sec = math.floor(tonumber(sec) or 0)
	return {
		days = math.floor(sec / 86400),
		hours = math.floor(sec % 86400 / 3600),
		minutes = math.floor(sec % 3600 / 60),
		seconds = sec % 60,
	}
end

-- /proc/stat 第一行 "cpu  user nice system idle iowait irq softirq ..." -> 计数器表
function M.parse_cpu(text)
	if type(text) ~= "string" then
		return nil
	end
	local line = text:match("^(cpu[^\n]*)")
	if not line then
		return nil
	end

	local v = {}
	for n in line:gmatch("(%d+)") do
		v[#v + 1] = tonumber(n)
	end
	if #v < 5 then
		return nil
	end

	local user, nice, system, idle, iowait = v[1], v[2], v[3], v[4], v[5]
	local irq, sirq = v[6] or 0, v[7] or 0
	local total = 0
	for _, n in ipairs(v) do
		total = total + n
	end

	return {
		total = total,
		user = user,
		nice = nice,
		system = system,
		idle = idle,
		iowait = iowait,
		irq = irq,
		sirq = sirq,
		busy = user + nice + system + irq + sirq,
	}
end

-- ubus network.interface dump -> 精简接口列表
-- 返回 { {name=, up=bool, proto=, address=, netmask=} ... }
function M.parse_interfaces(data)
	local out = {}
	if type(data) ~= "table" then
		return out
	end

	local list = data.interface or data
	if type(list) ~= "table" then
		return out
	end

	for _, itf in ipairs(list) do
		if type(itf) == "table" then
			local addr, mask
			local v4 = itf["ipv4-address"]
			if type(v4) == "table" and type(v4[1]) == "table" then
				addr = v4[1].address
				mask = v4[1].mask
			end
			out[#out + 1] = {
				name    = itf.interface or itf.name,
				up      = itf.up == true,
				proto   = itf.proto,
				address = addr,
				mask    = mask,
				device  = itf.l3_device or itf.device,
			}
		end
	end

	return out
end

-- ------------------------------------------------------------------
-- 采集
-- ------------------------------------------------------------------

-- sources: 可注入 { read = fn(path), ubus_dump = fn() -> table }
function M.collect(sources)
	sources = sources or {}
	local read = sources.read or M.default_read

	local meminfo = M.parse_meminfo(read(M.PROC_MEMINFO))
	local lavg = M.parse_loadavg(read(M.PROC_LOADAVG))
	local uptime = M.parse_uptime(read(M.PROC_UPTIME))
	local cpu = M.parse_cpu(read(M.PROC_STAT))

	local total = meminfo.memtotal or 0
	local free = meminfo.memfree or 0
	local cached = (meminfo.cached or 0) + (meminfo.sreclaimable or 0)
	local buffers = meminfo.buffers or 0
	local available = meminfo.memavailable or (free + cached + buffers)

	return {
		cpu = cpu or { total = 0, busy = 0, user = 0, nice = 0, system = 0,
		                idle = 0, iowait = 0, irq = 0, sirq = 0 },
		ram = {
			total = total,
			free = free,
			used = total - available,
			cached = cached,
			buffers = buffers,
		},
		swap = {
			total = meminfo.swaptotal or 0,
			used = (meminfo.swaptotal or 0) - (meminfo.swapfree or 0),
		},
		lavg = lavg or "0.00 0.00 0.00",
		uptime = M.split_uptime(uptime),
		interfaces = sources.interfaces or {},
	}
end

return M
