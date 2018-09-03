//
//  main.c
//  xpc_entitlements_test
//
//  Created by William Kent on 8/29/18.
//  Copyright © 2018 PureDarwin. All rights reserved.
//

#include <stdio.h>
#include <sys/errno.h>
#include <xpc/xpc.h>

extern xpc_object_t xpc_copy_entitlements_for_pid(pid_t pid);

int main(int argc, const char * argv[]) {
	pid_t pid = getpid();
	if (argc > 1) {
		pid = atoi(argv[1]);
	}

	xpc_object_t entitlements = xpc_copy_entitlements_for_pid(pid);
	if (entitlements == NULL) {
		if (errno == 0) {
			printf("pid %d has no entitlements\n", pid);
			return 0;
		} else {
			printf("xpc_copy_entitlements_for_pid(%d): error: %s\n", pid, strerror(errno));
			return 0;
		}
	}

	if (xpc_get_type(entitlements) == XPC_TYPE_DATA) {
		fwrite(xpc_data_get_bytes_ptr(entitlements), 1, xpc_data_get_length(entitlements), stdout);
	} else {
		fprintf(stderr, "Entitlements object not XPC_TYPE_DATA: this shouldn't happen\n");
	}

	return 0;
}
