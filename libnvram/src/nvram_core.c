/*
 * nvram_core.c - Padavan nvram 重写版（核心 + 文件 KV 后端）
 *
 * 设计要点：
 *  - API 与上游 nvram_linux.h 完全兼容
 *  - 后端可插拔：backend_file（本文件）/ backend_uci_cli / backend_uci
 *  - 持久配置存 /etc/config/padavan，临时变量存 /tmp/padavan.tmp
 *  - 原子写入（先写临时文件再 rename），flock 防并发
 *  - 默认值表兜底：变量不存在时回退 defaults 数组
 *
 * 文件后端对应「后端 B：纯文件 KV」，生产环境可替换为 UCI 后端。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#include "nvram_linux.h"
#include "nvram_backend.h"

#define DEFAULT_CFG_PATH  "/etc/config/padavan"
#define DEFAULT_TMP_PATH  "/tmp/padavan.tmp"

static char cfg_path[256] = DEFAULT_CFG_PATH;
static char tmp_path[256] = DEFAULT_TMP_PATH;

void nvram_set_config_file(const char *path, const char *tmp_path_)
{
	if (path)
		snprintf(cfg_path, sizeof(cfg_path), "%s", path);
	if (tmp_path_)
		snprintf(tmp_path, sizeof(tmp_path), "%s", tmp_path_);
}

/* 默认值表（原型内置少量示例；完整表从 shared/defaults.c 自动生成） */
static struct nvram_pair builtin_defaults[] = {
	{ "productid",  "Padavan-MT798x" },
	{ "hostname",   "Padavan" },
	{ "lan_ipaddr", "192.168.31.1" },
	{ "lan_netmask","255.255.255.0" },
	{ "wan0_proto", "dhcp" },
	{ NULL, NULL }
};

/* 外部注入的 defaults（nvram_load_defaults 设置，优先级高于内置表） */
static struct nvram_pair *external_defaults = NULL;

/* ---------------- 内部工具 ---------------- */

/* 从文件读取一行 key=value，返回 0 成功；自动跳过 # 注释与空行
 * val 可为 NULL（仅解析 key）；此时不拷贝 value。 */
static int parse_kv_line(const char *line, char *key, size_t klen, char *val, size_t vlen)
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

	if (!val || vlen == 0)
		return 0;

	i = 0;
	eq++;
	while (*eq && *eq != '\n' && *eq != '\r' && i < vlen - 1) {
		val[i++] = *eq++;
	}
	val[i] = '\0';
	return 0;
}

/* 从 KV 文件按 key 读值；返回 0 找到，-1 未找到 */
static int kv_get(const char *path, const char *key, char *out, size_t outlen)
{
	FILE *fp;
	char line[512];
	char k[NVRAM_MAX_PARAM_LEN], v[NVRAM_MAX_VALUE_LEN];
	int fd, ret = -1;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (flock(fd, LOCK_SH) != 0) {
		close(fd);
		return -1;
	}
	fp = fdopen(dup(fd), "r");
	if (!fp) {
		flock(fd, LOCK_UN);
		close(fd);
		return -1;
	}
	while (fgets(line, sizeof(line), fp)) {
		if (parse_kv_line(line, k, sizeof(k), v, sizeof(v)) != 0)
			continue;
		if (strcmp(k, key) == 0) {
			snprintf(out, outlen, "%s", v);
			ret = 0;
			break;
		}
	}
	fclose(fp);
	flock(fd, LOCK_UN);
	close(fd);
	return ret;
}

