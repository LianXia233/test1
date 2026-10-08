/*
 * nvram_uci_cli.c - Padavan nvram UCI 后端（通过 `uci` 命令）
 *
 * 依赖：OpenWrt / ImmortalWrt 上的 uci 命令（busybox 或 uci 包）。
 * 配置：/etc/config/padavan（可用 UCI_CONFIG_DIR 覆盖，便于测试）。
 *
 * 映射规则（docs/重写架构设计.md 3.2 兼容策略）：
 *   - nvram 名带下划线：第一个下划线前为 UCI section，之后为 option
 *     例：wan0_proto -> padavan.wan0.proto
 *   - nvram 名无下划线：归入 padavan.misc.<name>
 *     例：hostname -> padavan.misc.hostname
 *   - getall 反向：padavan.misc.<key> -> "<key>"，其余 -> "<sec>_<key>"
 *
 * 说明：与文件后端"set 即落盘"语义对齐，每次 set/unset 后立即 commit；
 * libuci 后端（nvram_uci.c）可改为暂存 + 显式 commit。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "nvram_linux.h"
#include "nvram_backend.h"

#define UCI_CONFIG_NAME  "padavan"
#define UCI_SECTION_MISC "misc"

/* 默认 uci 命令，可用环境变量 NVRAM_UCI_BIN 覆盖（测试时指向 fake_uci） */
static const char *uci_bin(void)
{
	const char *e = getenv("NVRAM_UCI_BIN");
	return (e && *e) ? e : "uci";
}

/* key 白名单：仅允许 [A-Za-z0-9_.-]（UCI 合法标识符），防注入 */
static int key_ok(const char *key)
{
	if (!key || !*key)
		return 0;
	for (; *key; key++) {
		if (!(isalnum((unsigned char)*key) || *key == '_' ||
		      *key == '.' || *key == '-'))
			return 0;
	}
	return 1;
}

/* value 单引号转义：' -> '\'' */
static void shell_quote(const char *val, char *out, size_t outlen)
{
	size_t i = 0;

	out[i++] = '\'';
	for (; *val && i + 4 < outlen; val++) {
		if (*val == '\'') {
			out[i++] = '\'';
			out[i++] = '\\';
			out[i++] = '\'';
			out[i++] = '\'';
		} else {
			out[i++] = *val;
		}
	}
	if (i + 2 < outlen) {
		out[i++] = '\'';
		out[i] = '\0';
	} else {
		out[outlen - 1] = '\0';
	}
}

/* nvram 名 -> padavan.<sec>.<key> */
static int name_to_path(const char *name, char *path, size_t pathlen)
{
	char sec[NVRAM_MAX_PARAM_LEN], key[NVRAM_MAX_PARAM_LEN];
	const char *us;

	if (!key_ok(name))
		return -1;

	us = strchr(name, '_');
	if (us) {
		size_t slen = (size_t)(us - name);
		if (slen >= sizeof(sec) || slen == 0)
			return -1;
		memcpy(sec, name, slen);
		sec[slen] = '\0';
		snprintf(key, sizeof(key), "%s", us + 1);
	} else {
		snprintf(sec, sizeof(sec), "%s", UCI_SECTION_MISC);
		snprintf(key, sizeof(key), "%s", name);
	}
	snprintf(path, pathlen, "%s.%s.%s", UCI_CONFIG_NAME, sec, key);
	return 0;
}

/* 执行 uci 命令，捕获 stdout 到 out（out 可为 NULL，此时只消费输出）；
 * 返回退出码。 */
static int run_uci(const char *cmd, char *out, size_t outlen)
{
	FILE *fp;
	char line[1024];
	int rc;

	fp = popen(cmd, "r");
	if (!fp)
		return -1;
	if (out && outlen > 0)
		out[0] = '\0';
	while (fgets(line, sizeof(line), fp)) {
		if (out && outlen > 0 &&
		    strlen(out) + strlen(line) < outlen)
			strncat(out, line, outlen - strlen(out) - 1);
	}
	rc = pclose(fp);
	return rc;
}

static int uci_get(const char *key, char *out, size_t outlen)
{
	char path[192], cmd[512];
	int rc;

	if (name_to_path(key, path, sizeof(path)) != 0)
		return -1;

	snprintf(cmd, sizeof(cmd), "%s get %s 2>/dev/null",
		 uci_bin(), path);
	rc = run_uci(cmd, out, outlen);
	if (rc != 0)
		return -1;
	/* 去掉尾随换行 */
	{
		size_t l = strlen(out);
		while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r'))
			out[--l] = '\0';
	}
	return 0;
}

static int uci_set(const char *key, const char *val)
{
	char path[192], cmd[512], q[8200];

	if (name_to_path(key, path, sizeof(path)) != 0)
		return -1;
	shell_quote(val, q, sizeof(q));

	snprintf(cmd, sizeof(cmd), "%s set %s=%s && %s commit %s",
		 uci_bin(), path, q, uci_bin(), UCI_CONFIG_NAME);
	return (run_uci(cmd, NULL, 0) == 0) ? 0 : -1;
}

