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
#include <sys/errno.h>
#include <sys/sbuf.h>
#include <mach/mach.h>
#include <mach/message.h>
#include <xpc/launchd.h>
#include <assert.h>
#include <syslog.h>
#include <stdarg.h>
#include <uuid/uuid.h>
#include <stdatomic.h>

#include "xpc_internal.h"

#define MAX_RECV 8192
#define XPC_RECV_SIZE			\
    MAX_RECV - 				\
    sizeof(mach_msg_header_t) - 	\
    sizeof(mach_msg_trailer_t) - 	\
    sizeof(uint64_t) - 			\
    sizeof(size_t)

struct xpc_message {
	mach_msg_header_t header;
	size_t size;
	uint64_t id;
	char data[0];
	mach_msg_trailer_t trailer;
};

struct xpc_recv_message {
	mach_msg_header_t header;
	size_t size;
	uint64_t id;
	char data[XPC_RECV_SIZE];
	mach_msg_trailer_t trailer;
};

static void xpc_copy_description_level(xpc_object_t obj, struct sbuf *sbuf, int level);

static void
xpc_dictionary_destroy(struct xpc_object *dict)
{
	struct xpc_dict_head *head;
	struct xpc_dict_pair *p, *ptmp;

	head = &dict->xo_dict;

	TAILQ_FOREACH_SAFE(p, head, xo_link, ptmp) {
		TAILQ_REMOVE(head, p, xo_link);
		xpc_release(p->value);
		free(p);
	}
}

static void
xpc_array_destroy(struct xpc_object *dict)
{
	struct xpc_object *p, *ptmp;
	struct xpc_array_head *head;

	head = &dict->xo_array;

	TAILQ_FOREACH_SAFE(p, head, xo_link, ptmp) {
		TAILQ_REMOVE(head, p, xo_link);
		xpc_release(p);
	}
}

void
xpc_object_destroy(struct xpc_object *xo)
{
	if (xo->xo_xpc_type == XPC_TYPE_DICTIONARY)
		xpc_dictionary_destroy(xo);

	if (xo->xo_xpc_type == XPC_TYPE_ARRAY)
		xpc_array_destroy(xo);

	if (xo->xo_xpc_type == XPC_TYPE_STRING)
		free(xo->xo_u.str);

	if (xo->xo_xpc_type == XPC_TYPE_DATA)
		free((void *)xo->xo_u.ptr);

	if (xo->xo_audit_token != NULL)
		free(xo->xo_audit_token);

	free(xo);
}

xpc_object_t
xpc_retain(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	if ((xo->xo_flags & _XPC_STATIC_OBJECT_FLAG) == _XPC_STATIC_OBJECT_FLAG)
		// Don't change the reference count of statically compiled objects.
		return obj;

	//atomic_add_int(&xo->xo_refcnt, 1); _sjc_ removed because linker couldn't find atomic_add_int()
	xo->xo_refcnt++;
	return (obj);
}

void
xpc_release(xpc_object_t obj)
{
	struct xpc_object *xo;
	xo = obj;

	if ((xo->xo_flags & _XPC_STATIC_OBJECT_FLAG) == _XPC_STATIC_OBJECT_FLAG)
		// Don't change the reference count of statically compiled objects.
		return;

	// atomic_fetch_sub() works like 'xo_refcnt--', in that the *old* value is returned.
	int refcount = atomic_fetch_sub(&xo->xo_refcnt, 1) - 1;
	if (refcount != 0)
		return;

	xpc_object_destroy(xo);
}

static const char *xpc_errors[] = {
	"No Error Found",
	"No Memory",
	"Invalid Argument",
	"No Such Process"
};


const char *
xpc_strerror(int error)
{

	if (error > EXMAX || error < 0)
		return "BAD ERROR";
	return (xpc_errors[error]);
}

