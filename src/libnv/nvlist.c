/*-
 * Copyright (c) 2009-2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include "sys_endian.h"
#include <sys/queue.h>

#ifdef _KERNEL

#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/stdarg.h>

#else
#include <sys/socket.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#define	_WITH_DPRINTF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//#include "msgio.h"
#endif

#ifdef HAVE_PJDLOG
#include <pjdlog.h>
#endif

#include "nv.h"
#include "nv_impl.h"
#include "nvlist_impl.h"
#include "nvpair_impl.h"

#ifndef	HAVE_PJDLOG
#ifdef _KERNEL
#define	PJDLOG_ASSERT(...)		MPASS(__VA_ARGS__)
#define	PJDLOG_RASSERT(expr, ...)	KASSERT(expr, (__VA_ARGS__))
#define	PJDLOG_ABORT(...)		panic(__VA_ARGS__)
#else
#include <sys/reason.h>
#define	PJDLOG_ABORT(msg, ...)		do {				\
	char *reason_string; asprintf(&reason_string, "%s:%u: " msg, __FILE__, __LINE__, __VA_ARGS__); \
	abort_with_reason(OS_REASON_LIBXPC, 1, reason_string, OS_REASON_FLAG_GENERATE_CRASH_REPORT); \
} while (0)
#define	PJDLOG_ASSERT(expr)		if (!(expr)) PJDLOG_ABORT("Assertion failed: %s", #expr)
#define	PJDLOG_RASSERT(expr, ...)	PJDLOG_ASSERT(expr)
#endif
#endif

#define	NV_FLAG_PRIVATE_MASK	(NV_FLAG_BIG_ENDIAN)
#define	NV_FLAG_PUBLIC_MASK	(NV_FLAG_IGNORE_CASE)
#define	NV_FLAG_ALL_MASK	(NV_FLAG_PRIVATE_MASK | NV_FLAG_PUBLIC_MASK)

#define	NVLIST_MAGIC	0x6e766c	/* "nvl" */
struct nvlist {
	int		 nvl_magic;
	int		 nvl_error;
	int		 nvl_flags;
	int		 nvl_type;
	nvpair_t	*nvl_parent;
	struct nvl_head	 nvl_head;
};

#define	NVLIST_ASSERT(nvl)	do {					\
	PJDLOG_ASSERT((nvl) != NULL);					\
	PJDLOG_ASSERT((nvl)->nvl_magic == NVLIST_MAGIC);		\
} while (0)

#ifdef _KERNEL
MALLOC_DEFINE(M_NVLIST, "nvlist", "kernel nvlist");
#endif

#define	NVPAIR_ASSERT(nvp)	nvpair_assert(nvp)

#define	NVLIST_HEADER_MAGIC	0x6c
#define	NVLIST_HEADER_VERSION	0x00
struct nvlist_header {
	uint8_t		nvlh_magic;
	uint8_t		nvlh_version;
	uint8_t		nvlh_flags;
	uint8_t		nvlh_type;
	uint64_t	nvlh_descriptors;
	uint64_t	nvlh_size;
} __attribute__((packed));

nvlist_t *
nvlist_create(int flags)
{
	nvlist_t *nvl;

	PJDLOG_ASSERT((flags & ~(NV_FLAG_PUBLIC_MASK)) == 0);

	nvl = nv_malloc(sizeof(*nvl));
	nvl->nvl_error = 0;
	nvl->nvl_flags = flags;
	nvl->nvl_parent = NULL;
	nvl->nvl_type = NV_TYPE_NVLIST;
	TAILQ_INIT(&nvl->nvl_head);
	nvl->nvl_magic = NVLIST_MAGIC;

	return (nvl);
}

nvlist_t *
nvlist_create_array(int flags)
{
	nvlist_t *nvl;

	PJDLOG_ASSERT((flags & ~(NV_FLAG_PUBLIC_MASK)) == 0);

	nvl = nv_malloc(sizeof(*nvl));
	nvl->nvl_error = 0;
	nvl->nvl_flags = flags;
	nvl->nvl_parent = NULL;
	nvl->nvl_type = NV_TYPE_NVLIST_ARRAY;
	TAILQ_INIT(&nvl->nvl_head);
	nvl->nvl_magic = NVLIST_MAGIC;

	return (nvl);
}

nvlist_t *
nvlist_create_dictionary(int flags)
{
	nvlist_t *nvl;

	PJDLOG_ASSERT((flags & ~(NV_FLAG_PUBLIC_MASK)) == 0);

	nvl = nv_malloc(sizeof(*nvl));
	nvl->nvl_error = 0;
	nvl->nvl_flags = flags;
	nvl->nvl_parent = NULL;
	nvl->nvl_type = NV_TYPE_NVLIST_DICTIONARY;
	TAILQ_INIT(&nvl->nvl_head);
	nvl->nvl_magic = NVLIST_MAGIC;

	printf("1 nvl = %p\n", nvl);
	return (nvl);
}

void
nvlist_destroy(nvlist_t *nvl)
{
	nvpair_t *nvp;
	int serrno;

	if (nvl == NULL)
		return;

	SAVE_ERRNO(serrno);

	NVLIST_ASSERT(nvl);

	while ((nvp = nvlist_first_nvpair(nvl)) != NULL) {
		nvlist_remove_nvpair(nvl, nvp);
		nvpair_free(nvp);
	}
	nvl->nvl_magic = 0;
	nv_free(nvl);

	RESTORE_ERRNO(serrno);
}

void
nvlist_set_error(nvlist_t *nvl, int error)
{

	PJDLOG_ASSERT(error != 0);

	/*
	 * Check for error != 0 so that we don't do the wrong thing if somebody
	 * tries to abuse this API when asserts are disabled.
	 */
	if (nvl != NULL && error != 0 && nvl->nvl_error == 0)
		nvl->nvl_error = error;
}

int
nvlist_error(const nvlist_t *nvl)
{

	if (nvl == NULL)
		return (ENOMEM);

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_error);
}

nvpair_t *
nvlist_get_nvpair_parent(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_parent);
}

const nvlist_t *
nvlist_get_parent(const nvlist_t *nvl, void **cookiep)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);

	nvp = nvl->nvl_parent;
	if (cookiep != NULL)
		*cookiep = nvp;
	if (nvp == NULL)
		return (NULL);

	return (nvpair_nvlist(nvp));
}

void
nvlist_set_parent(nvlist_t *nvl, nvpair_t *parent)
{

	NVLIST_ASSERT(nvl);

	nvl->nvl_parent = parent;
}

bool
nvlist_empty(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	return (nvlist_first_nvpair(nvl) == NULL);
}

int
nvlist_type(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	return (nvl->nvl_type);
}

static void
nvlist_report_missing(int type, const char *name)
{

	PJDLOG_ABORT("Element '%s' of type %s doesn't exist.",
	    name, nvpair_type_string(type));
}

