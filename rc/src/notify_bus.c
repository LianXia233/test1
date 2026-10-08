/*
 * notify_bus.c - notify_rc 兼容入口 + 事件总线（ubus 桥）
 *
 * 原版 rc 靠 notify_rc("xxx") 通知各 rc 子进程轮询动作；
 * 重写版由 rc-ng 进程内直接分发（bus_send_local），
 * 并保留 ubus 后端：正式版 rc-ng 常驻后，各服务经
 * `ubus call padavan notify` 事件驱动，替代文件/信号轮询。
 *
 * 后端选择：环境变量 PADAVAN_NOTIFY_BUS=ubus 时走 ubus，
 * 默认 local（进程内分发，骨架阶段即插即用、可测）。
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "padavan_service.h"

typedef int (*bus_send_fn)(const char *notify);

static int bus_send_local(const char *notify)
{
	return svc_dispatch(notify);
}

static int bus_send_ubus(const char *notify)
{
	char cmd[256];

	/* 骨架：调用外部 ubus 工具发送事件，需系统运行 ubusd */
	snprintf(cmd, sizeof(cmd),
	         "ubus call padavan notify '{\"action\":\"%s\"}' >/dev/null 2>&1", notify);
	return system(cmd);
}

/* notify_rc 兼容入口：原版为 void，保持签名一致 */
void notify_rc(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	const char *bus;
	bus_send_fn send = bus_send_local;

	bus = getenv("PADAVAN_NOTIFY_BUS");
	if (bus && strcmp(bus, "ubus") == 0)
		send = bus_send_ubus;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	send(buf);
}

/* ubus 事件发送：将 svc+act 编码为 notify 语义 */
int ubus_event_send(const char *svc, enum svc_action act)
{
	const char *act_str;

	switch (act) {
	case SVC_START:   act_str = "start"; break;
	case SVC_STOP:    act_str = "stop"; break;
	case SVC_RESTART: act_str = "restart"; break;
	case SVC_RELOAD:  act_str = "reload"; break;
	default:          act_str = "check"; break;
	}

	if (!svc_find(svc))
		return -1;

	notify_rc("%s_%s", act_str, svc);
	return 0;
}

/* ubus 事件接收循环：骨架阶段仅返回，正式版常驻 ubus 监听 */
int ubus_event_recv_loop(void)
{
	fprintf(stderr, "[rc-ng] ubus recv loop 骨架：正式版常驻监听 'padavan' 对象\n");
	return 0;
}