static void
xpc_copy_description_level(xpc_object_t obj, struct sbuf *sbuf, int level)
{
	struct xpc_object *xo = obj;

	if (obj == NULL) {
		sbuf_printf(sbuf, "<null value>\n");
		return;
	}

	sbuf_printf(sbuf, "(%s) ", _xpc_get_type_name(obj));

	if (xo->xo_xpc_type == XPC_TYPE_DICTIONARY) {
		sbuf_printf(sbuf, "\n");
		xpc_dictionary_apply(xo, ^(const char *k, xpc_object_t v) {
			sbuf_printf(sbuf, "%*s\"%s\": ", level * 4, " ", k);
			xpc_copy_description_level(v, sbuf, level + 1);
			return ((bool)true);
		});
	} else if (xo->xo_xpc_type == XPC_TYPE_ARRAY) {
		sbuf_printf(sbuf, "\n");
		xpc_array_apply(xo, ^(size_t idx, xpc_object_t v) {
			sbuf_printf(sbuf, "%*s%ld: ", level * 4, " ", idx);
			xpc_copy_description_level(v, sbuf, level + 1);
			return ((bool)true);
		});
	} else if (xo->xo_xpc_type == XPC_TYPE_BOOL) {
		sbuf_printf(sbuf, "%s\n", xpc_bool_get_value(obj) ? "true" : "false");
	} else if (xo->xo_xpc_type == XPC_TYPE_STRING) {
		sbuf_printf(sbuf, "\"%s\"\n", xpc_string_get_string_ptr(obj));
	} else if (xo->xo_xpc_type == XPC_TYPE_INT64) {
		sbuf_printf(sbuf, "0x%llX\n", xpc_int64_get_value(obj));
	} else if (xo->xo_xpc_type == XPC_TYPE_UINT64) {
		sbuf_printf(sbuf, "0x%llX\n", xpc_uint64_get_value(obj));
	} else if (xo->xo_xpc_type == XPC_TYPE_DATE) {
		sbuf_printf(sbuf, "%llu\n", xpc_date_get_value(obj));
	} else if (xo->xo_xpc_type == XPC_TYPE_UUID) {
		uuid_t id;
		uuid_string_t uuid_str;
		memcpy(id, xpc_uuid_get_bytes(obj), sizeof(uuid_t));
		uuid_unparse_upper(id, uuid_str);
		sbuf_printf(sbuf, "%s\n", uuid_str);
		free(uuid_str);
	} else if (xo->xo_xpc_type == XPC_TYPE_ENDPOINT) {
		sbuf_printf(sbuf, "<%lld>\n", xo->xo_int);
	} else if (xo->xo_xpc_type == XPC_TYPE_NULL) {
		sbuf_printf(sbuf, "<null>\n");
	} else {
		xpc_api_misuse("Unknown XPC type");
	}
}

extern struct sbuf *sbuf_new_auto(void);

char *
xpc_copy_description(xpc_object_t obj)
{
	char *result;
	struct sbuf *sbuf;

	sbuf = sbuf_new_auto();
	xpc_copy_description_level(obj, sbuf, 0);
	sbuf_finish(sbuf);
	result = strdup(sbuf_data(sbuf));
	sbuf_delete(sbuf);

	return (result);
}

xpc_object_t
xpc_copy_entitlement_for_token(const char *key __unused, audit_token_t *token __unused)
{
	xpc_u val;

	val.b = true;
	return (_xpc_prim_create(XPC_TYPE_BOOL, val,0));
}

extern kern_return_t
mach_msg_send(mach_msg_header_t *header);

