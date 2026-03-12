/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Bo Gan <ganboing@gmail.com>
 *
 */

#ifndef __SBI_VS_PASSTHROUGH_H__
#define __SBI_VS_PASSTHROUGH_H__

#include <sbi/sbi_trap.h>
#include <sbi/sbi_ecall.h>

extern ulong sbi_vspt_hgatp;

int sbi_vs_passth_enable(ulong hgatp);

static inline bool sbi_vs_passth_active(void)
{
	return sbi_vspt_hgatp != 0;
}

void sbi_vs_passth_before_handoff(void);

bool sbi_vs_passth_filter_ecall(struct sbi_trap_context *tcntx);

void sbi_vs_passth_sext_irq(void);

int sbi_vs_passth_sys_insn(ulong insn, struct sbi_trap_context *tcntx);

int sbi_vs_passth_csr_read(int csr_num,
			  struct sbi_trap_context *tcntx,
			  ulong *csr_val);

int sbi_vs_passth_csr_write(int csr_num,
			   struct sbi_trap_context *tcntx,
			   ulong csr_val);

#endif
