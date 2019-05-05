#include <xpc/xpc.h>
#include <CrashReporterClient.h>

static char *xpc_api_misuse_reason = NULL;

__attribute__((visibility("hidden"), noreturn))
void xpc_api_misuse(const char *info, ...) {
	va_list ap;
	va_start(ap, info);
	vasprintf(&xpc_api_misuse_reason, info, ap);
	va_end(ap);

	CRSetCrashLogMessage(xpc_api_misuse_reason);
	__builtin_trap();
}

const char *xpc_debugger_api_misuse_info(void) {
	return xpc_api_misuse_reason;
}
