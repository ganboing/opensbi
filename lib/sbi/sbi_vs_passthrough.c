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
#include <sbi/sbi_vs_passthrough.h>
#include <sbi/sbi_hfence.h>
#include <sbi/sbi_illegal_insn.h>

ulong sbi_vspt_hgatp;

struct sbi_vspt_context {
	unsigned long last_sret;
	struct {
		unsigned long hstatus;
		unsigned long hgatp;
		unsigned long hedeleg;
		unsigned long hideleg;
		unsigned long hvip;
		unsigned long hip;
		unsigned long hie;
		unsigned long hcounteren;
		unsigned long htval;
		unsigned long htinst;
	};
	struct {
		unsigned long vsstatus;
		unsigned long vsip;
		unsigned long vsie;
		unsigned long vsscratch;
		unsigned long vsepc;
		unsigned long vscause;
		unsigned long vstval;
		unsigned long vsatp;
	};
};

static unsigned long ctx_offset;

int sbi_vs_passth_enable(ulong hgatp)
{
	ctx_offset = sbi_scratch_alloc_type_offset(
			struct sbi_vspt_context);

	if (!ctx_offset)
		return SBI_ENOMEM;

	sbi_vspt_hgatp = hgatp;

	return 0;
}

bool sbi_vs_passth_filter_ecall(struct sbi_trap_context *tcntx)
{
	switch (tcntx->regs.a7) {
	case SBI_EXT_BASE:
	case SBI_EXT_TIME:
	case SBI_EXT_HSM:
	case SBI_EXT_IPI:
	case SBI_EXT_DBCN:
	case SBI_EXT_SRST:
		return true;
	case SBI_EXT_RFENCE:
		switch (tcntx->regs.a6) {
		case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
			return true;
		}
		break;
	case SBI_EXT_FWFT:
		switch (tcntx->regs.a0) {
		case SBI_FWFT_MISALIGNED_EXC_DELEG:
			return true;
		}
		break;
	case SBI_EXT_PMU: //TODO
		return false;
	case SBI_EXT_VENDOR_START + 0x489:
		switch (tcntx->regs.a6) {
		case 0x50425543:
			return true;
		}
		break;
	}
	sbi_panic("%s: Don't know how to handle ecall %lx,%lx\n", __func__,
		  tcntx->regs.a7,
		  tcntx->regs.a6);
	return false;
}

static void sbi_vs_passth_prep_hv(void)
{
	unsigned long mstatus = csr_read(CSR_MSTATUS), sxl =
		(mstatus & MSTATUS_SXL) >> MSTATUS_SXL_SHIFT;
	unsigned long hstatus = sxl << HSTATUS_VSXL_SHIFT;

	csr_write(CSR_HSTATUS, hstatus);
	// Enable ALL HPM
	csr_write(CSR_HCOUNTEREN, -1UL);
	// Using VMID 0
	csr_write(CSR_HGATP, sbi_vspt_hgatp);
	// Delegate all exceptions
	csr_write(CSR_HEDELEG, -1UL);
	// Delegate all interrupts
	csr_write(CSR_HIDELEG, -1UL);
	csr_write(CSR_HVIP, 0);
	csr_write(CSR_HIE, 0);
	csr_write(CSR_SIE, 0);
	// Let M translate S mode external intr
	csr_set(CSR_MIE, MIP_SEIP);
}

void sbi_vs_passth_switch_to_hv(void)
{
}

void sbi_vs_passth_switch_to_guest(void)
{
}

void sbi_vs_passth_before_handoff(void)
{
	struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();
	struct sbi_vspt_context *ctx;

	ctx = sbi_scratch_offset_ptr(scratch, ctx_offset);
	sbi_memset(ctx, 0, sizeof(*ctx));

	__sbi_hfence_gvma_all();
	sbi_vs_passth_prep_hv();

	sbi_printf("%s: handing off...\n", __func__);
}

static bool sbi_vs_passth_sync_vseip(void)
{
	if (csr_read(CSR_MIP) & MIP_SEIP) {
		csr_set(CSR_HVIP, MIP_VSEIP);
		csr_clear(CSR_MIE, MIP_SEIP);
		return true;
	} else {
		csr_clear(CSR_HVIP, MIP_VSEIP);
		csr_set(CSR_MIE, MIP_SEIP);
		return false;
	}
}

void sbi_vs_passth_sext_irq(void)
{
	csr_set(CSR_HSTATUS, HSTATUS_VTSR);
	csr_set(CSR_HVIP, MIP_VSEIP);
	csr_clear(CSR_MIE, MIP_SEIP);
}

int sbi_vs_passth_sys_insn(ulong insn, struct sbi_trap_context *tcntx)
{
	struct sbi_trap_regs *regs = &tcntx->regs;
	ulong vsstatus, next_mode;

	switch (insn) {
	case 0x10200073: // sret
		if (!sbi_vs_passth_sync_vseip()) {
			// Disable trapping sret and continue
			csr_clear(CSR_HSTATUS, HSTATUS_VTSR);
			break;
		}
		// Emulate it!
		vsstatus = csr_read(CSR_VSSTATUS);
		next_mode = (vsstatus & SSTATUS_SPP) >> SSTATUS_SPP_SHIFT;
		// Sync SPP -> MPP
		regs->mstatus &= ~MSTATUS_MPP;
		regs->mstatus |= (next_mode << MSTATUS_MPP_SHIFT);
		// Clear SPP
		vsstatus &= ~SSTATUS_SPP;
		// Sync SPIE -> SIE
		vsstatus &= ~SSTATUS_SIE;
		if (vsstatus & SSTATUS_SPIE)
			vsstatus |= SSTATUS_SIE;
		// Set SPIE
		vsstatus |= SSTATUS_SPIE;

		csr_set(CSR_VSSTATUS, vsstatus);

		// Sync SEPC -> MEPC
		regs->mepc = csr_read(CSR_VSEPC);
		break;
	default:
		return truly_illegal_insn(insn, tcntx);
	}
	return 0;
}

int sbi_vs_passth_csr_read(int csr_num, struct sbi_trap_context *tcntx,
			  ulong *csr_val)
{
	return SBI_ENOTSUPP;
}

int sbi_vs_passth_csr_write(int csr_num, struct sbi_trap_context *tcntx,
			   ulong csr_val)
{
	return SBI_ENOTSUPP;
}

