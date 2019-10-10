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

#ifndef	_LIBXPC_XPC_INTERNAL_H
#define	_LIBXPC_XPC_INTERNAL_H

#include "nv.h"
#include <os/log.h>


#include <queue.h> // to get TAILQ_HEAD()



#define debugf(msg, ...) \
	os_log(_xpc_debug_logger(), msg, ##__VA_ARGS__)

#define XPC_RESERVED_KEY_PREFIX "__XPC_IMPL$"
#define	XPC_SEQID	XPC_RESERVED_KEY_PREFIX "sequence number"
#define	XPC_RPORT	XPC_RESERVED_KEY_PREFIX "remote port"

#define _XPC_TYPE_INVALID (&_xpc_type_invalid)
__XNU_PRIVATE_EXTERN extern XPC_TYPE(_xpc_type_invalid);

struct xpc_object;
struct xpc_dict_pair;

TAILQ_HEAD(xpc_dict_head, xpc_dict_pair);
TAILQ_HEAD(xpc_array_head, xpc_object);

typedef union {
	struct xpc_dict_head dict;
	struct xpc_array_head array;
	uint64_t ui;
	int64_t i;
	const char *str;
	bool b;
	double d;
	uintptr_t ptr;
	uuid_t uuid;
	mach_port_t port;
} xpc_u;	


#define _XPC_FROM_WIRE 0x1
#define _XPC_STATIC_OBJECT_FLAG 0x2

struct xpc_object {
	xpc_type_t		xo_xpc_type;
	uint16_t		xo_flags;
	_Atomic(uint32_t)	xo_refcnt;
	size_t			xo_size;
	xpc_u			xo_u;
	audit_token_t *		xo_audit_token;
	TAILQ_ENTRY(xpc_object) xo_link;
};

struct xpc_dict_pair {
	const char *		key;
	struct xpc_object *	value;
	TAILQ_ENTRY(xpc_dict_pair) xo_link;
};

struct xpc_pending_call {
	uint64_t		xp_id;
	xpc_object_t		xp_response;
	dispatch_queue_t	xp_queue;
	xpc_handler_t		xp_handler;
	TAILQ_ENTRY(xpc_pending_call) xp_link;
};

struct xpc_connection {
	const char *		xc_name;
	mach_port_t		xc_remote_port;
	mach_port_t		xc_local_port;
	xpc_handler_t		xc_handler;
	dispatch_source_t	xc_recv_source;
	dispatch_queue_t	xc_send_queue;
	dispatch_queue_t	xc_recv_queue;
	dispatch_queue_t	xc_target_queue;
	int			xc_suspend_count;
	int			xc_transaction_count;
	int 			xc_flags;
	_Atomic(uint64_t)	xc_last_id;
	void *			xc_context;
	struct xpc_connection * xc_parent;
	uid_t			xc_remote_euid;
	gid_t			xc_remote_guid;
	pid_t			xc_remote_pid;
	au_asid_t		xc_remote_asid;
	TAILQ_HEAD(, xpc_pending_call) xc_pending;
	TAILQ_HEAD(, xpc_connection) xc_peers;
	TAILQ_ENTRY(xpc_connection) xc_link;
};

struct xpc_service {
	mach_port_t		xs_remote_port;
	TAILQ_HEAD(, xpc_connection) xs_connections;
};

#define xo_nv xo_u.nv
#define xo_str xo_u.str
#define xo_bool xo_u.b
#define xo_uint xo_u.ui
#define xo_int xo_u.i
#define xo_ptr xo_u.ptr
#define xo_d xo_u.d
#define xo_fd xo_u.fd
#define xo_uuid xo_u.uuid
#define xo_port xo_u.port
#define xo_array xo_u.array
#define xo_dict xo_u.dict

__private_extern__ struct xpc_object *_xpc_prim_create(xpc_type_t type, xpc_u value,
    size_t size);
__private_extern__ struct xpc_object *_xpc_prim_create_flags(xpc_type_t type,
    xpc_u value, size_t size, uint16_t flags);
__private_extern__ const char *_xpc_get_type_name(xpc_object_t obj);
__private_extern__ os_log_t _xpc_debug_logger(void);
__private_extern__ struct xpc_object *nv2xpc(const nvlist_t *nv, mach_port_t (^port_deserializer)(int64_t port_id));
__private_extern__ nvlist_t *xpc2nv(struct xpc_object *xo, int64_t (^port_serializer)(mach_port_t port));
__private_extern__ void xpc_object_destroy(struct xpc_object *xo);
__private_extern__ int xpc_pipe_send(xpc_object_t obj, mach_port_t dst,
    mach_port_t local, uint64_t id);
__private_extern__ int xpc_pipe_receive(mach_port_t local, mach_port_t *remote,
    xpc_object_t *result, uint64_t *id);
__private_extern__ void xpc_api_misuse(const char *info, ...) __attribute__((noreturn, format(printf, 1, 2)));
__private_extern__ void xpc_dictionary_set_value_nokeycheck(xpc_object_t xdict, const char *key, xpc_object_t value);

#define xpc_precondition(cond, message, ...) \
	do { if (!(cond)) xpc_api_misuse("Bug in client of libxpc: " message, ##__VA_ARGS__); } while (0)
#define xpc_assert(cond, message, ...) \
	do { if (!(cond)) xpc_api_misuse("Bug in libxpc: " message, ##__VA_ARGS__); } while (0)

#define xpc_assert_nonnull(xo) \
	xpc_precondition(xo != NULL, "Parameter cannot be NULL")
#define xpc_assert_type(xo, type) \
	xpc_precondition(xo->xo_xpc_type == type, "object type mismatch: Expected %s", #type);

#endif	/* _LIBXPC_XPC_INTERNAL_H */
