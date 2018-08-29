#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/codesign.h>
#include <sys/errno.h>

int main(int argc, const char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s pid\n", argv[0]);
		return 1;
	}

	pid_t pid = atoi(argv[1]);

	char fakeheader[8];
	int ret = csops(pid, CS_OPS_ENTITLEMENTS_BLOB, fakeheader, sizeof(fakeheader));
	if (ret == -1 && errno != ERANGE) {
		printf("csops() failed: %s\n", strerror(errno));
		return 1;
	}

	uint32_t required_length = ((uint32_t *)&fakeheader)[1];
	required_length = ntohl(required_length);

	if (required_length != 0) {
		char *blob = calloc(1, required_length);
		ret = csops(pid, CS_OPS_ENTITLEMENTS_BLOB, blob, required_length);
		if (ret == -1) {
			printf("csops() failed [with real blob]: %s\n", strerror(errno));
			return 1;
		}

		// The first 8 bytes of the blob are the header; skip those.
		blob += 8;
		fprintf(stdout, "%s", blob);
	} else {
		printf("pid %d has no entitlements\n", pid);
	}

	return 0;
}
