#include <xpc/private.h>

int _xpc_runtime_is_app_sandboxed(void)
{
	return 0;
}

xpc_object_t xpc_copy_entitlements_for_pid(pid_t pid) {
	// Not implemented.
	return NULL;
}

kern_return_t xpc_domain_server(mach_msg_header_t *request, mach_msg_header_t *reply) {
	// Not implemented.
	return KERN_FAILURE;
}
