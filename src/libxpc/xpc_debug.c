#include <xpc/xpc.h>

char *__crashreporter_info__;
asm(".desc __crashreporter_info__, 0x10");

static char *xpc_api_misuse_reason = NULL;

__attribute__((visibility("hidden")))
void xpc_api_misuse(const char *info) {
	xpc_api_misuse_reason = __crashreporter_info__ = info;
	abort();
}

const char *xpc_debugger_api_misuse_info(void) {
	return xpc_api_misuse_reason;
}
