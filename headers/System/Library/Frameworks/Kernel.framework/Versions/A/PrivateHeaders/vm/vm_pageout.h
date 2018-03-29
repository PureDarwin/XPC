/*
 * Copyright (c) 2000-2009 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * @OSF_COPYRIGHT@
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988,1987 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 */
/*
 *	File:	vm/vm_pageout.h
 *	Author:	Avadis Tevanian, Jr.
 *	Date:	1986
 *
 *	Declarations for the pageout daemon interface.
 */

#ifndef	_VM_VM_PAGEOUT_H_
#define _VM_VM_PAGEOUT_H_


#include <mach/mach_types.h>
#include <mach/boolean.h>
#include <mach/machine/vm_types.h>
#include <mach/memory_object_types.h>

#include <kern/kern_types.h>
#include <kern/lock.h>

#include <libkern/OSAtomic.h>


#include <vm/vm_options.h>


#include <sys/kdebug.h>

#if CONFIG_FREEZE
extern boolean_t vm_freeze_enabled;
#define VM_DYNAMIC_PAGING_ENABLED(port) ((vm_freeze_enabled == FALSE) && IP_VALID(port))
#else
#define VM_DYNAMIC_PAGING_ENABLED(port) IP_VALID(port)
#endif


extern int	vm_debug_events;

#define VMF_CHECK_ZFDELAY	0x100
#define VMF_COWDELAY		0x101
#define VMF_ZFDELAY		0x102

#define VM_PAGEOUT_SCAN		0x104
#define VM_PAGEOUT_BALANCE	0x105
#define VM_PAGEOUT_FREELIST	0x106
#define VM_PAGEOUT_PURGEONE	0x107
#define VM_PAGEOUT_CACHE_EVICT	0x108
#define VM_PAGEOUT_THREAD_BLOCK	0x109

#define VM_UPL_PAGE_WAIT	0x120
#define VM_IOPL_PAGE_WAIT	0x121

#define VM_DEBUG_EVENT(name, event, control, arg1, arg2, arg3, arg4)	\
	MACRO_BEGIN						\
	if (vm_debug_events) {					\
		KERNEL_DEBUG_CONSTANT((MACHDBG_CODE(DBG_MACH_VM, event)) | control, arg1, arg2, arg3, arg4, 0); \
	}							\
	MACRO_END



extern kern_return_t vm_map_create_upl(
	vm_map_t		map,
	vm_map_address_t	offset,
	upl_size_t		*upl_size,
	upl_t			*upl,
	upl_page_info_array_t	page_list,
	unsigned int		*count,
	int			*flags);

extern ppnum_t upl_get_highest_page(
	upl_t			upl);

extern upl_size_t upl_get_size(
	upl_t			upl);


typedef struct vm_page	*vm_page_t;

extern void                vm_page_free_list(
                            vm_page_t	mem,
                            boolean_t	prepare_object);

extern kern_return_t      vm_page_alloc_list(
                            int         page_count,
                            int			flags,
                            vm_page_t * list);

extern void               vm_page_set_offset(vm_page_t page, vm_object_offset_t offset);
extern vm_object_offset_t vm_page_get_offset(vm_page_t page);
extern ppnum_t            vm_page_get_phys_page(vm_page_t page);
extern vm_page_t          vm_page_get_next(vm_page_t page);


#if UPL_DEBUG
extern kern_return_t  upl_ubc_alias_set(
	upl_t upl,
	uintptr_t alias1,
	uintptr_t alias2);
extern int  upl_ubc_alias_get(
	upl_t upl,
	uintptr_t * al,
	uintptr_t * al2);
#endif /* UPL_DEBUG */

extern void vm_countdirtypages(void);

extern void vm_backing_store_disable(
			boolean_t	suspend);

extern kern_return_t upl_transpose(
	upl_t	upl1,
	upl_t	upl2);

extern kern_return_t mach_vm_pressure_monitor(
	boolean_t	wait_for_pressure,
	unsigned int	nsecs_monitored,
	unsigned int	*pages_reclaimed_p,
	unsigned int	*pages_wanted_p);

extern kern_return_t
vm_set_buffer_cleanup_callout(
	boolean_t	(*func)(int));

struct vm_page_stats_reusable {
	SInt32		reusable_count;
	uint64_t	reusable;
	uint64_t	reused;
	uint64_t	reused_wire;
	uint64_t	reused_remove;
	uint64_t	all_reusable_calls;
	uint64_t	partial_reusable_calls;
	uint64_t	all_reuse_calls;
	uint64_t	partial_reuse_calls;
	uint64_t	reusable_pages_success;
	uint64_t	reusable_pages_failure;
	uint64_t	reusable_pages_shared;
	uint64_t	reuse_pages_success;
	uint64_t	reuse_pages_failure;
	uint64_t	can_reuse_success;
	uint64_t	can_reuse_failure;
};
extern struct vm_page_stats_reusable vm_page_stats_reusable;
	
extern int hibernate_flush_memory(void);


#endif	/* _VM_VM_PAGEOUT_H_ */
