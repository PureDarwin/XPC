#include <xpc/xpc.h>

char *__crashreporter_info__;
asm(".desc __crashreporter_info__, 0x10");

static char *xpc_api_misuse_reason = NULL;

__attribute__((visibility("hidden"), noreturn))
void xpc_api_misuse(const char *info, ...) {
	va_list ap;
	va_start(ap, info);
	vasprintf(&xpc_api_misuse_reason, info, ap);
	va_end(ap);

	__crashreporter_info__ = xpc_api_misuse_reason;
	abort();
}

const char *xpc_debugger_api_misuse_info(void) {
	return xpc_api_misuse_reason;
}
