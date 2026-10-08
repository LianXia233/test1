/*
 * nvram_uci.c - Padavan nvram UCI 后端（libuci C API）
 *
 * 编译条件：WITH_UCI（OpenWrt/ImmortalWrt SDK 环境），链接 -luci。
 * 宿主开发机无 libuci 时默认不编译，用 nvram_uci_cli.c 或 nvram_core.c。
 *
 * 映射规则与 nvram_uci_cli.c 完全一致：
 *   - name 带下划线：第一个下划线前为 section、之后为 option
 *   - name 无下划线：padavan.misc.<name>
 *
 * 与 CLI 后端的差异：本实现支持"暂存 + 显式 commit"语义——
 * nvram_set/unset 只改内存暂存，nvram_commit 才落盘 /etc/config。
 *
 * 注意：本文件内部函数统一加 nv_ 前缀，避免与 libuci 同名库符号冲突。
 */
#ifdef WITH_UCI
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uci.h>
#include "nvram_linux.h"
#include "nvram_backend.h"

#define UCI_CONFIG_NAME  "padavan"
#define UCI_SECTION_MISC "misc"

/* name -> padavan.<sec>.<key>；返回指向静态缓冲的 path，失败返回 NULL */
static const char *name_to_path(const char *name)
{
	static char path[192];
	const char *us;
	size_t slen;

	if (!name || !*name)
		return NULL;

	us = strchr(name, '_');
	if (us) {
		slen = (size_t)(us - name);
		if (slen == 0 || slen >= 64)
			return NULL;
		snprintf(path, sizeof(path), "%.*s.%s", (int)slen, name, us + 1);
	} else {
		snprintf(path, sizeof(path), "%s.%s", UCI_SECTION_MISC, name);
	}
	return path;
}

/* 统一的上下文辅助：lookup 后返回 option 指针（上下文随 ctx 释放） */
static struct uci_option *lookup_opt(struct uci_context *ctx, const char *key)
{
	struct uci_ptr ptr = { 0 };
	const char *path = name_to_path(key);

	if (!path)
		return NULL;
	if (uci_lookup_ptr(ctx, &ptr, path, true) != UCI_OK)
		return NULL;
	return ptr.o;
}

static int nv_uci_get(const char *key, char *out, size_t outlen)
{
	struct uci_context *ctx;
	struct uci_option *o;
	int rc = -1;

	ctx = uci_alloc_context();
	if (!ctx)
		return -1;
	o = lookup_opt(ctx, key);
	if (o && o->type == UCI_TYPE_STRING && o->v.string) {
		snprintf(out, outlen, "%s", o->v.string);
		rc = 0;
	}
	uci_free_context(ctx);
	return rc;
}

static int nv_uci_set(const char *key, const char *val)
{
	struct uci_context *ctx;
	struct uci_ptr ptr = { 0 };
	const char *path = name_to_path(key);
	int rc = -1;

	if (!path)
		return -1;
	ctx = uci_alloc_context();
	if (!ctx)
		return -1;

	if (uci_lookup_ptr(ctx, &ptr, path, true) == UCI_OK) {
		ptr.value = (char *)val;
		if (uci_set(ctx, &ptr) == UCI_OK)
			rc = 0;
	}
	/* 注意：暂存态，nvram_commit() 时才 uci_commit() 落盘 */
	uci_free_context(ctx);
	return rc;
}

static int nv_uci_unset(const char *key)
{
	struct uci_context *ctx;
	struct uci_ptr ptr = { 0 };
	const char *path = name_to_path(key);
	int rc = -1;

	if (!path)
		return -1;
	ctx = uci_alloc_context();
	if (!ctx)
		return -1;

	if (uci_lookup_ptr(ctx, &ptr, path, true) == UCI_OK && ptr.o) {
		if (uci_delete(ctx, &ptr) == UCI_OK)
			rc = 0;
	}
	uci_free_context(ctx);
	return rc;
}

