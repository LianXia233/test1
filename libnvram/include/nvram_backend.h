/*
 * nvram_backend.h - libnvram 后端抽象接口
 *
 * 配置层支持可插拔后端（见 docs/重写架构设计.md 第 3 章）：
 *   - backend_file     纯文件 KV（Debian/裸机/无 uci 环境，原型默认）
 *   - backend_uci_cli  通过 `uci` 命令操作 /etc/config/padavan（OpenWrt）
 *   - backend_uci      libuci C API（OpenWrt SDK 编译，WITH_UCI 启用）
 *
 * 语义约定（与老毛子 nvram 对齐）：
 *   - get / getall 未命中返回 -1（调用方负责 defaults/temp 兜底）
 *   - set / unset 成功返回 0
 *   - temp 变量不进入后端，由公开层统一走 /tmp/padavan.tmp 文件
 */
#ifndef __NVRAM_BACKEND_H__
#define __NVRAM_BACKEND_H__

#include <stddef.h>

struct nvram_backend {
	const char *name;

	/* 读取 key 到 out，成功 0 / 未命中 -1 */
	int (*get)(const char *key, char *out, size_t outlen);
	/* 写入 key=value，成功 0 */
	int (*set)(const char *key, const char *val);
	/* 删除 key，成功 0 / 未找到 -1 */
	int (*unset)(const char *key);
	/* 全量导出到 buf（k=v\0 数组，返回项数） */
	int (*getall)(char *buf, int count);
	/* 提交暂存改动，成功 0 */
	int (*commit)(void);
	/* 清空全部配置，成功 0 */
	int (*clear)(void);
	/* 导出到文件，成功 0 */
	int (*export)(const char *path);
	/* 从文件导入，成功 0 */
	int (*import)(const char *path);
};

extern const struct nvram_backend *nvram_backend_file(void);
extern const struct nvram_backend *nvram_backend_uci_cli(void);
extern const struct nvram_backend *nvram_backend_uci(void);

#endif /* __NVRAM_BACKEND_H__ */
