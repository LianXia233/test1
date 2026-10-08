/*
 * main.c - rc-ng 命令行入口
 *
 *   rc-ng dispatch <notify>          # notify_rc 语义分发，如 restart_wan
 *   rc-ng start|stop|restart|reload|check <svc>   # 直连服务编排
 *   rc-ng list                       # 打印服务注册表
 */
#include <stdio.h>
#include <string.h>
#include "padavan_service.h"

static void print_registry(void)
{
	const struct padavan_service *svc;

	for (svc = padavan_services; svc->name; svc++) {
		printf("%-10s procd=%-16s uci=%-10s nvram=%-16s deps=%s\n",
		       svc->name, svc->procd_instance ? svc->procd_instance : "-",
		       svc->uci_config ? svc->uci_config : "-",
		       svc->nvram_enable ? svc->nvram_enable : "-",
		       svc->deps ? svc->deps[0].name : "-");
	}
}

int main(int argc, char **argv)
{
	int rc = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s dispatch <notify> | <action> <svc> | list\n", argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "dispatch") == 0 && argc >= 3) {
		rc = svc_dispatch(argv[2]);
	} else if (strcmp(argv[1], "list") == 0) {
		print_registry();
	} else if (argc >= 3) {
		enum svc_action act;
		const char *acts[] = { "start", "stop", "restart", "reload", "check" };
		int i;

		for (i = 0; i < (int)SVC_ACTION_MAX; i++) {
			if (strcmp(argv[1], acts[i]) == 0) {
				act = (enum svc_action)i;
				break;
			}
		}
		if (i == (int)SVC_ACTION_MAX) {
			fprintf(stderr, "unknown action: %s\n", argv[1]);
			return 1;
		}
		rc = svc_run(act, argv[2]);
	} else {
		fprintf(stderr, "usage: %s dispatch <notify> | <action> <svc> | list\n", argv[0]);
		return 1;
	}

	if (rc < 0)
		fprintf(stderr, "rc-ng: error %d\n", rc);
	return rc < 0 ? 1 : 0;
}
