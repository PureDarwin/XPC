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

void print_indent(int indent) {
	for (int i = 0; i < indent; i++) printf("    ");
}

void print_dict(xpc_object_t object, int indent);
void print_array(xpc_object_t object, int indent);
void print_object(xpc_object_t object, int indent);

void print_object(xpc_object_t object, int indent) {
	xpc_type_t type = xpc_get_type(object);
	if (type == XPC_TYPE_ARRAY) print_array(object, indent);
	else if (type == XPC_TYPE_DICTIONARY) print_dict(object, indent);
	else if (type == XPC_TYPE_BOOL) printf("%s", xpc_bool_get_value(object) ? "true" : "false");
	else if (type == XPC_TYPE_STRING) printf("\"%s\"", xpc_string_get_string_ptr(object));
	else if (type == XPC_TYPE_ENDPOINT) printf("<endpoint>");
	else if (type == XPC_TYPE_NULL) printf("<null>");
	else if (type == XPC_TYPE_INT64) printf("%lld", xpc_int64_get_value(object));
	else if (type == XPC_TYPE_UINT64) printf("%llu", xpc_uint64_get_value(object));
	else if (type == XPC_TYPE_DOUBLE) printf("%f", xpc_double_get_value(object));
	else if (type == XPC_TYPE_DATE) printf("<date>");
	else if (type == XPC_TYPE_DATA) printf("[%zu-byte data]", xpc_data_get_length(object));
	else if (type == XPC_TYPE_UUID) {
		const uint8_t *uuid = xpc_uuid_get_bytes(object);
		uuid_string_t uuid_string;
		uuid_unparse_upper(uuid, uuid_string);
		printf("{%s}", uuid_string);
	}
	else printf("<unhandled type>");
}

void print_array(xpc_object_t object, int indent) {
	printf("[");
	indent++;

	xpc_array_apply(object, ^bool(size_t index, xpc_object_t  _Nonnull value) {
		print_indent(indent);
		printf("[%zu]: ", index);
		print_object(value, indent);
		printf("\n");
		return true;
	});

	printf("]");
	indent--;
}

void print_dict(xpc_object_t object, int indent) {
	printf("{\n");
	indent++;

	xpc_dictionary_apply(object, ^bool(const char * _Nonnull key, xpc_object_t  _Nonnull value) {
		print_indent(indent);
		printf("%s = ", key);
		print_object(value, indent);
		printf("\n");
		return true;
	});

	indent--;
	print_indent(indent);
	printf("}\n");
}

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
		print_object(entitlements, 0);
		putchar('\n');
	}

	return 0;
}