static nvpair_t *
nvlist_find(const nvlist_t *nvl, int type, const char *name)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (type != NV_TYPE_NONE && nvpair_type(nvp) != type)
			continue;
		if ((nvl->nvl_flags & NV_FLAG_IGNORE_CASE) != 0) {
			if (strcasecmp(nvpair_name(nvp), name) != 0)
				continue;
		} else {
			if (strcmp(nvpair_name(nvp), name) != 0)
				continue;
		}
		break;
	}

	if (nvp == NULL)
		RESTORE_ERRNO(ENOENT);

	return (nvp);
}

bool
nvlist_exists_type(const nvlist_t *nvl, const char *name, int type)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	return (nvlist_find(nvl, type, name) != NULL);
}

#ifndef _KERNEL
bool
nvlist_existsf_type(const nvlist_t *nvl, int type, const char *namefmt, ...)
{
	va_list nameap;
	bool ret;

	va_start(nameap, namefmt);
	ret = nvlist_existsv_type(nvl, type, namefmt, nameap);
	va_end(nameap);

	return (ret);
}

bool
nvlist_existsv_type(const nvlist_t *nvl, int type, const char *namefmt,
    va_list nameap)
{
	char *name;
	bool exists;

	nv_vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		return (false);

	exists = nvlist_exists_type(nvl, name, type);
	nv_free(name);
	return (exists);
}
#endif

void
nvlist_free_type(nvlist_t *nvl, const char *name, int type)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	nvp = nvlist_find(nvl, type, name);
	if (nvp != NULL)
		nvlist_free_nvpair(nvl, nvp);
	else
		nvlist_report_missing(type, name);
}

#ifndef _KERNEL
void
nvlist_freef_type(nvlist_t *nvl, int type, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_freev_type(nvl, type, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_freev_type(nvlist_t *nvl, int type, const char *namefmt, va_list nameap)
{
	char *name;

	nv_vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(type, "<unknown>");
	nvlist_free_type(nvl, name, type);
	nv_free(name);
}
#endif

nvlist_t *
nvlist_clone(const nvlist_t *nvl)
{
	nvlist_t *newnvl;
	nvpair_t *nvp, *newnvp;

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		RESTORE_ERRNO(nvl->nvl_error);
		return (NULL);
	}

	newnvl = nvlist_create(nvl->nvl_flags & NV_FLAG_PUBLIC_MASK);
	newnvl->nvl_type = nvl->nvl_type;
	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		newnvp = nvpair_clone(nvp);
		if (newnvp == NULL)
			break;
		nvlist_move_nvpair(newnvl, newnvp);
	}
	if (nvp != NULL) {
		nvlist_destroy(newnvl);
		return (NULL);
	}
	return (newnvl);
}

#ifndef _KERNEL
static bool
nvlist_dump_error_check(const nvlist_t *nvl, int fd, int level)
{

	if (nvlist_error(nvl) != 0) {
		dprintf(fd, "%*serror: %d\n", level * 4, "",
		    nvlist_error(nvl));
		return (true);
	}

	return (false);
}

/*
 * Dump content of nvlist.
 */
void
nvlist_dump(const nvlist_t *nvl, int fd)
{
	const nvlist_t *tmpnvl;
	nvpair_t *nvp, *tmpnvp;
	void *cookie;
	int level;

	level = 0;
	if (nvlist_dump_error_check(nvl, fd, level))
		return;

	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		dprintf(fd, "%*s%s (%s):", level * 4, "", nvpair_name(nvp),
		    nvpair_type_string(nvpair_type(nvp)));
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			dprintf(fd, " null\n");
			break;
		case NV_TYPE_BOOL:
			dprintf(fd, " %s\n", nvpair_get_bool(nvp) ?
			    "TRUE" : "FALSE");
			break;
		case NV_TYPE_NUMBER:
		case NV_TYPE_PTR:
		case NV_TYPE_UINT64:
		case NV_TYPE_INT64:
		case NV_TYPE_ENDPOINT:
		case NV_TYPE_DATE:
			dprintf(fd, " %ju (%jd) (0x%jx)\n",
			    (uintmax_t)nvpair_get_number(nvp),
			    (intmax_t)nvpair_get_number(nvp),
			    (uintmax_t)nvpair_get_number(nvp));
			break;
		case NV_TYPE_STRING:
			dprintf(fd, " [%s]\n", nvpair_get_string(nvp));
			break;
		case NV_TYPE_NVLIST:
		case NV_TYPE_NVLIST_ARRAY:
		case NV_TYPE_NVLIST_DICTIONARY:
			dprintf(fd, "\n");
			tmpnvl = nvpair_get_nvlist(nvp);
			if (nvlist_dump_error_check(tmpnvl, fd, level + 1))
				break;
			tmpnvp = nvlist_first_nvpair(tmpnvl);
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				level++;
				continue;
			}
			break;
		case NV_TYPE_DESCRIPTOR:
			dprintf(fd, " %d\n", nvpair_get_descriptor(nvp));
			break;
		case NV_TYPE_BINARY:
		    {
			const unsigned char *binary;
			unsigned int ii;
			size_t size;

			binary = nvpair_get_binary(nvp, &size);
			dprintf(fd, " %zu ", size);
			for (ii = 0; ii < size; ii++)
				dprintf(fd, "%02hhx", binary[ii]);
			dprintf(fd, "\n");
			break;
		    }
		case NV_TYPE_UUID:
		    {
		    	uuid_string_t str;
		    	const uuid_t *uuid;

		    	uuid = nvpair_get_uuid(nvp);
				uuid_unparse_upper(*uuid, str);
	    		dprintf(fd, " [%s]\n", str);

		    	free(str);
		    	break;
		    }
		default:
			PJDLOG_ABORT("Unknown type: %d.", nvpair_type(nvp));
		}

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			cookie = NULL;
			nvl = nvlist_get_parent(nvl, &cookie);
			if (nvl == NULL)
				return;
			nvp = cookie;
			level--;
		}
	}
}

void
nvlist_fdump(const nvlist_t *nvl, FILE *fp)
{

	fflush(fp);
	nvlist_dump(nvl, fileno(fp));
}
#endif

/*
 * The function obtains size of the nvlist after nvlist_pack().
 */
size_t
nvlist_size(const nvlist_t *nvl)
{
	const nvlist_t *tmpnvl;
	const nvpair_t *nvp, *tmpnvp;
	void *cookie;
	size_t size;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	size = sizeof(struct nvlist_header);
	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		size += nvpair_header_size();
		size += strlen(nvpair_name(nvp)) + 1;
		if (nvpair_type(nvp) == NV_TYPE_NVLIST_ARRAY ||
			nvpair_type(nvp) == NV_TYPE_NVLIST_DICTIONARY ||
			nvpair_type(nvp) == NV_TYPE_NVLIST) {
			size += sizeof(struct nvlist_header);
			size += nvpair_header_size() + 1;
			tmpnvl = nvpair_get_nvlist(nvp);
			PJDLOG_ASSERT(tmpnvl->nvl_error == 0);
			tmpnvp = nvlist_first_nvpair(tmpnvl);
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				continue;
			}
		} else {
			size += nvpair_size(nvp);
		}

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			cookie = NULL;
			nvl = nvlist_get_parent(nvl, &cookie);
			if (nvl == NULL)
				goto out;
			nvp = cookie;
		}
	}