static int uci_unset(const char *key)
{
	char path[192], cmd[512];

	if (name_to_path(key, path, sizeof(path)) != 0)
		return -1;

	/* 先探测是否存在，避免 delete 失败误报未找到 */
	char probe[512], v[128];
	snprintf(probe, sizeof(probe), "%s get %s 2>/dev/null",
		 uci_bin(), path);
	if (run_uci(probe, v, sizeof(v)) != 0)
		return -1;

	snprintf(cmd, sizeof(cmd), "%s delete %s && %s commit %s",
		 uci_bin(), path, uci_bin(), UCI_CONFIG_NAME);
	return (run_uci(cmd, NULL, 0) == 0) ? 0 : -1;
}

static int parse_export_line(const char *line, char *key, size_t klen,
			     char *val, size_t vlen);

/* uci show padavan 输出解析：
 *   padavan.misc.hostname='Padavan'
 *   padavan.wan0.proto='pppoe'
 * 反向映射为 nvram 名（去掉 padavan. 前缀与 misc. 兜底段） */
static int uci_getall(char *buf, int count)
{
	char cmd[512], out[16384];
	char *line, *save = NULL;
	int off = 0, n = 0;

	snprintf(cmd, sizeof(cmd), "%s show %s 2>/dev/null",
		 uci_bin(), UCI_CONFIG_NAME);
	if (run_uci(cmd, out, sizeof(out)) != 0)
		return 0;

	for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *eq, *key, *val, *p;
		char nvname[NVRAM_MAX_PARAM_LEN];

		/* 跳过 config 定义行（无 . 的）与空行 */
		if (!strchr(line, '.'))
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;

		/* 值去单引号 */
		val = eq + 1;
		p = val + strlen(val);
		if (p > val && p[-1] == '\'') *(--p) = '\0';
		if (*val == '\'') val++;

		/* key 路径：padavan.<sec>.<key>，反向拼 nvram 名 */
		key = line;
		while (*key && *key != '.') key++;
		if (*key) key++;          /* 跳过 "padavan." */
		if (!*key) continue;
		if (strncmp(key, UCI_SECTION_MISC ".", strlen(UCI_SECTION_MISC) + 1) == 0) {
			/* misc 兜底段：名 = option 名（截断到 '='） */
			const char *opt = key + strlen(UCI_SECTION_MISC) + 1;
			snprintf(nvname, sizeof(nvname), "%.*s",
				 (int)(eq - opt), opt);
		} else {
			/* 常规段：sec_key（option 名同样截断到 '='） */
			const char *dot = strchr(key, '.');
			if (!dot || dot > eq)
				continue;
			snprintf(nvname, sizeof(nvname), "%.*s_%.*s",
				 (int)(dot - key), key,
				 (int)(eq - dot - 1), dot + 1);
		}
		if (off + strlen(nvname) + strlen(val) + 2 >= count)
			break;
		sprintf(buf + off, "%s=%s", nvname, val);
		off += strlen(buf + off) + 1;
		n++;
	}
	buf[off] = '\0';
	return n;
}

static int uci_commit(void)
{
	char cmd[512];

	snprintf(cmd, sizeof(cmd), "%s commit %s", uci_bin(), UCI_CONFIG_NAME);
	return (run_uci(cmd, NULL, 0) == 0) ? 0 : -1;
}

static int uci_clear(void)
{
	char cmd[512];

	/* 删除整个 padavan 配置包并提交；defaults 兜底由公开层保证 */
	snprintf(cmd, sizeof(cmd), "%s delete %s && %s commit %s",
		 uci_bin(), UCI_CONFIG_NAME, uci_bin(), UCI_CONFIG_NAME);
	return (run_uci(cmd, NULL, 0) == 0) ? 0 : -1;
}

static int uci_export(const char *path)
{
	char buf[16384];
	int n = uci_getall(buf, sizeof(buf));
	FILE *fp;
	char *p;
	int i;

	if (n < 0)
		return -1;
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	for (i = 0, p = buf; i < n; i++, p += strlen(p) + 1)
		fprintf(fp, "%s\n", p);
	fclose(fp);
	return 0;
}

static int uci_import(const char *path)
{
	FILE *fp;
	char line[512];
	int rc = 0;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		char key[NVRAM_MAX_PARAM_LEN], val[NVRAM_MAX_VALUE_LEN];
		if (parse_export_line(line, key, sizeof(key), val, sizeof(val)) != 0)
			continue;
		if (uci_set(key, val) != 0) {
			rc = -1;
			break;
		}
	}
	fclose(fp);
	return rc;
}

/* 解析导出行 "key=value\n"，供 import 复用（与 parse_kv_line 等价） */
static int parse_export_line(const char *line, char *key, size_t klen, char *val, size_t vlen)
{
	const char *p = line, *eq;
	size_t i;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r')
		return -1;

	eq = strchr(p, '=');
	if (!eq)
		return -1;

	i = 0;
	while (p + i < eq && i < klen - 1) {
		key[i] = p[i];
		i++;
	}
	key[i] = '\0';

	i = 0;
	eq++;
	while (*eq && *eq != '\n' && *eq != '\r' && i < vlen - 1)
		val[i++] = *eq++;
	val[i] = '\0';
	return 0;
}

static const struct nvram_backend backend_uci_cli_impl = {
	.name   = "uci_cli",
	.get    = uci_get,
	.set    = uci_set,
	.unset  = uci_unset,
	.getall = uci_getall,
	.commit = uci_commit,
	.clear  = uci_clear,
	.export = uci_export,
	.import = uci_import,
};

const struct nvram_backend *nvram_backend_uci_cli(void)
{
	return &backend_uci_cli_impl;
}
