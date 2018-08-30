/*
 * Copyright 2014-2015 iXsystems, Inc.
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include <sys/fileport.h>
#include <time.h>
#include "xpc_internal.h"

struct _xpc_type_s {
};

typedef const struct _xpc_type_s xt;
xt _xpc_type_array;
xt _xpc_type_bool;
xt _xpc_type_connection;
xt _xpc_type_data;
xt _xpc_type_date;
xt _xpc_type_dictionary;
xt _xpc_type_endpoint;
xt _xpc_type_null;
xt _xpc_type_error;
xt _xpc_type_fd;
xt _xpc_type_int64;
xt _xpc_type_uint64;
xt _xpc_type_shmem;
xt _xpc_type_string;
xt _xpc_type_uuid;
xt _xpc_type_double;


struct _xpc_bool_s {
};

typedef const struct _xpc_bool_s xb;

xb _xpc_bool_true;
xb _xpc_bool_false;

static size_t xpc_data_hash(const uint8_t *data, size_t length);

static xpc_type_t xpc_typemap[] = {
	NULL,
	XPC_TYPE_DICTIONARY,
	XPC_TYPE_ARRAY,
	XPC_TYPE_BOOL,
	XPC_TYPE_CONNECTION,
	XPC_TYPE_ENDPOINT,
	XPC_TYPE_NULL,
	NULL,
	XPC_TYPE_INT64,
	XPC_TYPE_UINT64,
	XPC_TYPE_DATE,
	XPC_TYPE_DATA,
	XPC_TYPE_STRING,
	XPC_TYPE_UUID,
	XPC_TYPE_FD,
	XPC_TYPE_SHMEM,
	XPC_TYPE_ERROR,
	XPC_TYPE_DOUBLE
};

static const char *xpc_typestr[] = {
	"invalid",
	"dictionary",
	"array",
	"bool",
	"connection",
	"endpoint",
	"null",
	"invalid",
	"int64",
	"uint64",
	"date",
	"data",
	"string",
	"uuid",
	"fd",
	"shmem",
	"error",
	"double"
};

__private_extern__ struct xpc_object *
_xpc_prim_create(int type, xpc_u value, size_t size)
{

	return (_xpc_prim_create_flags(type, value, size, 0));
}

__private_extern__ struct xpc_object *
_xpc_prim_create_flags(int type, xpc_u value, size_t size, uint16_t flags)
{
	struct xpc_object *xo;

	if ((xo = malloc(sizeof(*xo))) == NULL)
		return (NULL);

	xo->xo_size = size;
	xo->xo_xpc_type = type;
	xo->xo_flags = flags;
	xo->xo_u = value;
	xo->xo_refcnt = 1;
	xo->xo_audit_token = NULL;

	if (type == _XPC_TYPE_DICTIONARY)
		TAILQ_INIT(&xo->xo_dict);

	if (type == _XPC_TYPE_ARRAY)
		TAILQ_INIT(&xo->xo_array);

	return (xo);
}

#define xpc_assert_type(xo, type) \
	do { if (xo->xo_xpc_type != type) xpc_api_misuse("XPC object type mismatch: Expected " #type); } while (0)
#define xpc_assert_nonnull(xo) \
	do { if (xo == NULL) xpc_api_misuse("Parameter cannot be NULL"); } while (0)

xpc_object_t
xpc_null_create(void)
{
	xpc_u val = {0};
	return _xpc_prim_create(_XPC_TYPE_NULL, val, 0);
}

xpc_object_t
xpc_bool_create(bool value)
{
	xpc_u val;

	val.b = value;
	return _xpc_prim_create(_XPC_TYPE_BOOL, val, 1);
}

bool
xpc_bool_get_value(xpc_object_t xbool)
{
	struct xpc_object *xo = xbool;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_BOOL);

	return (xo->xo_bool);
}

xpc_object_t
xpc_int64_create(int64_t value)
{
	xpc_u val;

	val.i = value;
	return _xpc_prim_create(_XPC_TYPE_INT64, val, 1);
}

int64_t
xpc_int64_get_value(xpc_object_t xint)
{
	struct xpc_object *xo = xint;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_INT64);

	return (xo->xo_int);
}

xpc_object_t
xpc_uint64_create(uint64_t value)
{
	xpc_u val;

	val.ui = value;
	return _xpc_prim_create(_XPC_TYPE_UINT64, val, 1);
}

uint64_t
xpc_uint64_get_value(xpc_object_t xuint)
{
	struct xpc_object *xo = xuint;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_UINT64);

	return (xo->xo_uint);
}

xpc_object_t
xpc_double_create(double value)
{
	xpc_u val;

	val.d = value;
	return _xpc_prim_create(_XPC_TYPE_DOUBLE, val, 1);
}

double
xpc_double_get_value(xpc_object_t xdouble)
{
	struct xpc_object *xo = xdouble;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_DOUBLE);

	return (xo->xo_d);
}

xpc_object_t
xpc_date_create(int64_t interval)
{
	xpc_u val;

	val.i = interval;
	return _xpc_prim_create(_XPC_TYPE_DATE, val, 1);
}

xpc_object_t
xpc_date_create_from_current(void)
{
    return xpc_date_create(time(NULL) * NSEC_PER_SEC);
}

int64_t
xpc_date_get_value(xpc_object_t xdate)
{
	struct xpc_object *xo = xdate;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_DATE);

	return (xo->xo_int);
}

xpc_object_t
xpc_data_create(const void *bytes, size_t length)
{
	xpc_u val;

	val.ptr = (uintptr_t)malloc(length);
	memcpy((void *)val.ptr, bytes, length);
	return _xpc_prim_create(_XPC_TYPE_DATA, val, length);
}

xpc_object_t
xpc_data_create_with_dispatch_data(dispatch_data_t ddata)
{
	xpc_api_misuse("%s: function unimplemented", __PRETTY_FUNCTION__);
}

size_t
xpc_data_get_length(xpc_object_t xdata)
{
	struct xpc_object *xo = xdata;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_DATA);

	return (xo->xo_size);
}

const void *
xpc_data_get_bytes_ptr(xpc_object_t xdata)
{
	struct xpc_object *xo = xdata;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_DATA);

	return (void *)(xo->xo_ptr);
}

size_t
xpc_data_get_bytes(xpc_object_t xdata, void *buffer, size_t off, size_t length)
{
	struct xpc_object *xo = xdata;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_DATA);

	size_t length_to_copy = length;
	if (length_to_copy > xo->xo_size) length_to_copy = xo->xo_size;

	uint8_t *byte_buffer = (uint8_t *)buffer;
	byte_buffer += off;
	memcpy(byte_buffer, (void *)xo->xo_u.ptr, length_to_copy);

	return length_to_copy;
}

xpc_object_t
xpc_fd_create(int fd)
{
	xpc_u val;
	fileport_makeport(fd, &val.port);
	return _xpc_prim_create(_XPC_TYPE_FD, val, sizeof(val.port));
}

int
xpc_fd_dup(xpc_object_t xfd)
{
	struct xpc_object *xo = xfd;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_FD);

	return fileport_makefd(xo->xo_u.port);
}

xpc_object_t
xpc_string_create(const char *string)
{
	xpc_u val;

	val.str = strdup(string);
	return _xpc_prim_create(_XPC_TYPE_STRING, val, strlen(string));
}

xpc_object_t
xpc_string_create_with_format(const char *fmt, ...)
{
	va_list ap;
	xpc_u val;

	va_start(ap, fmt);
	vasprintf((char **)&val.str, fmt, ap);
	va_end(ap);
	return _xpc_prim_create(_XPC_TYPE_STRING, val, strlen(val.str));
}

xpc_object_t
xpc_string_create_with_format_and_arguments(const char *fmt, va_list ap)
{
	xpc_u val;

	vasprintf((char **)&val.str, fmt, ap);
	return _xpc_prim_create(_XPC_TYPE_STRING, val, strlen(val.str));
}

size_t
xpc_string_get_length(xpc_object_t xstring)
{
	struct xpc_object *xo = xstring;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_STRING);

	return (xo->xo_size);
}

const char *
xpc_string_get_string_ptr(xpc_object_t xstring)
{
	struct xpc_object *xo = xstring;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_STRING);

	return (xo->xo_str);
}

xpc_object_t
xpc_uuid_create(const uuid_t uuid)
{
	xpc_u val;

	memcpy(val.uuid, uuid, sizeof(uuid_t));
	return _xpc_prim_create(_XPC_TYPE_UUID, val, 1);
}

const uint8_t *
xpc_uuid_get_bytes(xpc_object_t xuuid)
{
	struct xpc_object *xo = xuuid;

	xpc_assert_nonnull(xo);
	xpc_assert_type(xo, _XPC_TYPE_UUID);

	return ((uint8_t*)&xo->xo_uuid);
}

xpc_type_t
xpc_get_type(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	return (xpc_typemap[xo->xo_xpc_type]);
}

bool
xpc_equal(xpc_object_t x1, xpc_object_t x2)
{
	struct xpc_object *xo1, *xo2;

	xo1 = x1;
	xo2 = x2;

	xpc_assert_nonnull(xo1);
	xpc_assert_nonnull(xo2);

	if (xo1->xo_xpc_type != xo2->xo_xpc_type) return false;

	switch (xo1->xo_xpc_type) {
		case _XPC_TYPE_BOOL:
			return xo1->xo_u.b == xo2->xo_u.b;
		case _XPC_TYPE_INT64:
		case _XPC_TYPE_DATE:
			return xo1->xo_u.i == xo2->xo_u.i;
		case _XPC_TYPE_UINT64:
			return xo1->xo_u.ui == xo2->xo_u.ui;
		case _XPC_TYPE_ENDPOINT:
			xpc_api_misuse("Cannot compare two endpoints: Not implemented");
		case _XPC_TYPE_STRING:
			return strcmp(xo1->xo_u.str, xo1->xo_u.str) == 0;
		case _XPC_TYPE_DATA:
			return memcmp((void *)xo1->xo_u.ptr, (void *)xo2->xo_u.ptr, xo1->xo_size) == 0;
		case _XPC_TYPE_DICTIONARY:
		{
			struct xpc_dict_pair *pair;
			size_t count1 = 0, count2 = 0;
			TAILQ_FOREACH(pair, &xo1->xo_u.dict, xo_link) {
				count1++;
			}
			TAILQ_FOREACH(pair, &xo2->xo_u.dict, xo_link) {
				count2++;
			}

			if (count1 != count2) return false;

			TAILQ_FOREACH(pair, &xo1->xo_u.dict, xo_link) {
				struct xpc_object *value1 = pair->value;
				struct xpc_object *value2 = xpc_dictionary_get_value(xo2, pair->key);
				if (value2 == NULL) return false;
				if (!xpc_equal(value1, value2)) return false;
			}

			TAILQ_FOREACH(pair, &xo2->xo_u.dict, xo_link) {
				if (xpc_dictionary_get_value(xo1, pair->key) == NULL) return false;
			}

			return true;
		}
		case _XPC_TYPE_ARRAY:
		{
			if (xpc_array_get_count(xo1) != xpc_array_get_count(xo2)) return false;

			return xpc_array_apply(xo1, ^bool(size_t index, xpc_object_t value) {
				return xpc_equal(value, xpc_array_get_value(xo2, index));
			});
		}
	}

    xpc_api_misuse("xpc_equal() is not implemented for this object type");
}

static size_t
xpc_data_hash(const uint8_t *data, size_t length)
{
    size_t hash = 5381;

    while (length--)
        hash = ((hash << 5) + hash) + data[length];

    return (hash);
}

size_t
xpc_hash(xpc_object_t obj)
{
	struct xpc_object *xo;
	__block size_t hash = 0;

	xo = obj;
	switch (xo->xo_xpc_type) {
	case _XPC_TYPE_BOOL:
	case _XPC_TYPE_INT64:
	case _XPC_TYPE_UINT64:
	case _XPC_TYPE_DATE:
	case _XPC_TYPE_ENDPOINT:
		return ((size_t)xo->xo_u.ui);

	case _XPC_TYPE_STRING:
		return (xpc_data_hash(
		    (const uint8_t *)xpc_string_get_string_ptr(obj),
		    xpc_string_get_length(obj)));

	case _XPC_TYPE_DATA:
		return (xpc_data_hash(
		    xpc_data_get_bytes_ptr(obj),
		    xpc_data_get_length(obj)));

	case _XPC_TYPE_DICTIONARY:
		xpc_dictionary_apply(obj, ^(const char *k, xpc_object_t v) {
			hash ^= xpc_data_hash((const uint8_t *)k, strlen(k));
			hash ^= xpc_hash(v);
			return ((bool)true);
		});
		return (hash);

	case _XPC_TYPE_ARRAY:
		xpc_array_apply(obj, ^(size_t idx, xpc_object_t v) {
			hash ^= xpc_hash(v);
			return ((bool)true);
		});
		return (hash);
	}
    
    printf("end of unimplmented xpc_hash()\n");
    return 1; // _sjc_ nothing here, returned this now
}

__private_extern__ const char *
_xpc_get_type_name(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	return (xpc_typestr[xo->xo_xpc_type]);	
}