out:
	return (size);
}

#ifndef _KERNEL
static int *
nvlist_xdescriptors(const nvlist_t *nvl, int *descs, int level)
{
	const nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(level < 3);

	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		switch (nvpair_type(nvp)) {
		case NV_TYPE_DESCRIPTOR:
			*descs = nvpair_get_descriptor(nvp);
			descs++;
			break;
		case NV_TYPE_NVLIST:
		case NV_TYPE_NVLIST_ARRAY:
		case NV_TYPE_NVLIST_DICTIONARY:
			descs = nvlist_xdescriptors(nvpair_get_nvlist(nvp),
			    descs, level + 1);
			break;
		}
	}

	return (descs);
}
#endif

#ifndef _KERNEL
int *
nvlist_descriptors(const nvlist_t *nvl, size_t *nitemsp)
{
	size_t nitems;
	int *fds;

	nitems = nvlist_ndescriptors(nvl);
	fds = nv_malloc(sizeof(fds[0]) * (nitems + 1));
	if (fds == NULL)
		return (NULL);
	if (nitems > 0)
		nvlist_xdescriptors(nvl, fds, 0);
	fds[nitems] = -1;
	if (nitemsp != NULL)
		*nitemsp = nitems;
	return (fds);
}
#endif

static size_t
nvlist_xndescriptors(const nvlist_t *nvl, int level)
{
#ifndef _KERNEL
	const nvpair_t *nvp;
	size_t ndescs;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(level < 3);

	ndescs = 0;
	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		switch (nvpair_type(nvp)) {
		case NV_TYPE_DESCRIPTOR:
			ndescs++;
			break;
		case NV_TYPE_NVLIST:
		case NV_TYPE_NVLIST_ARRAY:
		case NV_TYPE_NVLIST_DICTIONARY:
			ndescs += nvlist_xndescriptors(nvpair_get_nvlist(nvp),
			    level + 1);
			break;
		}
	}

	return (ndescs);
#else
	return (0);
#endif
}

size_t
nvlist_ndescriptors(const nvlist_t *nvl)
{

	return (nvlist_xndescriptors(nvl, 0));
}

static unsigned char *
nvlist_pack_header(const nvlist_t *nvl, unsigned char *ptr, size_t *leftp)
{
	struct nvlist_header nvlhdr;

	NVLIST_ASSERT(nvl);

	nvlhdr.nvlh_magic = NVLIST_HEADER_MAGIC;
	nvlhdr.nvlh_version = NVLIST_HEADER_VERSION;
	nvlhdr.nvlh_flags = nvl->nvl_flags;
	nvlhdr.nvlh_type = (uint8_t)nvl->nvl_type;
#if BYTE_ORDER == BIG_ENDIAN
	nvlhdr.nvlh_flags |= NV_FLAG_BIG_ENDIAN;
#endif
	nvlhdr.nvlh_descriptors = nvlist_ndescriptors(nvl);
	nvlhdr.nvlh_size = *leftp - sizeof(nvlhdr);
	PJDLOG_ASSERT(*leftp >= sizeof(nvlhdr));
	memcpy(ptr, &nvlhdr, sizeof(nvlhdr));
	ptr += sizeof(nvlhdr);
	*leftp -= sizeof(nvlhdr);

	return (ptr);
}

void *
nvlist_xpack(const nvlist_t *nvl, void *ubuf, int64_t *fdidxp, size_t *sizep)
{
	unsigned char *buf, *ptr;
	size_t left, size;
	const nvlist_t *tmpnvl;
	nvpair_t *nvp, *tmpnvp;
	void *cookie;

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		RESTORE_ERRNO(nvl->nvl_error);
		return (NULL);
	}

	size = nvlist_size(nvl);
	if (ubuf) {
		if (sizep == NULL || *sizep != size)
			return (NULL);
		else
			buf = ubuf;
	} else
		buf = nv_malloc(size);
	if (buf == NULL)
		return (NULL);

	ptr = buf;
	left = size;

	ptr = nvlist_pack_header(nvl, ptr, &left);

	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		NVPAIR_ASSERT(nvp);

		nvpair_init_datasize(nvp);
		ptr = nvpair_pack_header(nvp, ptr, &left);
		if (ptr == NULL) {
			nv_free(buf);
			return (NULL);
		}
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			ptr = nvpair_pack_null(nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL:
			ptr = nvpair_pack_bool(nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER:
		case NV_TYPE_PTR:
		case NV_TYPE_UINT64:
		case NV_TYPE_INT64:
		case NV_TYPE_ENDPOINT:
			ptr = nvpair_pack_number(nvp, ptr, &left);
			break;
		case NV_TYPE_STRING:
			ptr = nvpair_pack_string(nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST:
		case NV_TYPE_NVLIST_ARRAY:
		case NV_TYPE_NVLIST_DICTIONARY:
			tmpnvl = nvpair_get_nvlist(nvp);
			ptr = nvlist_pack_header(tmpnvl, ptr, &left);
			if (ptr == NULL)
				goto out;
			tmpnvp = nvlist_first_nvpair(tmpnvl);
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				continue;
			}
			ptr = nvpair_pack_nvlist_up(ptr, &left);
			break;
#ifndef _KERNEL
		case NV_TYPE_DESCRIPTOR:
			ptr = nvpair_pack_descriptor(nvp, ptr, fdidxp, &left);
			break;
#endif
		case NV_TYPE_BINARY:
		case NV_TYPE_UUID:
			ptr = nvpair_pack_binary(nvp, ptr, &left);
			break;
		default:
			PJDLOG_ABORT("Invalid type (%d).", nvpair_type(nvp));
		}
		if (ptr == NULL) {
			nv_free(buf);
			return (NULL);
		}
		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			cookie = NULL;
			nvl = nvlist_get_parent(nvl, &cookie);
			if (nvl == NULL)
				goto out;
			nvp = cookie;
			ptr = nvpair_pack_nvlist_up(ptr, &left);
			if (ptr == NULL)
				goto out;
		}
	}

out:
	if (sizep != NULL)
		*sizep = size;
	return (buf);
}

void *
nvlist_pack(const nvlist_t *nvl, size_t *sizep)
{

	return (nvlist_pack_buffer(nvl, NULL, sizep));
}

