#include <xpc/xpc.h>
#include <xpc/private.h>
#include <sys/codesign.h>
#include <errno.h>

int _xpc_runtime_is_app_sandboxed(void)
{
	return 0;
}

kern_return_t xpc_domain_server(mach_msg_header_t *request, mach_msg_header_t *reply) {
	// Not implemented.
	return KERN_FAILURE;
}

xpc_object_t xpc_copy_entitlements_for_pid(pid_t pid) {
	char fakeheader[8];
	int ret = csops(pid, CS_OPS_ENTITLEMENTS_BLOB, fakeheader, sizeof(fakeheader));
	if (ret == -1 && errno != ERANGE) {
		return NULL;
	}

	uint32_t required_length = ((uint32_t *)&fakeheader)[1];
	required_length = ntohl(required_length);

	if (required_length != 0) {
		char *blob = alloca(required_length);
		ret = csops(pid, CS_OPS_ENTITLEMENTS_BLOB, blob, required_length);
		if (ret == -1) {
			return NULL;
		}

		// The first 8 bytes of the blob are a binary header; skip it.
		// The rest of the blob is an XML-format plist.
		return xpc_data_create(blob + 8, required_length - 8);
	} else {
		// The process has no entitlements.
		return NULL;
	}
}
