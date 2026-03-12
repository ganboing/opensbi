/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Bo Gan <ganboing@gmail.com>
 *
 */

#ifndef __SBI_PHYSMEM_ALIAS_H__
#define __SBI_PHYSMEM_ALIAS_H__

#if __riscv_xlen == 32
#define PT_BIT_PER_LVL		(10)
#define PT_PPN_BITS		(22)
#elif __riscv_xlen == 64
#define PT_BIT_PER_LVL		(9)
#define PT_PPN_BITS		(44)
#endif

#define PT_XPN_SHIFT(lvl) 	(PT_BIT_PER_LVL * (lvl))
#define PT_S_VPN_BITS(lvl)	(PT_XPN_SHIFT(lvl))
#define PT_G_VPN_BITS(lvl)	(PT_XPN_SHIFT(lvl) + 2)
#define PT_PAGE_SHIFT(lvl)	(PT_XPN_SHIFT(lvl) + PAGE_SHIFT)
//#define PT_PPN(x, lvl)		((x) << PT_XPN_SHIFT(lvl))
//#define PT_PAGE_SIZE(lvl)	(1UL << (PT_XPN_SHIFT(lvl) + PAGE_SHIFT))

#define PT_PTE_ENTRIES		(1UL << PT_BIT_PER_LVL)
#define PT_PTE_VALID		(1 << 0)
#define PT_PTE_READ		(1 << 1)
#define PT_PTE_WRITE		(1 << 2)
#define PT_PTE_EXEC		(1 << 3)
#define PT_PTE_USER		(1 << 4)
#define PT_PTE_GLOBAL		(1 << 5)
#define PT_PTE_ACCESSED		(1 << 6)
#define PT_PTE_DIRTY		(1 << 7)
#define PT_PTE_RSW_SHIFT	(8)
#define PT_PTE_PPN_SHIFT(lvl)	(10 + PT_XPN_SHIFT(lvl))
#define PT_PTE_RWX		(PT_PTE_READ | PT_PTE_WRITE | PT_PTE_EXEC)

#define PT_SV32x4		2
#define PT_SV39x4		3
#define PT_SV48x4		4
#define PT_SV57x4		5

struct sbi_mem_alias {
	unsigned long cache_base;
	unsigned long uncache_base;
	unsigned long size;
};

typedef unsigned long riscv_gs_pgtable[PT_PTE_ENTRIES * 4];

ulong sbi_memalias_gatp_init(const struct sbi_mem_alias *aliases,
			     riscv_gs_pgtable *gs_pgtable,
			     int scheme);

#endif