void *
nvlist_pack_buffer(const nvlist_t *nvl, void *ubuf, size_t *sizep)
{

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		RESTORE_ERRNO(nvl->nvl_error);
		return (NULL);
	}

	if (nvlist_ndescriptors(nvl) > 0) {
		RESTORE_ERRNO(EOPNOTSUPP);
		return (NULL);
	}

	return (nvlist_xpack(nvl, ubuf, NULL, sizep));
}

static bool
nvlist_check_header(struct nvlist_header *nvlhdrp)
{

	if (nvlhdrp->nvlh_magic != NVLIST_HEADER_MAGIC) {
		RESTORE_ERRNO(EINVAL);
		return (false);
	}
	if ((nvlhdrp->nvlh_flags & ~NV_FLAG_ALL_MASK) != 0) {
		RESTORE_ERRNO(EINVAL);
		return (false);
	}
#if BYTE_ORDER == BIG_ENDIAN
	if ((nvlhdrp->nvlh_flags & NV_FLAG_BIG_ENDIAN) == 0) {
		nvlhdrp->nvlh_size = le64toh(nvlhdrp->nvlh_size);
		nvlhdrp->nvlh_descriptors = le64toh(nvlhdrp->nvlh_descriptors);
	}
#else
	if ((nvlhdrp->nvlh_flags & NV_FLAG_BIG_ENDIAN) != 0) {
		nvlhdrp->nvlh_size = be64toh(nvlhdrp->nvlh_size);
		nvlhdrp->nvlh_descriptors = be64toh(nvlhdrp->nvlh_descriptors);
	}
#endif
	return (true);
}

const unsigned char *
nvlist_unpack_header(nvlist_t *nvl, const unsigned char *ptr, size_t nfds,
    bool *isbep, size_t *leftp)
{
	struct nvlist_header nvlhdr;

	if (*leftp < sizeof(nvlhdr)) PJDLOG_ABORT("nvlist %p buffer too small to contain nvlist_header", nvl);

	memcpy(&nvlhdr, ptr, sizeof(nvlhdr));

	if (!nvlist_check_header(&nvlhdr)) PJDLOG_ABORT("%s failed", "nvlist_check_header");

	if (nvlhdr.nvlh_size != *leftp - sizeof(nvlhdr)) PJDLOG_ABORT("nvlhdr.nvlh_size (%llu) different than message size (%lu)", nvlhdr.nvlh_size, *leftp - sizeof(nvlhdr));

	/*
	 * nvlh_descriptors might be smaller than nfds in embedded nvlists.
	 */
	if (nvlhdr.nvlh_descriptors > nfds) PJDLOG_ABORT("nvlhdr.nvlh_descriptors (%llu) <= nfds (%zu)", nvlhdr.nvlh_descriptors, nfds);

	if ((nvlhdr.nvlh_flags & ~NV_FLAG_ALL_MASK) != 0) PJDLOG_ABORT("Invalid nvlh_flags %d", nvlhdr.nvlh_flags);

	nvl->nvl_flags = (nvlhdr.nvlh_flags & NV_FLAG_PUBLIC_MASK);

	ptr += sizeof(nvlhdr);
	if (isbep != NULL)
		*isbep = (((int)nvlhdr.nvlh_flags & NV_FLAG_BIG_ENDIAN) != 0);
	*leftp -= sizeof(nvlhdr);

	nvl->nvl_type = nvlhdr.nvlh_type;
	return (ptr);
}

