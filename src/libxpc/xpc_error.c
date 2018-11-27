// We cannot include xpc/xpc.h in this file, as otherwise the definitions
// of the _xpc_error_* symbols would not compile correctly. Therefore, I
// must redefine all applicable symbols here. How annoying.

#include <os/object.h>
#include <dispatch/dispatch.h>

#include <sys/mman.h>
#include <uuid/uuid.h>
#include <bsm/audit.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct xpc_object * xpc_object_t;
typedef void (^xpc_handler_t)(xpc_object_t object);

extern xpc_object_t xpc_string_create(const char *);
extern void xpc_dictionary_set_value(xpc_object_t, const char *, xpc_object_t);
extern void xpc_release(xpc_object_t);

#include "xpc_internal.h"

#define xpc_assert_type(xo, type) \
	do { if (((struct xpc_object *)xo)->xo_xpc_type != type) xpc_api_misuse("XPC object type mismatch: Expected %s", #type); } while (0)
#define xpc_assert_nonnull(xo) \
	do { if (xo == NULL) xpc_api_misuse("Parameter cannot be NULL"); } while (0)

const char * const _xpc_error_key_description = "XPC_ERROR_DESCRIPTION";

struct _xpc_dictionary_s {
	struct xpc_object object;
};

struct _xpc_dictionary_s _xpc_error_connection_interrupted = {0};
struct _xpc_dictionary_s _xpc_error_connection_invalid = {0};
struct _xpc_dictionary_s _xpc_error_termination_imminent = {0};

static void _xpc_initialize_error_object(struct xpc_object *object, const char *description) {
	object->xo_xpc_type = _XPC_TYPE_DICTIONARY;
	object->xo_flags = _XPC_STATIC_OBJECT_FLAG;
	object->xo_refcnt = 1;
	object->xo_audit_token = NULL;
	object->xo_size = 1;
	TAILQ_INIT(&object->xo_dict);

	xpc_object_t descriptionObject = xpc_string_create(description);
	xpc_dictionary_set_value(object, _xpc_error_key_description, descriptionObject);
	xpc_release(descriptionObject);
}

__attribute__((visibility("hidden")))
void _xpc_initialize_errors(void) {
	_xpc_initialize_error_object(&_xpc_error_connection_invalid.object, "Connection invalid");
	_xpc_initialize_error_object(&_xpc_error_connection_interrupted.object, "Connection interrupted");
	_xpc_initialize_error_object(&_xpc_error_termination_imminent.object, "Process termination imminent");
}
