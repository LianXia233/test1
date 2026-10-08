/*
 * padavan_service.h - Padavan 重写版服务层 rc-ng 公开接口
 *
 * M3 目标：把原版 rc 巨型 switch(notify_rc 轮询) 拆为
 *   声明式服务注册表 + 动作分发 + 依赖编排，
 *   事件可通过本地进程内分发或 ubus 桥发出。
 *
 * 设计约定：
 *   - 服务以静态注册表声明（padavan_services[]），新增服务=加一行；
 *   - notify_rc("restart_firewall") 兼容原版语义，解析动作+服务名；
 *   - 服务执行前先按 deps 递归满足依赖（拓扑序），执行后触发 cascade 级联；
 *   - handler 为空时走默认执行器（调 script 或 procd 托管）。
 */
#ifndef __PADAVAN_SERVICE_H__
#define __PADAVAN_SERVICE_H__

enum svc_action {
	SVC_START = 0,
	SVC_STOP,
	SVC_RESTART,
	SVC_RELOAD,
	SVC_CHECK,
	SVC_ACTION_MAX
};

struct svc_dep {
	const char *name;   /* 依赖的服务名 */
	int optional;       /* 1=可选依赖，缺失不阻断 */
};

struct padavan_service {
	const char *name;            /* 服务名：wan / firewall / dnsmasq ... */
	const char *procd_instance;  /* procd 实例名（正式版 /etc/init.d/ 脚本） */
	const char *uci_config;      /* 对应 UCI 配置段，便于与 M2 libnvram 联动 */
	const char *nvram_enable;    /* 使能开关 nvram 名；NULL 表示默认启用 */
	const char *script;          /* 启停脚本路径；NULL 表示由 procd 托管 */
	const struct svc_dep *deps;  /* 前置依赖，NULL 结尾 */
	const char *const *cascade;  /* 动作完成后级联通知的服务名，NULL 结尾 */
	int (*handler)(enum svc_action act, const char *name); /* 自定义执行器；NULL 走默认 */
};

/* ---- 动作解析：notify_rc 语义 ---- */
int svc_parse_notify(const char *notify, enum svc_action *act, const char **svc);

/* ---- 顶层入口 ---- */
int svc_dispatch(const char *notify);   /* notify_rc("restart_firewall") 完整分发 */

/* ---- 服务查找与编排 ---- */
const struct padavan_service *svc_find(const char *name);
int svc_run(enum svc_action act, const char *name);   /* 带依赖+级联编排 */

/* ---- 轨迹钩子：骨架阶段供测试注入，正式版可移除 ---- */
typedef void (*svc_trace_fn)(enum svc_action act, const char *name);
void svc_set_trace(svc_trace_fn cb);

/* ---- ubus 事件桥（可选后端） ---- */
int ubus_event_send(const char *svc, enum svc_action act);
int ubus_event_recv_loop(void);

/* ---- 服务注册表（定义于 service_registry.c） ---- */
extern const struct padavan_service padavan_services[];

#endif /* __PADAVAN_SERVICE_H__ */
