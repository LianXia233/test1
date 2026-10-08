/*
 * test_nvram.c - libnvram 单元测试（可切换后端）
 *
 * 默认 file 后端；--backend uci_cli 时通过 fake_uci 桩测试 UCI CLI 后端。
 * 运行前无需 root：配置指向临时目录（uci_cli 模式经 UCI_CONFIG_DIR 隔离）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvram_linux.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
	if (cond) { printf("PASS: %s\n", msg); } \
	else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(int argc, char **argv)
{
	char buf[8192];
	char *v;
	const char *backend = "file";

	if (argc > 1 && strcmp(argv[1], "--backend") == 0 && argc > 2)
		backend = argv[2];

	/* 重定向到测试目录 */
	char tmpl[] = "/tmp/libnvram_testXXXXXX";
	char cfg[160], tmp[160];
	char *d = mkdtemp(tmpl);
	if (!d) { perror("mkdtemp"); return 1; }
	snprintf(cfg, sizeof(cfg), "%s/padavan.conf", d);
	snprintf(tmp, sizeof(tmp), "%s/padavan.tmp", d);
	nvram_set_config_file(cfg, tmp);

	if (strcmp(backend, "uci_cli") == 0) {
		/* 让 fake_uci 桩把配置写到同一临时目录 */
		if (nvram_set_backend("uci_cli") != 0) {
			fprintf(stderr, "切换 uci_cli 后端失败\n");
			return 1;
		}
		setenv("UCI_CONFIG_DIR", d, 1);
	}
	printf("== libnvram 测试（后端 %s，配置目录 %s）==\n", backend, d);

	/* 1. set / get */
	CHECK(nvram_set("wan0_proto", "pppoe") == 0, "nvram_set 基本写");
	v = nvram_get("wan0_proto");
	CHECK(v && strcmp(v, "pppoe") == 0, "nvram_get 读回");

	/* 2. 覆盖写 */
	CHECK(nvram_set("wan0_proto", "dhcp") == 0, "nvram_set 覆盖");
	CHECK(strcmp(nvram_get("wan0_proto"), "dhcp") == 0, "覆盖生效");

	/* 3. 不存在变量 -> NULL；safe_get -> "" */
	CHECK(nvram_get("no_such_key") == NULL, "不存在返回 NULL");
	CHECK(strcmp(nvram_safe_get("no_such_key"), "") == 0, "safe_get 返回空串");

	/* 4. defaults 兜底 */
	CHECK(strcmp(nvram_safe_get("lan_ipaddr"), "192.168.31.1") == 0, "defaults 兜底");

	/* 5. int 接口与范围钳制 */
	nvram_set_int("wan0_mtu", 1500);
	CHECK(nvram_get_int("wan0_mtu") == 1500, "get_int");
	CHECK(nvram_safe_get_int("wan0_mtu", 0, 100, 1400) == 1400, "safe_get_int 上界钳制");

	/* 6. match / invmatch */
	CHECK(nvram_match("wan0_proto", "dhcp") == 1, "nvram_match 命中");
	CHECK(nvram_invmatch("wan0_proto", "pppoe") == 1, "nvram_invmatch 未命中");

	/* 7. temp 变量 */
	CHECK(nvram_set_temp("uptime_hint", "1") == 0, "set_temp");
	CHECK(strcmp(nvram_get("uptime_hint"), "1") == 0, "temp 可读");

	/* 8. unset（用不在 defaults 表中的变量验证真正删除） */
	CHECK(nvram_set("my_custom", "hello") == 0, "set 自定义变量");
	CHECK(nvram_unset("my_custom") == 0, "unset");
	CHECK(nvram_get("my_custom") == NULL, "unset 后为 NULL");
	/* defaults 回退语义：unset 表中变量后回退默认值（老毛子行为） */
	CHECK(nvram_unset("wan0_proto") == 0, "unset defaults 变量");
	CHECK(strcmp(nvram_get("wan0_proto"), "dhcp") == 0, "defaults 回退");
	/* unset 不影响其他项 */
	CHECK(strcmp(nvram_get("wan0_mtu"), "1500") == 0, "unset 后其他项保留");

	/* 9. getall 包含 set 项（遍历 \0 分隔数组，不能用 %s 直接断言） */
	nvram_set("aaa", "111");
	memset(buf, 0, sizeof(buf));
	nvram_getall(buf, sizeof(buf), 0);
	{
		char *p = buf;
		int found = 0;
		while (p && *p) {
			if (strcmp(p, "aaa=111") == 0) { found = 1; break; }
			p += strlen(p) + 1;
		}
		CHECK(found == 1, "getall 包含 set 项");
	}

	/* 10. commit 幂等 */
	CHECK(nvram_commit() == 0, "commit 幂等");

	/* 11. export / import 往返 */
	{
		char exp[192];
		snprintf(exp, sizeof(exp), "%s/backup.conf", d);
		CHECK(nvram_export(exp) == 0, "export");
		nvram_clear();
		CHECK(nvram_get("aaa") == NULL, "clear 后清空");
		CHECK(nvram_import(exp) == 0, "import");
		CHECK(strcmp(nvram_get("aaa"), "111") == 0, "import 后恢复");
	}

	/* 12. 持久性：重新设置配置路径后仍可读 */
	nvram_set_config_file(cfg, tmp);
	CHECK(strcmp(nvram_get("aaa"), "111") == 0, "文件持久化");

	printf("== 结果：%s ==\n", failures ? "有失败" : "全部通过");
	return failures ? 1 : 0;
}