static int nv_uci_getall(char *buf, int count)
{
	struct uci_context *ctx;
	struct uci_package *pkg;
	struct uci_element *e, *o;
	int off = 0, n = 0;

	ctx = uci_alloc_context();
	if (!ctx)
		return 0;

	if (uci_load(ctx, UCI_CONFIG_NAME, &pkg) != UCI_OK) {
		uci_free_context(ctx);
		return 0;
	}

	uci_foreach_element(&pkg->sections, e) {
		struct uci_section *s = uci_to_section(e);
		uci_foreach_element(&s->options, o) {
			struct uci_option *opt = uci_to_option(o);
			char nvname[NVRAM_MAX_PARAM_LEN];

			if (opt->type != UCI_TYPE_STRING || !opt->v.string)
				continue;

			/* section 名为 misc 时去掉前缀，否则 sec_key */
			if (strcmp(s->e.name, UCI_SECTION_MISC) == 0)
				snprintf(nvname, sizeof(nvname), "%s", opt->e.name);
			else
				snprintf(nvname, sizeof(nvname), "%s_%s",
					 s->e.name, opt->e.name);

			if ((size_t)off + strlen(nvname) + strlen(opt->v.string) + 2 >=
			    (size_t)count)
				goto done;
			sprintf(buf + off, "%s=%s", nvname, opt->v.string);
			off += strlen(buf + off) + 1;
			n++;
		}
	}
done:
	buf[off] = '\0';
	uci_free_context(ctx);
	return n;
}

static int nv_uci_commit(void)
{
	struct uci_context *ctx;
	int rc = -1;

	ctx = uci_alloc_context();
	if (!ctx)
		return -1;
	if (uci_commit(ctx, NULL, false) == UCI_OK)
		rc = 0;
	uci_free_context(ctx);
	return rc;
}

static int nv_uci_clear(void)
{
	struct uci_context *ctx;
	struct uci_ptr ptr = { 0 };
	int rc = -1;

	ctx = uci_alloc_context();
	if (!ctx)
		return -1;

	if (uci_lookup_ptr(ctx, &ptr, UCI_CONFIG_NAME, false) == UCI_OK) {
		if (uci_delete(ctx, &ptr) == UCI_OK &&
		    uci_commit(ctx, &ptr.p, false) == UCI_OK)
			rc = 0;
	}
	uci_free_context(ctx);
	return rc;
}

static int nv_uci_export(const char *path)
{
	char buf[16384];
	FILE *fp;
	char *p;
	int i, n = nv_uci_getall(buf, sizeof(buf));

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

static int nv_uci_import(const char *path)
{
	FILE *fp;
	char line[512];
	int rc = 0;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		char *eq = strchr(line, '=');
		if (!eq || eq == line)
			continue;
		*eq = '\0';
		{
			char *val = eq + 1;
			size_t l = strlen(val);
			while (l > 0 && (val[l - 1] == '\n' || val[l - 1] == '\r'))
				val[--l] = '\0';
			if (nv_uci_set(line, val) != 0) {
				rc = -1;
				break;
			}
		}
	}
	fclose(fp);
	if (rc == 0)
		rc = nv_uci_commit();
	return rc;
}

static const struct nvram_backend backend_uci_impl = {
	.name   = "uci",
	.get    = nv_uci_get,
	.set    = nv_uci_set,
	.unset  = nv_uci_unset,
	.getall = nv_uci_getall,
	.commit = nv_uci_commit,
	.clear  = nv_uci_clear,
	.export = nv_uci_export,
	.import = nv_uci_import,
};

const struct nvram_backend *nvram_backend_uci(void)
{
	return &backend_uci_impl;
}

#else /* !WITH_UCI */

/* 宿主环境无 libuci：返回 NULL，核心层会拒绝切换到该后端 */
#include "nvram_backend.h"
const struct nvram_backend *nvram_backend_uci(void)
{
	return NULL;
}

#endif /* WITH_UCI */