/* 原子写入整个 KV 文件（内存态全量替换，简单可靠） */
static int kv_write_all(const char *path, const char *pairs[], int npairs)
{
	char tmpl[320];
	int fd, tfd, i;
	ssize_t w;
	char buf[512];

	snprintf(tmpl, sizeof(tmpl), "%s.tmp.%d", path, (int)getpid());
	tfd = open(tmpl, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (tfd < 0)
		return -1;

	for (i = 0; i < npairs; i++) {
		snprintf(buf, sizeof(buf), "%s\n", pairs[i]);
		w = write(tfd, buf, strlen(buf));
		if (w < 0) {
			close(tfd);
			unlink(tmpl);
			return -1;
		}
	}
	fsync(tfd);
	close(tfd);

	if (rename(tmpl, path) != 0) {
		unlink(tmpl);
		return -1;
	}
	return 0;
}

/* 读整个 KV 文件到内存：k=v\0 数组形式，返回项数 */
static int kv_load_all(const char *path, char *buf, int buflen)
{
	FILE *fp;
	char line[512];
	char k[NVRAM_MAX_PARAM_LEN], v[NVRAM_MAX_VALUE_LEN];
	int off = 0, n = 0;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	fp = fdopen(dup(fd), "r");
	if (!fp) {
		close(fd);
		return 0;
	}
	while (fgets(line, sizeof(line), fp)) {
		if (parse_kv_line(line, k, sizeof(k), v, sizeof(v)) != 0)
			continue;
		if (off + strlen(k) + strlen(v) + 2 >= buflen)
			break;
		sprintf(buf + off, "%s=%s", k, v);
		off += strlen(buf + off) + 1;
		n++;
	}
	fclose(fp);
	close(fd);
	buf[off] = '\0';
	return n;
}

/* set 实现：读全量 → 更新/追加 → 原子写回 */
static int kv_set(const char *path, const char *key, const char *val)
{
	char buf[8192], *pairs[1024];
	int n = 0, i, found = 0;
	char *tok;

	n = kv_load_all(path, buf, sizeof(buf));
	/* 解析内存数组为行数组 */
	for (i = 0, tok = buf; i < n; i++, tok += strlen(tok) + 1)
		pairs[i] = tok;

	for (i = 0; i < n; i++) {
		char k[NVRAM_MAX_PARAM_LEN];
		if (parse_kv_line(pairs[i], k, sizeof(k), NULL, 0) == 0 &&
		    strcmp(k, key) == 0) {
			char *newline = malloc(strlen(key) + strlen(val) + 2);
			if (!newline)
				return -1;
			sprintf(newline, "%s=%s", key, val);
			pairs[i] = newline;
			found = 1;
			break;
		}
	}
	if (!found) {
		char *newline = malloc(strlen(key) + strlen(val) + 2);
		if (!newline)
			return -1;
		sprintf(newline, "%s=%s", key, val);
		pairs[n++] = newline;
	}
	kv_write_all(path, (const char **)pairs, n);
	/* 释放临时分配 */
	for (i = 0; i < n; i++) {
		char k[NVRAM_MAX_PARAM_LEN];
		if (strchr(pairs[i], '=') &&
		    parse_kv_line(pairs[i], k, sizeof(k), NULL, 0) == 0 &&
		    strcmp(k, key) == 0) {
			free(pairs[i]);
			break;
		}
	}
	return 0;
}

static int kv_unset(const char *path, const char *key)
{
	char buf[8192], *pairs[1024];
	int n = 0, i, out = 0;
	char *tok;

	n = kv_load_all(path, buf, sizeof(buf));
	for (i = 0, tok = buf; i < n; i++, tok += strlen(tok) + 1)
		pairs[i] = tok;

	for (i = 0; i < n; i++) {
		char k[NVRAM_MAX_PARAM_LEN];
		if (parse_kv_line(pairs[i], k, sizeof(k), NULL, 0) == 0 &&
		    strcmp(k, key) == 0)
			continue; /* 跳过目标项 */
		pairs[out++] = pairs[i];
	}
	if (out == n)
		return -1; /* 未找到 key */

	/* 写回一个临时数组，避免越界读旧数组 */
	{
		char *newbuf[1024];
		for (i = 0; i < out; i++)
			newbuf[i] = pairs[i];
		kv_write_all(path, (const char **)newbuf, out);
	}
	return 0;
}

/* ---------------- 后端 B：纯文件 KV ---------------- */

static int file_get(const char *key, char *out, size_t outlen)
{
	return kv_get(cfg_path, key, out, outlen);
}

static int file_set(const char *key, const char *val)
{
	return kv_set(cfg_path, key, val);
}

static int file_unset(const char *key)
{
	return kv_unset(cfg_path, key);
}

static int file_getall(char *buf, int count)
{
	return kv_load_all(cfg_path, buf, count);
}

static int file_commit(void)
{
	int fd = open(cfg_path, O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
	return 0;
}

static int file_clear(void)
{
	unlink(cfg_path);
	unlink(tmp_path);
	return 0;
}

static int file_export(const char *path)
{
	FILE *src = fopen(cfg_path, "r");
	FILE *dst;
	char buf[4096];
	size_t n;

	if (!src)
		return -1;
	dst = fopen(path, "w");
	if (!dst) {
		fclose(src);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
		fwrite(buf, 1, n, dst);
	fclose(src);
	fclose(dst);
	return 0;
}

static int file_import(const char *path)
{
	FILE *src = fopen(path, "r");
	FILE *dst;
	char buf[4096];
	size_t n;

	if (!src)
		return -1;
	dst = fopen(cfg_path, "w");
	if (!dst) {
		fclose(src);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
		fwrite(buf, 1, n, dst);
	fclose(src);
	fclose(dst);
	return 0;
}

static const struct nvram_backend backend_file_impl = {
	.name   = "file",
	.get    = file_get,
	.set    = file_set,
	.unset  = file_unset,
	.getall = file_getall,
	.commit = file_commit,
	.clear  = file_clear,
	.export = file_export,
	.import = file_import,
};

const struct nvram_backend *nvram_backend_file(void)
{
	return &backend_file_impl;
}

/* ---------------- 后端选择 ---------------- */

static const struct nvram_backend *active_backend = &backend_file_impl;

/* 按名称切换后端：file / uci_cli / uci；未知名称返回 -1 */
int nvram_set_backend(const char *name)
{
	const struct nvram_backend *b;

	if (!name)
		return -1;
	if (strcmp(name, "file") == 0)
		b = nvram_backend_file();
	else if (strcmp(name, "uci_cli") == 0)
		b = nvram_backend_uci_cli();
	else if (strcmp(name, "uci") == 0)
		b = nvram_backend_uci();
	else
		return -1;
	if (!b)
		return -1;
	active_backend = b;
	return 0;
}

const struct nvram_backend *nvram_current_backend(void)
{
	return active_backend;
}

/* ---------------- 公开 API ---------------- */

static char value_buf[NVRAM_MAX_VALUE_LEN];

static const char *lookup_default(const char *name)
{
	struct nvram_pair *d;

	if (external_defaults) {
		for (d = external_defaults; d->name; d++) {
			if (strcmp(d->name, name) == 0)
				return d->value;
		}
	}
	for (d = builtin_defaults; d->name; d++) {
		if (strcmp(d->name, name) == 0)
			return d->value;
	}
	return NULL;
}

char *nvram_get(const char *name)
{
	if (active_backend->get(name, value_buf, sizeof(value_buf)) == 0)
		return value_buf;
	if (kv_get(tmp_path, name, value_buf, sizeof(value_buf)) == 0)
		return value_buf;
	/* defaults 兜底 */
	{
		const char *d = lookup_default(name);
		if (d) {
			snprintf(value_buf, sizeof(value_buf), "%s", d);
			return value_buf;
		}
	}
	return NULL;
}

char *nvram_safe_get(const char *name)
{
	char *v = nvram_get(name);
	return v ? v : "";
}

int nvram_get_int(const char *name)
{
	char *v = nvram_get(name);
	return v ? atoi(v) : 0;
}

int nvram_safe_get_int(const char *name, int val_def, int val_min, int val_max)
{
	char *v = nvram_get(name);
	int r = v ? atoi(v) : val_def;
	if (r < val_min) r = val_min;
	if (r > val_max) r = val_max;
	return r;
}

int nvram_getall(char *buf, int count, int include_temp)
{
	int n = active_backend->getall(buf, count);
	if (include_temp) {
		/* 追加 temp 项（简化：不重排序） */
		char tmpbuf[4096];
		int m = kv_load_all(tmp_path, tmpbuf, sizeof(tmpbuf));
		char *p = buf + strlen(buf) + 1; /* 跳过结束 \0 */
		int off = 0;
		int i;
		for (i = 0; i < m && off < count - 1; i++) {
			int len = strlen(tmpbuf + off);
			if (p + len + 1 > buf + count)
				break;
			memcpy(p, tmpbuf + off, len + 1);
			p += len + 1;
			off += len + 1;
		}
		(void)p;
	}
	return n;
}

int nvram_set(const char *name, const char *value)
{
	return active_backend->set(name, value);
}

int nvram_set_int(const char *name, int value)
{
	char v[32];
	snprintf(v, sizeof(v), "%d", value);
	return nvram_set(name, v);
}

int nvram_unset(const char *name)
{
	return active_backend->unset(name);
}

int nvram_set_temp(const char *name, const char *value)
{
	return kv_set(tmp_path, name, value);
}

int nvram_set_int_temp(const char *name, int value)
{
	char v[32];
	snprintf(v, sizeof(v), "%d", value);
	return nvram_set_temp(name, v);
}

int nvram_match(const char *name, char *match)
{
	char *v = nvram_get(name);
	if (!v)
		return 0;
	return strcmp(v, match) == 0;
}

int nvram_invmatch(const char *name, char *invmatch)
{
	char *v = nvram_get(name);
	if (!v)
		return 1;
	return strcmp(v, invmatch) != 0;
}

int nvram_commit(void)
{
	return active_backend->commit();
}

int nvram_clear(void)
{
	return active_backend->clear();
}

int nvram_load_defaults(struct nvram_pair *defaults)
{
	external_defaults = defaults;
	return 0;
}

int nvram_export(const char *path)
{
	return active_backend->export(path);
}

int nvram_import(const char *path)
{
	return active_backend->import(path);
}