int
xpc_pipe_routine_reply(xpc_object_t xobj)
{
	xpc_assert_nonnull(xobj);

	struct xpc_object *xo;
	size_t size, msg_size;
	struct xpc_message *message;
	kern_return_t kr;
	int err;

	xo = xobj;
	xpc_assert(xo->xo_xpc_type == XPC_TYPE_DICTIONARY, "xpc_object_t not of %s type", "dictionary");
	nvlist_t *nvlist = xpc2nv(xobj);
	if (nvlist == NULL)
		return (EINVAL);
	size = nvlist_size(nvlist);
	msg_size = __DARWIN_ALIGN(size + sizeof(mach_msg_header_t) + sizeof(size_t) + sizeof(uint64_t));
	if ((message = calloc(msg_size, 1)) == NULL)
		return (ENOMEM);

	void *packed = nvlist_pack_buffer(nvlist, NULL, &size);
	if (packed == NULL) {
		debugf("Could not pack XPC message for transport");
		free(message);
		nvlist_destroy(nvlist);
		return EINVAL;
	}

	message->header.msgh_size = (mach_msg_size_t)msg_size;
	message->header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
	message->header.msgh_remote_port = xpc_dictionary_copy_mach_send(xobj, XPC_RPORT);
	xpc_assert(message->header.msgh_remote_port != MACH_PORT_NULL, "'%s' key not found in reply", XPC_RPORT);
	message->header.msgh_local_port = MACH_PORT_NULL;
	message->size = size;
	message->id = xpc_dictionary_get_uint64(xobj, XPC_SEQID);
	xpc_assert(message->id != 0, "'%s' key not found in reply", XPC_SEQID);
	memcpy(&message->data, packed, size);
	kr = mach_msg_send(&message->header);
	if (kr != KERN_SUCCESS)
		err = (kr == KERN_INVALID_TASK) ? EPIPE : EINVAL;
	else
		err = 0;
	free(message);
	free(packed);
	nvlist_destroy(nvlist);
	return (err);
}

int
xpc_pipe_send(xpc_object_t xobj, mach_port_t dst, mach_port_t local,
    uint64_t id)
{
	struct xpc_object *xo;
	size_t size, msg_size;
	struct xpc_message *message;
	kern_return_t kr;
	int err;

	xo = xobj;
	xpc_assert(xo->xo_xpc_type == XPC_TYPE_DICTIONARY, "xpc_object_t not of %s type", "dictionary");

	nvlist_t *nvl = xpc2nv(xo);
	size = nvlist_size(nvl);

	msg_size = __DARWIN_ALIGN(size + sizeof(mach_msg_header_t) + sizeof(size_t) + sizeof(uint64_t));
	if ((message = calloc(msg_size, 1)) == NULL)
		return (ENOMEM);

	void *packed = nvlist_pack_buffer(nvl, NULL, &size);
	if (packed == NULL) {
		debugf("Could not pack XPC message for transport");
		free(message);
		nvlist_destroy(nvl);
		return (EINVAL);
	}

	if (size > XPC_RECV_SIZE) {
		debugf("XPC message too big, would be truncated upon receive");
		free(message);
		nvlist_destroy(nvl);
		return EINVAL;
	}

	message->header.msgh_size = (mach_msg_size_t)msg_size;
	message->header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
	    MACH_MSG_TYPE_MAKE_SEND);
	message->header.msgh_remote_port = dst;
	message->header.msgh_local_port = local;
	message->id = id;
	message->size = size;
	memcpy(&message->data, packed, size);
	kr = mach_msg_send(&message->header);
	if (kr != KERN_SUCCESS) {
		debugf("mach_msg_send() failed, kr=0x%X", kr);
		err = (kr == KERN_INVALID_TASK) ? EPIPE : EINVAL;
	} else
		err = 0;
	free(packed);
	free(message);
	nvlist_destroy(nvl);
	return (err);
}

