/*
 * test_rc.c - rc-ng 服务层骨架单元测试
 *
 * 覆盖：
 *   - svc_parse_notify 动作解析（合法/非法输入）
 *   - svc_find 服务查找
 *   - svc_run / svc_dispatch 依赖编排与级联顺序（轨迹钩子断言）
 *
 * 运行: make test
 */
#include <stdio.h>
#include <string.h>
#include "padavan_service.h"
#include "nvram_linux.h"

#define MAX_TRACE 32
static enum svc_action g_acts[MAX_TRACE];
static const char *g_names[MAX_TRACE];
static int g_trace_cnt = 0;

static void trace_cb(enum svc_action act, const char *name)
{
	if (g_trace_cnt < MAX_TRACE) {
		g_acts[g_trace_cnt] = act;
		g_names[g_trace_cnt] = name;
		g_trace_cnt++;
	}
}

static int g_fail = 0;

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		printf("FAIL: %s (line %d)\n", msg, __LINE__); \
		g_fail++; \
	} else { \
		printf("PASS: %s\n", msg); \
	} \
} while (0)

static int idx_of(const char *name)
{
	int i;
	for (i = 0; i < g_trace_cnt; i++) {
		if (strcmp(g_names[i], name) == 0)
			return i;
	}
	return -1;
}

static void test_parse_notify(void)
{
	enum svc_action act;
	const char *svc;

	CHECK(svc_parse_notify("restart_wan", &act, &svc) == 0 && act == SVC_RESTART &&
	      strcmp(svc, "wan") == 0, "parse restart_wan");
	CHECK(svc_parse_notify("stop_firewall", &act, &svc) == 0 && act == SVC_STOP &&
	      strcmp(svc, "firewall") == 0, "parse stop_firewall");
	CHECK(svc_parse_notify("start_dnsmasq", &act, &svc) == 0 && act == SVC_START &&
	      strcmp(svc, "dnsmasq") == 0, "parse start_dnsmasq");
	CHECK(svc_parse_notify("reload_upnp", &act, &svc) == 0 && act == SVC_RELOAD &&
	      strcmp(svc, "upnp") == 0, "parse reload_upnp");
	CHECK(svc_parse_notify("check_pptp", &act, &svc) == 0 && act == SVC_CHECK &&
	      strcmp(svc, "pptp") == 0, "parse check_pptp");
	CHECK(svc_parse_notify("bogus_wan", &act, &svc) == -1, "parse bogus_wan 拒绝");
	CHECK(svc_parse_notify("restart_", &act, &svc) == -1, "parse 空服务名拒绝");
	CHECK(svc_parse_notify(NULL, &act, &svc) == -1, "parse NULL 拒绝");
}

static void test_find(void)
{
	CHECK(svc_find("wan") != NULL, "find wan");
	CHECK(svc_find("dnsmasq") != NULL, "find dnsmasq");
	CHECK(svc_find("nosuchsvc") == NULL, "find 未知服务返回 NULL");
}

static void test_dispatch_cascade(void)
{
	int rc;

	g_trace_cnt = 0;
	rc = svc_dispatch("restart_wan");
	CHECK(rc == 0, "dispatch restart_wan 成功");

	/* wan 本体必先执行 */
	CHECK(g_trace_cnt > 0 && strcmp(g_names[0], "wan") == 0, "wan 最先执行");
	/* 级联触发 firewall 与 dnsmasq */
	CHECK(idx_of("firewall") >= 0, "级联触发 firewall");
	CHECK(idx_of("dnsmasq") >= 0, "级联触发 dnsmasq");
	/* firewall 在 dnsmasq 之前（级联顺序） */
	CHECK(idx_of("firewall") < idx_of("dnsmasq"), "firewall 先于 dnsmasq");
}

static void test_dispatch_deps(void)
{
	int rc, i_wan, i_pptp, i_fw;

	g_trace_cnt = 0;
	rc = svc_dispatch("restart_pptp");
	CHECK(rc == 0, "dispatch restart_pptp 成功");

	i_wan = idx_of("wan");
	i_fw = idx_of("firewall");
	i_pptp = idx_of("pptp");
	CHECK(i_pptp >= 0, "pptp 本体执行");
	CHECK(i_wan >= 0 && i_wan < i_pptp, "依赖 wan 先于 pptp");
	CHECK(i_fw >= 0 && i_fw < i_pptp, "依赖 firewall 先于 pptp");

	/* upnp 仅依赖 firewall，无需 wan */
	g_trace_cnt = 0;
	rc = svc_dispatch("restart_upnp");
	CHECK(rc == 0, "dispatch restart_upnp 成功");
	CHECK(idx_of("firewall") >= 0 && idx_of("firewall") < idx_of("upnp"),
	      "upnp 依赖 firewall 先执行");
	CHECK(idx_of("wan") == -1, "upnp 不触发 wan");
}

static void test_dispatch_errors(void)
{
	CHECK(svc_dispatch("restart_nosuch") == -2, "未知服务返回 -2");
	CHECK(svc_dispatch("garbage") == -1, "非法动作返回 -1");
	CHECK(svc_run(SVC_RESTART, "nosuch") == -1, "svc_run 未知服务返回 -1");
}

/* ---- nvram 开关门控（M2 libnvram 联动） ---- */
#define TEST_CFG "/home/marvis/Marvis/User/3CBAC8107E73429435739CCBD26C293F/workspace/conv_d42fda146033434cbb3992e05dc7068f/temp/padavan_rc_test.kv"
#define TEST_TMP "/home/marvis/Marvis/User/3CBAC8107E73429435739CCBD26C293F/workspace/conv_d42fda146033434cbb3992e05dc7068f/temp/padavan_rc_test.tmp"

static void test_nvram_gate(void)
{
	int rc;

	nvram_set_config_file(TEST_CFG, TEST_TMP);
	/* 预置：wan 禁用 */
	CHECK(nvram_set("wan_enable", "0") == 0, "nvram_set wan_enable=0");
	nvram_commit();

	g_trace_cnt = 0;
	rc = svc_dispatch("restart_wan");
	CHECK(rc == 0, "dispatch restart_wan 成功（wan 禁用）");
	CHECK(idx_of("wan") == -1, "wan 开关=0 时本体被跳过");
	CHECK(idx_of("firewall") >= 0, "级联 firewall 仍执行");
	CHECK(idx_of("dnsmasq") >= 0, "级联 dnsmasq 仍执行");

	/* 恢复：wan 启用 */
	CHECK(nvram_set("wan_enable", "1") == 0, "nvram_set wan_enable=1");
	nvram_commit();

	g_trace_cnt = 0;
	rc = svc_dispatch("restart_wan");
	CHECK(rc == 0, "dispatch restart_wan 成功（wan 启用）");
	CHECK(idx_of("wan") >= 0, "wan 开关=1 时本体执行");
}

int main(void)
{
	svc_set_trace(trace_cb);

	test_parse_notify();
	test_find();
	test_dispatch_cascade();
	test_dispatch_deps();
	test_dispatch_errors();
	test_nvram_gate();

	if (g_fail) {
		printf("\n%d 项 FAIL\n", g_fail);
		return 1;
	}
	printf("\n全部通过\n");
	return 0;
}
