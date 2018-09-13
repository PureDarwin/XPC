#include <xpc/xpc.h>
#include "xpc_internal.h"

#define xpc_assert_type(xo, type) \
	do { if (((struct xpc_object *)xo)->xo_xpc_type != type) xpc_api_misuse("XPC object type mismatch: Expected %s", #type); } while (0)
#define xpc_assert_nonnull(xo) \
	do { if (xo == NULL) xpc_api_misuse("Parameter cannot be NULL"); } while (0)

const char * const _xpc_error_key_description = "XPC_ERROR_DESCRIPTION";

// N.B.: This type as used in this file *MUST* have the same layout as struct xpc_object!
// I cannot make them identical due to compiler insistance otherwise.
struct _xpc_dictionary_s {
	uint8_t			xo_xpc_type;
	uint16_t		xo_flags;
	volatile uint32_t	xo_refcnt;
	size_t			xo_size;
	xpc_u			xo_u;
	audit_token_t *		xo_audit_token;
	TAILQ_ENTRY(xpc_object) xo_link;
};

#define XPC_MAGIC_DICT_INTERRUPTED_ERROR		(uint16_t)0x10001
#define XPC_MAGIC_DICT_INVALID_ERROR			(uint16_t)0x10002
#define XPC_MAGIC_DICT_TERMINATION_IMMINENT		(uint16_t)0x10003

const struct _xpc_dictionary_s _xpc_error_connection_interrupted = {
	_XPC_TYPE_STATIC_ERROR,
	XPC_MAGIC_DICT_INTERRUPTED_ERROR
};
const struct _xpc_dictionary_s _xpc_error_connection_invalid = {
	_XPC_TYPE_STATIC_ERROR,
	XPC_MAGIC_DICT_INVALID_ERROR
};
const struct _xpc_dictionary_s _xpc_error_termination_imminent = {
	_XPC_TYPE_STATIC_ERROR,
	XPC_MAGIC_DICT_TERMINATION_IMMINENT
};

xpc_object_t xpc_hydrate_static_error(xpc_object_t input) {
	xpc_assert_nonnull(input);
	xpc_assert_type(input, _XPC_TYPE_STATIC_ERROR);

	struct xpc_object *xo = input;

	if (xo->xo_flags == XPC_MAGIC_DICT_INVALID_ERROR) {
		const char *key = XPC_ERROR_KEY_DESCRIPTION;
		xpc_object_t value = xpc_string_create("Connection invalid");
		xpc_object_t result = xpc_dictionary_create(&key, &value, 1);
		xpc_release(value);
		return result;
	} else if (xo->xo_flags == XPC_MAGIC_DICT_INTERRUPTED_ERROR) {
		const char *key = XPC_ERROR_KEY_DESCRIPTION;
		xpc_object_t value = xpc_string_create("Connection interrupted");
		xpc_object_t result = xpc_dictionary_create(&key, &value, 1);
		xpc_release(value);
		return result;
	} else if (xo->xo_flags == XPC_MAGIC_DICT_TERMINATION_IMMINENT) {
		const char *key = XPC_ERROR_KEY_DESCRIPTION;
		xpc_object_t value = xpc_string_create("Process termination imminent");
		xpc_object_t result = xpc_dictionary_create(&key, &value, 1);
		xpc_release(value);
		return result;
	}

	xpc_api_misuse("Invalid XPC magic dict ID %d", xo->xo_flags);
}