nvlist_t *
nvlist_xunpack(const void *buf, size_t size, const int *fds, size_t nfds)
{
	const unsigned char *ptr;
	nvlist_t *nvl, *retnvl, *tmpnvl;
	nvpair_t *nvp;
	size_t left;
	bool isbe;

	left = size;
	ptr = buf;

	tmpnvl = NULL;
	nvl = retnvl = nvlist_create(0);
	if (nvl == NULL) PJDLOG_ABORT("nvlist_create returned %s", "NULL");

	ptr = nvlist_unpack_header(nvl, ptr, nfds, &isbe, &left);
	if (ptr == NULL) PJDLOG_ABORT("Could not unpack nvlist header: (isbe=%s, left=%zu)", isbe ? "true" : "false", left);

	while (left > 0) {
		ptr = nvpair_unpack(isbe, ptr, &left, &nvp);
		if (ptr == NULL) PJDLOG_ABORT("Could not unpack nvlist (left=%zu)", left);
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			ptr = nvpair_unpack_null(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL:
			ptr = nvpair_unpack_bool(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER:
		case NV_TYPE_PTR:
		case NV_TYPE_UINT64:
		case NV_TYPE_INT64:
		case NV_TYPE_ENDPOINT:
			ptr = nvpair_unpack_number(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_STRING:
			ptr = nvpair_unpack_string(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST:
		case NV_TYPE_NVLIST_ARRAY:
		case NV_TYPE_NVLIST_DICTIONARY:
			ptr = nvpair_unpack_nvlist(isbe, nvp, ptr, &left, nfds, &tmpnvl);
			nvlist_set_parent(tmpnvl, nvp);
			break;
#ifndef _KERNEL
		case NV_TYPE_DESCRIPTOR:
			ptr = nvpair_unpack_descriptor(isbe, nvp, ptr, &left, fds, nfds);
			break;
#endif
		case NV_TYPE_BINARY:
		case NV_TYPE_UUID:
			ptr = nvpair_unpack_binary(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST_UP:
			if (nvl->nvl_parent == NULL) PJDLOG_ABORT("nvlist_t %p has no parent", nvl);
			nvl = nvpair_nvlist(nvl->nvl_parent);
			continue;
		default:
			PJDLOG_ABORT("Invalid type (%d).", nvpair_type(nvp));
		}
		if (ptr == NULL) PJDLOG_ABORT("ptr == NULL (nvp=%p)", nvp);
		nvlist_move_nvpair(nvl, nvp);
		if (tmpnvl != NULL) {
			nvl = tmpnvl;
			tmpnvl = NULL;
		}
	}

	return (retnvl);
}

nvlist_t *
nvlist_unpack(const void *buf, size_t size)
{

	return (nvlist_xunpack(buf, size, NULL, 0));
}

nvpair_t *
nvlist_first_nvpair(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (TAILQ_FIRST(&nvl->nvl_head));
}

nvpair_t *
nvlist_next_nvpair(const nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *retnvp;

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	retnvp = nvpair_next(nvp);
	PJDLOG_ASSERT(retnvp == NULL || nvpair_nvlist(retnvp) == nvl);

	return (retnvp);

}

nvpair_t *
nvlist_prev_nvpair(const nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *retnvp;

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	retnvp = nvpair_prev(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(retnvp) == nvl);

	return (retnvp);
}

const char *
nvlist_next(const nvlist_t *nvl, int *typep, void **cookiep)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(cookiep != NULL);

	if (*cookiep == NULL)
		nvp = nvlist_first_nvpair(nvl);
	else
		nvp = nvlist_next_nvpair(nvl, *cookiep);
	if (nvp == NULL)
		return (NULL);
	if (typep != NULL)
		*typep = nvpair_type(nvp);
	*cookiep = nvp;
	return (nvpair_name(nvp));
}

bool
nvlist_exists(const nvlist_t *nvl, const char *name)
{

	return (nvlist_find(nvl, NV_TYPE_NONE, name) != NULL);
}

#define	NVLIST_EXISTS(type, TYPE)					\
bool									\
nvlist_exists_##type(const nvlist_t *nvl, const char *name)		\
{									\
									\
	return (nvlist_find(nvl, NV_TYPE_##TYPE, name) != NULL);	\
}

NVLIST_EXISTS(null, NULL)
NVLIST_EXISTS(bool, BOOL)
NVLIST_EXISTS(number, NUMBER)
NVLIST_EXISTS(ptr, PTR)
NVLIST_EXISTS(uint64, UINT64)
NVLIST_EXISTS(int64, INT64)
NVLIST_EXISTS(endpoint, ENDPOINT)
NVLIST_EXISTS(date, DATE)
NVLIST_EXISTS(string, STRING)
NVLIST_EXISTS(nvlist, NVLIST)
#ifndef _KERNEL
NVLIST_EXISTS(descriptor, DESCRIPTOR)
#endif
NVLIST_EXISTS(binary, BINARY)
NVLIST_EXISTS(uuid, UUID)

#undef	NVLIST_EXISTS

#ifndef _KERNEL
bool
nvlist_existsf(const nvlist_t *nvl, const char *namefmt, ...)
{
	va_list nameap;
	bool ret;

	va_start(nameap, namefmt);
	ret = nvlist_existsv(nvl, namefmt, nameap);
	va_end(nameap);
	return (ret);
}

#define	NVLIST_EXISTSF(type)						\
bool									\
nvlist_existsf_##type(const nvlist_t *nvl, const char *namefmt, ...)	\
{									\
	va_list nameap;							\
	bool ret;							\
									\
	va_start(nameap, namefmt);					\
	ret = nvlist_existsv_##type(nvl, namefmt, nameap);		\
	va_end(nameap);							\
	return (ret);							\
}

NVLIST_EXISTSF(null)
NVLIST_EXISTSF(bool)
NVLIST_EXISTSF(number)
NVLIST_EXISTSF(ptr)
NVLIST_EXISTSF(uint64)
NVLIST_EXISTSF(int64)
NVLIST_EXISTSF(endpoint)
NVLIST_EXISTSF(date)
NVLIST_EXISTSF(string)
NVLIST_EXISTSF(nvlist)
#ifndef _KERNEL
NVLIST_EXISTSF(descriptor)
#endif
NVLIST_EXISTSF(binary)
NVLIST_EXISTSF(uuid)

#undef	NVLIST_EXISTSF

bool
nvlist_existsv(const nvlist_t *nvl, const char *namefmt, va_list nameap)
{
	char *name;
	bool exists;

	nv_vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		return (false);

	exists = nvlist_exists(nvl, name);
	nv_free(name);
	return (exists);
}

#define	NVLIST_EXISTSV(type)						\
bool									\
nvlist_existsv_##type(const nvlist_t *nvl, const char *namefmt,		\
    va_list nameap)							\
{									\
	char *name;							\
	bool exists;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		return (false);						\
	exists = nvlist_exists_##type(nvl, name);			\
	free(name);							\
	return (exists);						\
}

NVLIST_EXISTSV(null)
NVLIST_EXISTSV(bool)
NVLIST_EXISTSV(number)
NVLIST_EXISTSV(ptr)
NVLIST_EXISTSV(uint64)
NVLIST_EXISTSV(int64)
NVLIST_EXISTSV(endpoint)
NVLIST_EXISTSV(date)
NVLIST_EXISTSV(string)
NVLIST_EXISTSV(nvlist)
NVLIST_EXISTSV(descriptor)
NVLIST_EXISTSV(binary)
NVLIST_EXISTSV(uuid)

#undef	NVLIST_EXISTSV
#endif

void
nvlist_add_nvpair(nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *newnvp;

	NVPAIR_ASSERT(nvp);

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}
	if (nvlist_exists(nvl, nvpair_name(nvp))) {
		nvl->nvl_error = EEXIST;
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	newnvp = nvpair_clone(nvp);
	if (newnvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvpair_insert(&nvl->nvl_head, newnvp, nvl);
}

void
nvlist_add_null(nvlist_t *nvl, const char *name)
{

	nvlist_addf_null(nvl, "%s", name);
}

void
nvlist_add_bool(nvlist_t *nvl, const char *name, bool value)
{

	nvlist_addf_bool(nvl, value, "%s", name);
}

void
nvlist_add_number(nvlist_t *nvl, const char *name, uint64_t value)
{

	nvlist_addf_number(nvl, value, "%s", name);
}

void
nvlist_add_ptr(nvlist_t *nvl, const char *name, uintptr_t value)
{

	nvlist_addf_ptr(nvl, value, "%s", name);
}

void
nvlist_add_uint64(nvlist_t *nvl, const char *name, uint64_t value)
{

	nvlist_addf_uint64(nvl, value, "%s", name);
}

void
nvlist_add_int64(nvlist_t *nvl, const char *name, int64_t value)
{

	nvlist_addf_int64(nvl, value, "%s", name);
}

void
nvlist_add_endpoint(nvlist_t *nvl, const char *name, int value)
{

	nvlist_addf_endpoint(nvl, value, "%s", name);
}

void
nvlist_add_date(nvlist_t *nvl, const char *name, uint64_t value)
{

	nvlist_addf_date(nvl, value, "%s", name);
}

void
nvlist_add_string(nvlist_t *nvl, const char *name, const char *value)
{

	nvlist_addf_string(nvl, value, "%s", name);
}

void
nvlist_add_stringv(nvlist_t *nvl, const char *name, const char *valuefmt,
    va_list valueap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_create_stringv(name, valuefmt, valueap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_add_stringf(nvlist_t *nvl, const char *name, const char *valuefmt, ...)
{
	va_list valueap;

	va_start(valueap, valuefmt);
	nvlist_add_stringv(nvl, name, valuefmt, valueap);
	va_end(valueap);
}

void
nvlist_add_nvlist(nvlist_t *nvl, const char *name, const nvlist_t *value)
{

	nvlist_addf_nvlist(nvl, value, "%s", name);
}

void
nvlist_add_nvlist_array(nvlist_t *nvl, const char *name, const nvlist_t *value)
{

	nvlist_addf_nvlist_array(nvl, value, "%s", name);
}

void
nvlist_add_nvlist_dictionary(nvlist_t *nvl, const char *name, const nvlist_t *value)
{

	nvlist_addf_nvlist_dictionary(nvl, value, "%s", name);
}

#ifndef _KERNEL
void
nvlist_add_descriptor(nvlist_t *nvl, const char *name, int value)
{

	nvlist_addf_descriptor(nvl, value, "%s", name);
}
#endif

void
nvlist_add_binary(nvlist_t *nvl, const char *name, const void *value,
    size_t size)
{

	nvlist_addf_binary(nvl, value, size, "%s", name);
}

void
nvlist_add_uuid(nvlist_t *nvl, const char *name, const uuid_t *value)
{

	nvlist_addf_uuid(nvl, value, "%s", name);
}

void
nvlist_addf_null(nvlist_t *nvl, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_null(nvl, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_bool(nvlist_t *nvl, bool value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_bool(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_number(nvlist_t *nvl, uint64_t value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_number(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_ptr(nvlist_t *nvl, uintptr_t value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_ptr(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_uint64(nvlist_t *nvl, uint64_t value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_uint64(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_int64(nvlist_t *nvl, int64_t value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_int64(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_endpoint(nvlist_t *nvl, int value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_endpoint(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_date(nvlist_t *nvl, uint64_t value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_date(nvl, value, namefmt, nameap);
	va_end(nameap);
}


void
nvlist_addf_string(nvlist_t *nvl, const char *value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_string(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_nvlist(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_nvlist(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_nvlist_array(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_nvlist_array(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_nvlist_dictionary(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_nvlist_dictionary(nvl, value, namefmt, nameap);
	va_end(nameap);
}

#ifndef _KERNEL
void
nvlist_addf_descriptor(nvlist_t *nvl, int value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_descriptor(nvl, value, namefmt, nameap);
	va_end(nameap);
}
#endif

void
nvlist_addf_binary(nvlist_t *nvl, const void *value, size_t size,
    const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_binary(nvl, value, size, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_uuid(nvlist_t *nvl, const uuid_t *value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_uuid(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addv_null(nvlist_t *nvl, const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_null(namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_bool(nvlist_t *nvl, bool value, const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_bool(value, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_number(nvlist_t *nvl, uint64_t value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_number_type(value, NV_TYPE_NUMBER, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_ptr(nvlist_t *nvl, uintptr_t value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_number_type(value, NV_TYPE_PTR, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_uint64(nvlist_t *nvl, uint64_t value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_number_type(value, NV_TYPE_UINT64, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_int64(nvlist_t *nvl, int64_t value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_number_type(value, NV_TYPE_INT64, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_endpoint(nvlist_t *nvl, int value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_number_type(value, NV_TYPE_ENDPOINT, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_date(nvlist_t *nvl, uint64_t value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_number_type(value, NV_TYPE_DATE, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_string(nvlist_t *nvl, const char *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_string(value, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_nvlist(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
	va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_nvlist_type(value, NV_TYPE_NVLIST, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_nvlist_array(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_nvlist_type(value, NV_TYPE_NVLIST_ARRAY, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_nvlist_dictionary(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_nvlist_type(value, NV_TYPE_NVLIST_DICTIONARY, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

#ifndef _KERNEL
void
nvlist_addv_descriptor(nvlist_t *nvl, int value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_descriptor(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}
#endif

void
nvlist_addv_binary(nvlist_t *nvl, const void *value, size_t size,
    const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_binary(value, size, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_uuid(nvlist_t *nvl, const uuid_t *value,
    const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_createv_uuid(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_move_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == NULL);

	if (nvlist_error(nvl) != 0) {
		nvpair_free(nvp);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}
	if (nvlist_exists(nvl, nvpair_name(nvp))) {
		nvpair_free(nvp);
		nvl->nvl_error = EEXIST;
		RESTORE_ERRNO(nvl->nvl_error);
		return;
	}

	nvpair_insert(&nvl->nvl_head, nvp, nvl);
}

#define	NVLIST_MOVE(vtype, type)					\
void									\
nvlist_move_##type(nvlist_t *nvl, const char *name, vtype value)	\
{									\
									\
	nvlist_movef_##type(nvl, value, "%s", name);			\
}

NVLIST_MOVE(char *, string)
NVLIST_MOVE(nvlist_t *, nvlist)
NVLIST_MOVE(nvlist_t *, nvlist_array)
NVLIST_MOVE(nvlist_t *, nvlist_dictionary)
#ifndef _KERNEL
NVLIST_MOVE(int, descriptor)
#endif

#undef	NVLIST_MOVE

void
nvlist_move_binary(nvlist_t *nvl, const char *name, void *value, size_t size)
{

	nvlist_movef_binary(nvl, value, size, "%s", name);
}

void
nvlist_move_uuid(nvlist_t *nvl, const char *name, uuid_t *value)
{

	nvlist_movef_binary(nvl, value, sizeof(uuid_t), "%s", name);
}

#define	NVLIST_MOVEF(vtype, type)					\
void									\
nvlist_movef_##type(nvlist_t *nvl, vtype value, const char *namefmt,	\
    ...)								\
{									\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nvlist_movev_##type(nvl, value, namefmt, nameap);		\
	va_end(nameap);							\
}

NVLIST_MOVEF(char *, string)
NVLIST_MOVEF(nvlist_t *, nvlist)
NVLIST_MOVEF(nvlist_t *, nvlist_array)
NVLIST_MOVEF(nvlist_t *, nvlist_dictionary)
#ifndef _KERNEL
NVLIST_MOVEF(int, descriptor)
#endif

#undef	NVLIST_MOVEF

void
nvlist_movef_binary(nvlist_t *nvl, void *value, size_t size,
    const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_movev_binary(nvl, value, size, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_movef_uuid(nvlist_t *nvl, uuid_t *value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_movev_uuid(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_movev_string(nvlist_t *nvl, char *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_movev_string(value, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_movev_nvlist(nvlist_t *nvl, nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL && nvlist_get_nvpair_parent(value) != NULL)
			nvlist_destroy(value);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_movev_nvlist_type(value, NV_TYPE_NVLIST, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_movev_nvlist_array(nvlist_t *nvl, nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL && nvlist_get_nvpair_parent(value) != NULL)
			nvlist_destroy(value);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_movev_nvlist_type(value, NV_TYPE_NVLIST_ARRAY, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_movev_nvlist_dictionary(nvlist_t *nvl, nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL && nvlist_get_nvpair_parent(value) != NULL)
			nvlist_destroy(value);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_movev_nvlist_type(value, NV_TYPE_NVLIST_DICTIONARY, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

#ifndef _KERNEL
void
nvlist_movev_descriptor(nvlist_t *nvl, int value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		close(value);
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_movev_descriptor(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}
#endif

void
nvlist_movev_binary(nvlist_t *nvl, void *value, size_t size,
    const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_movev_binary(value, size, namefmt, nameap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		RESTORE_ERRNO(nvl->nvl_error);
	} else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_movev_uuid(nvlist_t *nvl, uuid_t *value,
const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		RESTORE_ERRNO(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_movev_uuid(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

const nvpair_t *
nvlist_get_nvpair(const nvlist_t *nvl, const char *name)
{

	return (nvlist_find(nvl, NV_TYPE_NONE, name));
}

#define	NVLIST_GET(ftype, type, acc_type, TYPE)					\
ftype									\
nvlist_get_##type(const nvlist_t *nvl, const char *name)		\
{									\
	const nvpair_t *nvp;						\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE, name);			\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, name);		\
	return ((ftype)nvpair_get_##acc_type(nvp));				\
}

NVLIST_GET(bool, bool, bool, BOOL)
NVLIST_GET(uint64_t, number, number, NUMBER)
NVLIST_GET(uintptr_t, ptr, number, PTR)
NVLIST_GET(uint64_t, uint64, number, UINT64)
NVLIST_GET(int64_t, int64, number, INT64)
NVLIST_GET(int, endpoint, number, ENDPOINT)
NVLIST_GET(uint64_t, date, number, DATE)
NVLIST_GET(const char *, string, string, STRING)
NVLIST_GET(const nvlist_t *, nvlist, nvlist, NVLIST)
NVLIST_GET(const nvlist_t *, nvlist_array, nvlist, NVLIST_ARRAY)
NVLIST_GET(const nvlist_t *, nvlist_dictionary, nvlist, NVLIST_DICTIONARY)
#ifndef _KERNEL
NVLIST_GET(int, descriptor, descriptor, DESCRIPTOR)
#endif
NVLIST_GET(const uuid_t *, uuid, uuid, UUID)

#undef	NVLIST_GET

const void *
nvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep)
{
	nvpair_t *nvp;

	nvp = nvlist_find(nvl, NV_TYPE_BINARY, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, name);

	return (nvpair_get_binary(nvp, sizep));
}

#define	NVLIST_GETF(ftype, type)					\
ftype									\
nvlist_getf_##type(const nvlist_t *nvl, const char *namefmt, ...)	\
{									\
	va_list nameap;							\
	ftype value;							\
									\
	va_start(nameap, namefmt);					\
	value = nvlist_getv_##type(nvl, namefmt, nameap);		\
	va_end(nameap);							\
									\
	return (value);							\
}

#ifndef _KERNEL
NVLIST_GETF(bool, bool)
NVLIST_GETF(uint64_t, number)
NVLIST_GETF(uintptr_t, ptr)
NVLIST_GETF(uint64_t, uint64)
NVLIST_GETF(int64_t, int64)
NVLIST_GETF(int, endpoint)
NVLIST_GETF(uint64_t, date)
NVLIST_GETF(const char *, string)
NVLIST_GETF(const nvlist_t *, nvlist)
NVLIST_GETF(const nvlist_t *, nvlist_array)
NVLIST_GETF(const nvlist_t *, nvlist_dictionary)
NVLIST_GETF(int, descriptor)
NVLIST_GETF(const uuid_t *, uuid)

#undef	NVLIST_GETF

const void *
nvlist_getf_binary(const nvlist_t *nvl, size_t *sizep, const char *namefmt, ...)
{
	va_list nameap;
	const void *value;

	va_start(nameap, namefmt);
	value = nvlist_getv_binary(nvl, sizep, namefmt, nameap);
	va_end(nameap);

	return (value);
}

#define	NVLIST_GETV(ftype, type, TYPE)					\
ftype									\
nvlist_getv_##type(const nvlist_t *nvl, const char *namefmt,		\
    va_list nameap)							\
{									\
	char *name;							\
	ftype value;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, "<unknown>");	\
	value = nvlist_get_##type(nvl, name);				\
	free(name);							\
									\
	return (value);							\
}

NVLIST_GETV(bool, bool, BOOL)
NVLIST_GETV(uint64_t, number, NUMBER)
NVLIST_GETV(uintptr_t, ptr, PTR)
NVLIST_GETV(uint64_t, uint64, UINT64)
NVLIST_GETV(int64_t, int64, INT64)
NVLIST_GETV(int, endpoint, ENDPOINT)
NVLIST_GETV(uint64_t, date, DATE)
NVLIST_GETV(const char *, string, STRING)
NVLIST_GETV(const nvlist_t *, nvlist, NVLIST)
NVLIST_GETV(const nvlist_t *, nvlist_array, NVLIST_ARRAY)
NVLIST_GETV(const nvlist_t *, nvlist_dictionary, NVLIST_DICTIONARY)
NVLIST_GETV(int, descriptor, DESCRIPTOR)
NVLIST_GETV(const uuid_t *, uuid, UUID)

#undef	NVLIST_GETV

const void *
nvlist_getv_binary(const nvlist_t *nvl, size_t *sizep, const char *namefmt,
    va_list nameap)
{
	char *name;
	const void *binary;

	nv_vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, "<unknown>");

	binary = nvlist_get_binary(nvl, name, sizep);
	nv_free(name);
	return (binary);
}
#endif

#define	NVLIST_TAKE(ftype, type, acc_type, TYPE)			\
ftype									\
nvlist_take_##type(nvlist_t *nvl, const char *name)			\
{									\
	nvpair_t *nvp;							\
	ftype value;							\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE, name);			\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, name);		\
	value = (ftype)(intptr_t)nvpair_get_##acc_type(nvp);		\
	nvlist_remove_nvpair(nvl, nvp);					\
	nvpair_free_structure(nvp);					\
	return (value);							\
}

NVLIST_TAKE(bool, bool, bool, BOOL)
NVLIST_TAKE(uint64_t, number, number, NUMBER)
NVLIST_TAKE(uintptr_t, ptr, number, PTR)
NVLIST_TAKE(uint64_t, uint64, number, UINT64)
NVLIST_TAKE(int64_t, int64, number, INT64)
NVLIST_TAKE(int, endpoint, number, ENDPOINT)
NVLIST_TAKE(uint64_t, date, number, DATE)
NVLIST_TAKE(char *, string, string, STRING)
NVLIST_TAKE(nvlist_t *, nvlist, nvlist, NVLIST)
NVLIST_TAKE(nvlist_t *, nvlist_array, nvlist, NVLIST_ARRAY)
NVLIST_TAKE(nvlist_t *, nvlist_dictionary, nvlist, NVLIST_DICTIONARY)
#ifndef _KERNEL
NVLIST_TAKE(int, descriptor, descriptor, DESCRIPTOR)
#endif
NVLIST_TAKE(uuid_t *, uuid, uuid, UUID)

#undef	NVLIST_TAKE

void *
nvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep)
{
	nvpair_t *nvp;
	void *value;

	nvp = nvlist_find(nvl, NV_TYPE_BINARY, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, name);

	value = (void *)(intptr_t)nvpair_get_binary(nvp, sizep);
	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free_structure(nvp);
	return (value);
}

#define	NVLIST_TAKEF(ftype, type)					\
ftype									\
nvlist_takef_##type(nvlist_t *nvl, const char *namefmt, ...)		\
{									\
	va_list nameap;							\
	ftype value;							\
									\
	va_start(nameap, namefmt);					\
	value = nvlist_takev_##type(nvl, namefmt, nameap);		\
	va_end(nameap);							\
									\
	return (value);							\
}

#ifndef _KERNEL
NVLIST_TAKEF(bool, bool)
NVLIST_TAKEF(uint64_t, number)
NVLIST_TAKEF(uintptr_t, ptr)
NVLIST_TAKEF(uint64_t, uint64)
NVLIST_TAKEF(int64_t, int64)
NVLIST_TAKEF(int, endpoint)
NVLIST_TAKEF(uint64_t, date)
NVLIST_TAKEF(char *, string)
NVLIST_TAKEF(nvlist_t *, nvlist)
NVLIST_TAKEF(nvlist_t *, nvlist_array)
NVLIST_TAKEF(nvlist_t *, nvlist_dictionary)
NVLIST_TAKEF(int, descriptor)
NVLIST_TAKEF(uuid_t *, uuid)

#undef	NVLIST_TAKEF

void *
nvlist_takef_binary(nvlist_t *nvl, size_t *sizep, const char *namefmt, ...)
{
	va_list nameap;
	void *value;

	va_start(nameap, namefmt);
	value = nvlist_takev_binary(nvl, sizep, namefmt, nameap);
	va_end(nameap);

	return (value);
}

#define	NVLIST_TAKEV(ftype, type, acc_type, TYPE)					\
ftype									\
nvlist_takev_##type(nvlist_t *nvl, const char *namefmt, va_list nameap)	\
{									\
	char *name;							\
	ftype value;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, "<unknown>");	\
	value = nvlist_take_##type(nvl, name);				\
	free(name);							\
	return (value);							\
}

NVLIST_TAKEV(bool, bool, bool, BOOL)
NVLIST_TAKEV(uint64_t, number, number, NUMBER)
NVLIST_TAKEV(uintptr_t, ptr, number, PTR)
NVLIST_TAKEV(uint64_t, uint64, number, UINT64)
NVLIST_TAKEV(int64_t, int64, number, INT64)
NVLIST_TAKEV(int, endpoint, number, ENDPOINT)
NVLIST_TAKEV(uint64_t, date, number, DATE)
NVLIST_TAKEV(char *, string, string, STRING)
NVLIST_TAKEV(nvlist_t *, nvlist, nvlist, NVLIST)
NVLIST_TAKEV(nvlist_t *, nvlist_array, nvlist, NVLIST_ARRAY)
NVLIST_TAKEV(nvlist_t *, nvlist_dictionary, nvlist, NVLIST_DICTIONARY)
NVLIST_TAKEV(int, descriptor, descriptor, DESCRIPTOR)
NVLIST_TAKEV(uuid_t *, uuid, uuid, UUID)

#undef	NVLIST_TAKEV

void *
nvlist_takev_binary(nvlist_t *nvl, size_t *sizep, const char *namefmt,
    va_list nameap)
{
	char *name;
	void *binary;

	nv_vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, "<unknown>");

	binary = nvlist_take_binary(nvl, name, sizep);
	nv_free(name);
	return (binary);
}
#endif

void
nvlist_remove_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	nvpair_remove(&nvl->nvl_head, nvp, nvl);
}

void
nvlist_free(nvlist_t *nvl, const char *name)
{

	nvlist_free_type(nvl, name, NV_TYPE_NONE);
}

#define	NVLIST_FREE(type, TYPE)						\
void									\
nvlist_free_##type(nvlist_t *nvl, const char *name)			\
{									\
									\
	nvlist_free_type(nvl, name, NV_TYPE_##TYPE);			\
}

NVLIST_FREE(null, NULL)
NVLIST_FREE(bool, BOOL)
NVLIST_FREE(number, NUMBER)
NVLIST_FREE(ptr, PTR)
NVLIST_FREE(uint64, UINT64)
NVLIST_FREE(int64, INT64)
NVLIST_FREE(endpoint, ENDPOINT)
NVLIST_FREE(date, DATE)
NVLIST_FREE(string, STRING)
NVLIST_FREE(nvlist, NVLIST)
NVLIST_FREE(nvlist_array, NVLIST_ARRAY)
NVLIST_FREE(nvlist_dictionary, NVLIST_DICTIONARY)
#ifndef _KERNEL
NVLIST_FREE(descriptor, DESCRIPTOR)
#endif
NVLIST_FREE(binary, BINARY)
NVLIST_FREE(uuid, UUID)

#undef	NVLIST_FREE

#ifndef _KERNEL
void
nvlist_freef(nvlist_t *nvl, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_freev(nvl, namefmt, nameap);
	va_end(nameap);
}

#define	NVLIST_FREEF(type)						\
void									\
nvlist_freef_##type(nvlist_t *nvl, const char *namefmt, ...)		\
{									\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nvlist_freev_##type(nvl, namefmt, nameap);			\
	va_end(nameap);							\
}

NVLIST_FREEF(null)
NVLIST_FREEF(bool)
NVLIST_FREEF(number)
NVLIST_FREEF(ptr)
NVLIST_FREEF(uint64)
NVLIST_FREEF(int64)
NVLIST_FREEF(endpoint)
NVLIST_FREEF(date)
NVLIST_FREEF(string)
NVLIST_FREEF(nvlist)
NVLIST_FREEF(nvlist_array)
NVLIST_FREEF(nvlist_dictionary)
NVLIST_FREEF(descriptor)
NVLIST_FREEF(binary)
NVLIST_FREEF(uuid)

#undef	NVLIST_FREEF

void
nvlist_freev(nvlist_t *nvl, const char *namefmt, va_list nameap)
{

	nvlist_freev_type(nvl, NV_TYPE_NONE, namefmt, nameap);
}

#define	NVLIST_FREEV(type, TYPE)					\
void									\
nvlist_freev_##type(nvlist_t *nvl, const char *namefmt, va_list nameap)	\
{									\
	char *name;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, "<unknown>");	\
	nvlist_free_##type(nvl, name);					\
	free(name);							\
}

NVLIST_FREEV(null, NULL)
NVLIST_FREEV(bool, BOOL)
NVLIST_FREEV(number, NUMBER)
NVLIST_FREEV(ptr, PTR)
NVLIST_FREEV(uint64, UINT64)
NVLIST_FREEV(int64, INT64)
NVLIST_FREEV(endpoint, ENDPOINT)
NVLIST_FREEV(date, DATE)
NVLIST_FREEV(string, STRING)
NVLIST_FREEV(nvlist, NVLIST)
NVLIST_FREEV(nvlist_array, NVLIST_ARRAY)
NVLIST_FREEV(nvlist_dictionary, NVLIST_DICTIONARY)
NVLIST_FREEV(descriptor, DESCRIPTOR)
NVLIST_FREEV(binary, BINARY)
NVLIST_FREEV(uuid, UUID)
#undef	NVLIST_FREEV
#endif

void
nvlist_free_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free(nvp);
}
