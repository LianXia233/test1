/*
 * nvram_linux.h - Padavan nvram API 兼容头文件（重写版 libnvram）
 *
 * 与 hanwckf/rt-n56u trunk/user/shared/nvram_linux.h 保持签名一致，
 * 使老毛子应用代码无需修改即可链接新库。
 */
#ifndef __NVRAM_LINUX__
#define __NVRAM_LINUX__

struct nvram_backend;

#define NVRAM_MAX_PARAM_LEN	64
#define NVRAM_MAX_VALUE_LEN	4096

struct nvram_pair {
	char *name;
	char *value;
};

extern char *nvram_get(const char *name);
extern char *nvram_safe_get(const char *name);
extern int nvram_get_int(const char *name);
extern int nvram_safe_get_int(const char* name, int val_def, int val_min, int val_max);
extern int nvram_getall(char *buf, int count, int include_temp);

extern int nvram_set(const char *name, const char *value);
extern int nvram_set_int(const char *name, int value);
extern int nvram_unset(const char *name);

extern int nvram_set_temp(const char *name, const char *value);
extern int nvram_set_int_temp(const char *name, int value);

extern int nvram_match(const char *name, char *match);
extern int nvram_invmatch(const char *name, char *invmatch);

extern int nvram_commit(void);
extern int nvram_clear(void);

/* ---- 重写版扩展接口（原版没有，供移植工具与默认值表使用） ---- */
extern int nvram_load_defaults(struct nvram_pair *defaults);
extern int nvram_export(const char *path);
extern int nvram_import(const char *path);
extern void nvram_set_config_file(const char *path, const char *tmp_path);

/* ---- 后端选择（重写版扩展）：file / uci_cli / uci ---- */
extern int nvram_set_backend(const char *name);
extern const struct nvram_backend *nvram_current_backend(void);

#endif /* __NVRAM_LINUX__ */
