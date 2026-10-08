/*
 * service_registry.c - rc-ng 声明式服务注册表 + 动作编排
 *
 * 核心逻辑：
 *   1. padavan_services[] 静态声明服务（替换原版 rc 巨型 switch）；
 *   2. svc_dispatch 解析 notify_rc 动作并分发；
 *   3. svc_run 递归满足 deps（拓扑序）→ 执行本体 → 触发 cascade 级联；
 *   4. 环保护：深度上限 + 简单递归深度计数。
 *
 * 与 M2 libnvram 的衔接点：使能开关 nvram_enable 通过 nvram_safe_get_int
 * 读取，骨架阶段打印日志，正式版接入执行。
 */
#include <stdio.h>
#include <string.h>
#include "padavan_service.h"
#include "nvram_linux.h"

/* ---- 轨迹钩子（测试注入用） ---- */
static svc_trace_fn g_trace = NULL;

void svc_set_trace(svc_trace_fn cb)
{
	g_trace = cb;
}

static void trace_call(enum svc_action act, const char *name)
{
	if (g_trace)
		g_trace(act, name);
}

/* ---- 默认执行器：无自定义 handler 时调 script 或 procd 托管 ---- */
static int default_handler(const struct padavan_service *svc, enum svc_action act)
{
	const char *act_str;

	/* nvram 开关门控：启用动作（start/restart/reload）在开关为 0 时跳过本体。
	 * stop/check 不受开关限制，保证总能停止已配置的服务。 */
	if (svc->nvram_enable &&
	    (act == SVC_START || act == SVC_RESTART || act == SVC_RELOAD)) {
		if (nvram_safe_get_int(svc->nvram_enable, 1, 0, 1) == 0) {
			fprintf(stderr, "[rc-ng] %s 已禁用（%s=0），跳过\n",
			        svc->name, svc->nvram_enable);
			return 1;   /* 1=主动跳过：未执行，不记录轨迹 */
		}
	}

	switch (act) {
	case SVC_START:   act_str = "start"; break;
	case SVC_STOP:    act_str = "stop"; break;
	case SVC_RESTART: act_str = "restart"; break;
	case SVC_RELOAD:  act_str = "reload"; break;
	default:          act_str = "check"; break;
	}

	if (!svc->script) {
		/* procd 托管：正式版经 ubus 通知 procd 实例 svc->procd_instance */
		fprintf(stderr, "[rc-ng] procd 托管服务 %s: %s（骨架阶段仅日志）\n",
		        svc->name, act_str);
		return 0;
	}

	fprintf(stderr, "[rc-ng] 执行 %s %s\n", svc->script, act_str);
	return 0;
}

/* ---- 服务查找 ---- */
const struct padavan_service *svc_find(const char *name)
{
	const struct padavan_service *svc;

	for (svc = padavan_services; svc->name; svc++) {
		if (strcmp(svc->name, name) == 0)
			return svc;
	}
	return NULL;
}

/* ---- 动作解析：restart_wan / stop_firewall / ... ---- */
int svc_parse_notify(const char *notify, enum svc_action *act, const char **svc)
{
	static const struct {
		const char *prefix;
		enum svc_action act;
	} acts[] = {
		{ "restart_", SVC_RESTART },
		{ "start_",   SVC_START },
		{ "stop_",    SVC_STOP },
		{ "reload_",  SVC_RELOAD },
		{ "check_",   SVC_CHECK },
	};
	size_t i;

	if (!notify || !*notify)
		return -1;

	for (i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) {
		size_t plen = strlen(acts[i].prefix);

		if (strncmp(notify, acts[i].prefix, plen) == 0 && notify[plen] != '\0') {
			*act = acts[i].act;
			*svc = notify + plen;
			return 0;
		}
	}
	return -1;
}

/* ---- 递归编排：deps -> 本体 -> cascade ---- */
#define SVC_MAX_DEPTH 8

static int svc_run_rec(enum svc_action act, const char *name, int depth)
{
	const struct padavan_service *svc = svc_find(name);
	int i, rc = 0;

	if (!svc)
		return -1;
	if (depth > SVC_MAX_DEPTH) {
		fprintf(stderr, "[rc-ng] 依赖环或过深，终止: %s\n", name);
		return -2;
	}

	/* 1. 前置依赖（拓扑序） */
	if (svc->deps) {
		for (i = 0; svc->deps[i].name; i++) {
			int drc = svc_run_rec(act, svc->deps[i].name, depth + 1);
			if (drc < 0 && !svc->deps[i].optional)
				rc = drc;
		}
	}
	if (rc < 0)
		return rc;

	/* 2. 本体 */
	if (svc->handler)
		rc = svc->handler(act, svc->name);
	else
		rc = default_handler(svc, act);
	if (rc == 1)            /* 主动跳过（开关禁用）：视为成功但不记录 */
		rc = 0;
	else if (rc < 0)
		return rc;
	else
		trace_call(act, svc->name);

	/* 3. 级联通知 */
	if (svc->cascade) {
		for (i = 0; svc->cascade[i]; i++) {
			int crc = svc_run_rec(act, svc->cascade[i], depth + 1);
			if (crc < 0)
				return crc;
		}
	}
	return rc;
}

int svc_run(enum svc_action act, const char *name)
{
	return svc_run_rec(act, name, 0);
}

int svc_dispatch(const char *notify)
{
	enum svc_action act;
	const char *name;

	if (svc_parse_notify(notify, &act, &name) < 0)
		return -1;
	if (!svc_find(name))
		return -2;

	fprintf(stderr, "[rc-ng] dispatch %s\n", notify);
	return svc_run(act, name);
}

/* ---- 服务注册表：新增服务 = 在此追加一行 ---- */

static const struct svc_dep deps_firewall_for_upnp[] = {
	{ "firewall", 0 },
	{ NULL, 0 },
};

static const struct svc_dep deps_wan_for_pptp[] = {
	{ "wan", 0 },
	{ "firewall", 0 },
	{ NULL, 0 },
};

static const char *cascade_wan[] = { "firewall", "dnsmasq", NULL };
static const char *cascade_firewall[] = { "dnsmasq", NULL };

const struct padavan_service padavan_services[] = {
	{
		.name = "wan",
		.procd_instance = "padavan-wan",
		.uci_config = "network",
		.nvram_enable = "wan_enable",
		.deps = NULL,
		.cascade = cascade_wan,   /* 原版 restart_wan 会连带重建 firewall/dnsmasq */
	},
	{
		.name = "firewall",
		.procd_instance = "padavan-firewall",
		.uci_config = "firewall",
		.nvram_enable = "fw_enable",
		.cascade = cascade_firewall,
	},
	{
		.name = "dnsmasq",
		.procd_instance = "padavan-dnsmasq",
		.uci_config = "dhcp",
		.nvram_enable = "dns_enable",
	},
	{
		.name = "upnp",
		.procd_instance = "padavan-upnp",
		.uci_config = "upnpd",
		.nvram_enable = "upnp_enable",
		.deps = deps_firewall_for_upnp,
	},
	{
		.name = "pptp",
		.procd_instance = "padavan-pptp",
		.uci_config = "pptp",
		.nvram_enable = "pptp_client_enable",
		.deps = deps_wan_for_pptp,
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
};