int
xpc_pipe_receive(mach_port_t local, mach_port_t *remote, xpc_object_t *result,
    uint64_t *id)
{
	struct xpc_recv_message message;
	mach_msg_header_t *request;
	kern_return_t kr;
	mach_msg_trailer_t *tr;
	size_t data_size;
	struct xpc_object *xo;
	audit_token_t *auditp;
	xpc_u val;

	request = &message.header;
	/* should be size - but what about arbitrary XPC data? */
	request->msgh_size = MAX_RECV;
	request->msgh_local_port = local;
	kr = mach_msg(request, MACH_RCV_MSG |
	    MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
	    MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
	    0, request->msgh_size, request->msgh_local_port,
	    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

	if (kr != 0)
		debugf("mach_msg_receive returned %d\n", kr);
	*remote = request->msgh_remote_port;
	*id = message.id;
	data_size = message.size;
	debugf("unpacking data_size=%zu", data_size);

	nvlist_t *nv = nvlist_unpack(&message.data, data_size);
	xo = nv2xpc(nv);
	nvlist_destroy(nv);

	tr = (mach_msg_trailer_t *)(((char *)&message) + request->msgh_size);
	auditp = &((mach_msg_audit_trailer_t *)tr)->msgh_audit;

	xo->xo_audit_token = malloc(sizeof(*auditp));
	memcpy(xo->xo_audit_token, auditp, sizeof(*auditp));

	xpc_dictionary_set_mach_send(xo, XPC_RPORT, request->msgh_remote_port);
	xpc_dictionary_set_uint64(xo, XPC_SEQID, message.id);
	xo->xo_flags |= _XPC_FROM_WIRE;
	*result = xo;
	return (0);
}

int
xpc_pipe_try_receive(mach_port_t portset, xpc_object_t *requestobj, mach_port_t *rcvport,
	boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *), mach_msg_size_t msgsize __unused,
	int flags __unused)
{
	struct xpc_recv_message message;
	struct xpc_recv_message rsp_message;
	mach_msg_header_t *request;
	kern_return_t kr;
	mach_msg_header_t *response;
	mach_msg_trailer_t *tr;
	int data_size;
	struct xpc_object *xo;
	audit_token_t *auditp;

	request = &message.header;
	response = &rsp_message.header;
	/* should be size - but what about arbitrary XPC data? */
	request->msgh_size = MAX_RECV;
	request->msgh_local_port = portset;
	kr = mach_msg(request, MACH_RCV_MSG |
	    MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
	    MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
	    0, request->msgh_size, request->msgh_local_port,
	    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

	if (kr != 0)
		debugf("mach_msg_receive returned %d\n", kr);
	*rcvport = request->msgh_remote_port;
	if (demux(request, response)) {
		(void)mach_msg_send(response);
		/*  can't do anything with the return code
		* just tell the caller this has been handled
		*/
		return (TRUE);
	}
	debugf("demux returned false\n");
	data_size = message.size;
	debugf("unpacking data_size=%d", data_size);

	nvlist_t *nvlist = nvlist_unpack(&message.data, data_size);
	xo = nv2xpc(nvlist);
	nvlist_destroy(nvlist);

	/* is padding for alignment enforced in the kernel?*/
	tr = (mach_msg_trailer_t *)(((char *)&message) + request->msgh_size);
	auditp = &((mach_msg_audit_trailer_t *)tr)->msgh_audit;

	xo->xo_audit_token = malloc(sizeof(*auditp));
	memcpy(xo->xo_audit_token, auditp, sizeof(*auditp));

	xpc_dictionary_set_mach_send(xo, XPC_RPORT, request->msgh_remote_port);
	xpc_dictionary_set_uint64(xo, XPC_SEQID, message.id);
	xo->xo_flags |= _XPC_FROM_WIRE;
	*requestobj = xo;
	return (0);
}

int
xpc_call_wakeup(mach_port_t rport, int retcode)
{
	mig_reply_error_t msg;
	int err;
	kern_return_t kr;

	msg.Head.msgh_remote_port = rport;
	msg.RetCode = retcode;
	kr = mach_msg_send(&msg.Head);
	if (kr != KERN_SUCCESS)
		err = (kr == KERN_INVALID_TASK) ? EPIPE : EINVAL;
	else
		err = 0;

	return (err);
}
