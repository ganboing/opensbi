/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Bo Gan <ganboing@gmail.com>
 *
 */

#include <sbi/sbi_scratch.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hfence.h>
#include <sbi/sbi_physmem_alias.h>

ulong sbi_memalias_gatp_init(const struct sbi_mem_alias *aliases,
			     riscv_gs_pgtable *gs_pgtable,
			     int scheme)
{
	unsigned int root_lvl = scheme - 1;
	unsigned long root_pgshift = PT_PAGE_SHIFT(root_lvl);
	unsigned long root_pgmask = (1UL << root_pgshift) - 1;
	unsigned long entry, c_base, u_base, *root_pte = *gs_pgtable;
	const struct sbi_mem_alias *alias;

	sbi_printf("%s: Using %d-level G-stage pgtable, granularity %lx\n",
		   __func__, scheme, root_pgmask + 1);

	/* 1:1 map for first half of the G stage */
	for (entry = 0; entry < PT_PTE_ENTRIES * 2; entry++) {
		//root_pte[entry] = root_pte[entry + PT_PTE_ENTRIES * 2] =
		root_pte[entry] =
			(entry << PT_PTE_PPN_SHIFT(root_lvl)) |
				  PT_PTE_VALID |
				  PT_PTE_RWX |
				  PT_PTE_USER |
				  PT_PTE_ACCESSED |
				  PT_PTE_DIRTY;
	}

	for (alias = aliases; alias->size; alias++) {
		sbi_printf("%s: uncached window for %lx,+%lx at %lx\n",
			   __func__,
			   alias->cache_base,
			   alias->size,
			   alias->uncache_base);

		if ((alias->cache_base & root_pgmask) ||
		    (alias->uncache_base & root_pgmask) ||
		    (alias->size & root_pgmask))
			sbi_panic("%s: unaligned memory alias: %lx,+%lx -> %lx\n",
				  __func__,
				  alias->cache_base,
				  alias->size,
				  alias->uncache_base);

		c_base = alias->cache_base >> root_pgshift;
		u_base = alias->uncache_base >> root_pgshift;

		/* map of sizeof(phys addrspace) / 2 + cache_base -> uncache_base */
		for (entry = 0; entry < (alias->size >> root_pgshift); entry++) {
			root_pte[entry + c_base + PT_PTE_ENTRIES * 2] =
				((entry + u_base) << PT_PTE_PPN_SHIFT(root_lvl)) |
				PT_PTE_VALID |
				PT_PTE_RWX |
				PT_PTE_USER |
				PT_PTE_ACCESSED |
				PT_PTE_DIRTY;
		}
	}

	return ((unsigned long)gs_pgtable >> PAGE_SHIFT) |
		((unsigned long)HGATP_MODE(scheme) << HGATP_MODE_SHIFT);
}
