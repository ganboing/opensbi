/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_fp.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_trap_ldst.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_console.h>

/**
 * Load emulator callback:
 * @insn: The instruction that caused the load fault (optional). If 0,
 *        it's not fetched by caller, and the emulator can fetch it
 *        on a need basis.
 * @rlen: Read length in [0, 1, 2, 4, 8]. If 0, it's a special load.
 *        In that case, it could be a vector load or customized insn,
 *        which may read a block of memory. The emulator should further
 *        parse the @insn (and fetch if 0), then properly emulate it.
 * @addr: Read address. If @rlen is not 0, it's the base address of the
 *        load. It doesn't necessarily match tcntx->trap->tval, in case
 *        of unaligned load triggering access fault.
 *        If @rlen is 0, @addr is ignored.
 * @out_val: The buffer to hold data loaded by the emulator.
 *           If @rlen == 0, @out_val is ignored.
 * @tcntx: Trap context saved on load fault entry.
 *
 * @return >0:  Register will be set by caller if @rlen != 0, and
 *              mepc will be advanced by caller.
 *         ==0: No regs modification; No mepc advancement.
 *         <0:  Failure
 *
 * It's expected that if @rlen != 0, and the emulation is successful, the
 * caller will set the corresponding registers with @out_val to simplify
 * things. Otherwise, no register manipulation is done by the caller.
 */
typedef int (*sbi_trap_ld_emulator)(ulong insn, int rlen, ulong addr,
				    union sbi_ldst_data *out_val,
				    struct sbi_trap_context *tcntx);

/**
 * Store emulator callback:
 * @insn: The instruction that caused the store fault (optional). If 0,
 *        it's not fetched by caller, and the emulator can fetch it
 *        on a need basis.
 * @wlen: Write length in [0, 1, 2, 4, 8]. If 0, it's a special store.
 *        In that case, it could be a vector store or customized insn,
 *        which may write a block of memory. The emulator should further
 *        parse the @insn (and fetch if 0), then properly emulate it.
 * @addr: Write address. If @wlen is not 0, it's the base address of the
 *        store. It doesn't necessarily match tcntx->trap->tval, in case
 *        of unaligned store triggering access fault.
 *        If @wlen is 0, @addr is ignored.
 * @in_val: The buffer to hold data about to be stored by the emulator.
 *          If @wlen == 0, @in_val should be ignored.
 * @tcntx: Trap context saved on store fault entry.
 *
 * @return >0:  mepc will be advanced by caller.
 *         ==0: No mepc advancement.
 *         <0:  Failure
 */
typedef int (*sbi_trap_st_emulator)(ulong insn, int wlen, ulong addr,
				    union sbi_ldst_data in_val,
				    struct sbi_trap_context *tcntx);

ulong sbi_misaligned_tinst_fixup(ulong orig_tinst, ulong new_tinst,
					ulong addr_offset)
{
	if (new_tinst == INSN_PSEUDO_VS_LOAD ||
	    new_tinst == INSN_PSEUDO_VS_STORE)
		return new_tinst;
	else if (orig_tinst == 0)
		return 0UL;
	else
		return orig_tinst | (addr_offset << SH_RS1);
}

static int sbi_trap_emulate_load(struct sbi_trap_context *tcntx,
				 sbi_trap_ld_emulator emu)
{
	const struct sbi_trap_info *orig_trap = &tcntx->trap;
	struct sbi_trap_regs *regs = &tcntx->regs;
	ulong insn, insn_len, imm = 0, shift = 0, off = 0;
	union sbi_ldst_data val = { 0 };
	struct sbi_trap_info uptrap;
	bool xform = false, c_load = false, c_ldsp = false;
	int rc, len = 0, fp = 0, prev_xlen = 0;

	if (orig_trap->tinst & 0x1) {
		/*
		 * Bit[0] == 1 implies trapped instruction value is
		 * transformed instruction or custom instruction.
		 */
		xform    = true;
		insn	 = orig_trap->tinst | INSN_16BIT_MASK;
		insn_len = (orig_trap->tinst & 0x2) ? INSN_LEN(insn) : 2;
	} else {
		/*
		 * Bit[0] == 0 implies trapped instruction value is
		 * zero or special value.
		 */
		insn = sbi_get_insn(regs->mepc, &uptrap);
		if (uptrap.cause) {
			return sbi_trap_redirect(regs, &uptrap);
		}
		insn_len = INSN_LEN(insn);
	}

	/**
	 * Common for RV32/RV64:
	 *    lb, lbu, lh, lhu, lw, flw, flw
	 *    c.lbu, c.lh, c.lhu, c.lw, c.lwsp, c.fld, c.fldsp
	 */
	if ((insn & INSN_MASK_LB) == INSN_MATCH_LB) {
		len = -1;
	} else if ((insn & INSN_MASK_LBU) == INSN_MATCH_LBU) {
		len = 1;
	} else if ((insn & INSN_MASK_C_LBU) == INSN_MATCH_C_LBU) {
		/* Zcb */
		len = 1;
		imm = RVC_LB_IMM(insn);
		c_load = true;
	} else if ((insn & INSN_MASK_LH) == INSN_MATCH_LH) {
		len = -2;
	} else if ((insn & INSN_MASK_C_LH) == INSN_MATCH_C_LH) {
		/* Zcb */
		len = -2;
		imm = RVC_LH_IMM(insn);
		c_load = true;
	} else if ((insn & INSN_MASK_LHU) == INSN_MATCH_LHU) {
		len = 2;
	} else if ((insn & INSN_MASK_C_LHU) == INSN_MATCH_C_LHU) {
		/* Zcb */
		len = 2;
		imm = RVC_LH_IMM(insn);
		c_load = true;
	} else if ((insn & INSN_MASK_LW) == INSN_MATCH_LW) {
		len = -4;
	} else if ((insn & INSN_MASK_C_LW) == INSN_MATCH_C_LW) {
		/* Zca */
		len = -4;
		imm = RVC_LW_IMM(insn);
		c_load = true;
	} else if ((insn & INSN_MASK_C_LWSP) == INSN_MATCH_C_LWSP) {
		/* Zca */
		len = -4;
		imm = RVC_LWSP_IMM(insn);
		c_ldsp = true;
#ifdef __riscv_flen
	} else if ((insn & INSN_MASK_FLW) == INSN_MATCH_FLW) {
		fp = 4;
	} else if ((insn & INSN_MASK_FLD) == INSN_MATCH_FLD) {
		fp = 8;
	} else if ((insn & INSN_MASK_C_FLD) == INSN_MATCH_C_FLD) {
		/* Zcd */
		fp = 8;
		imm = RVC_LD_IMM(insn);
		c_load = true;
	} else if ((insn & INSN_MASK_C_FLDSP) == INSN_MATCH_C_FLDSP) {
		/* Zcd */
		fp = 8;
		imm = RVC_LDSP_IMM(insn);
		c_ldsp = true;
#endif
	} else {
		prev_xlen = sbi_regs_prev_xlen(regs);
	}

	/**
	 * Must distinguish between rv64 and rv32, RVC instructions have
	 * overlapping encoding:
	 *     c.ld in rv64 == c.flw in rv32
	 *     c.ldsp in rv64 == c.flwsp in rv32
	 */
	if (prev_xlen == 64) {
		/* RV64 Only: lwu, ld, c.ld, c.ldsp  */
		if ((insn & INSN_MASK_LWU) == INSN_MATCH_LWU) {
			len = 4;
		} else if ((insn & INSN_MASK_LD) == INSN_MATCH_LD) {
			len = 8;
		} else if ((insn & INSN_MASK_C_LD) == INSN_MATCH_C_LD) {
			/* Zca */
			len = 8;
			imm = RVC_LD_IMM(insn);
			c_load = true;
		} else if ((insn & INSN_MASK_C_LDSP) == INSN_MATCH_C_LDSP &&
			GET_RD_NUM(insn)) {
			/* Zca */
			len = 8;
			imm = RVC_LDSP_IMM(insn);
			c_ldsp = true;
		}
#ifdef __riscv_flen
	} else if (prev_xlen == 32) {
		/* RV32 Only: c.flw, c.flwsp */
		if ((insn & INSN_MASK_C_FLW) == INSN_MATCH_C_FLW) {
			/* Zcf */
			fp = 4;
			imm = RVC_LW_IMM(insn);
			c_load = true;
		} else if ((insn & INSN_MASK_C_FLWSP) == INSN_MATCH_C_FLWSP) {
			/* Zcf */
			fp = 4;
			imm = RVC_LWSP_IMM(insn);
			c_ldsp = true;
		}
#endif
	}

	if (fp)
		len = fp;

	if (!len || orig_trap->cause == CAUSE_MISALIGNED_LOAD)
		/* Unknown instruction or no need to calculate offset */
		goto do_emu;

	if (xform)
		/* Transformed insn */
		off = GET_RS1_NUM(insn);
	else if (c_load)
		/* non SP-based compressed load */
		off = orig_trap->tval - GET_RS1S(insn, regs) - imm;
	else if (c_ldsp)
		/* SP-based compressed load */
		off = orig_trap->tval - REG_VAL(2, regs) - imm;
	else
		/* I-type non-compressed load */
		off = orig_trap->tval - GET_RS1(insn, regs) - (ulong)IMM_I(insn);
	/**
	 * Normalize offset, in case the XLEN of unpriv mode is smaller,
	 * or pointer masking is in effect
	 */
	//off &= (len - 1);

do_emu:
	if (len < 0) {
		len = -len;
		shift = 8 * (sizeof(ulong) - len);
	}
	rc = emu(xform ? 0 : insn, len, orig_trap->tval - off, &val, tcntx);
	if (rc <= 0)
		return rc;
	if (!len)
		goto epc_fixup;

	if (!fp) {
		ulong v = ((long)(val.data_ulong << shift)) >> shift;

		if (c_load)
			SET_RD2S(insn, regs, v);
		else
			SET_RD(insn, regs, v);
#ifdef __riscv_flen
	} else if (fp == 8) {
		if (c_load)
			SET_F64_RD2S(insn, regs, val.data_u64);
		else
			SET_F64_RD(insn, regs, val.data_u64);
	} else {
		if (c_load)
			SET_F32_RD2S(insn, regs, val.data_ulong);
		else
			SET_F32_RD(insn, regs, val.data_ulong);
#endif
	}

epc_fixup:
	regs->mepc += insn_len;

	return 0;
}

static int sbi_trap_emulate_store(struct sbi_trap_context *tcntx,
				  sbi_trap_st_emulator emu)
{
	const struct sbi_trap_info *orig_trap = &tcntx->trap;
	struct sbi_trap_regs *regs = &tcntx->regs;
	ulong insn, insn_len, imm = 0, off = 0;
	union sbi_ldst_data val;
	struct sbi_trap_info uptrap;
	bool xform = false, c_store = false, c_stsp = false;
	int rc, len = 0, fp = 0, prev_xlen = 0;

	if (orig_trap->tinst & 0x1) {
		/*
		 * Bit[0] == 1 implies trapped instruction value is
		 * transformed instruction or custom instruction.
		 */
		xform    = true;
		insn	 = orig_trap->tinst | INSN_16BIT_MASK;
		insn_len = (orig_trap->tinst & 0x2) ? INSN_LEN(insn) : 2;
	} else {
		/*
		 * Bit[0] == 0 implies trapped instruction value is
		 * zero or special value.
		 */
		insn = sbi_get_insn(regs->mepc, &uptrap);
		if (uptrap.cause) {
			return sbi_trap_redirect(regs, &uptrap);
		}
		insn_len = INSN_LEN(insn);
	}

	/**
	 * Common for RV32/RV64:
	 *    sb, sh, sw, fsw, fsd
	 *    c.sb, c.sh, c.sw, c.swsp, c.fsd, c.fsdsp
	 */
	if ((insn & INSN_MASK_SB) == INSN_MATCH_SB) {
		len = 1;
	} else if ((insn & INSN_MASK_C_SB) == INSN_MATCH_C_SB) {
		/* Zcb */
		len = 1;
		imm = RVC_SB_IMM(insn);
		c_store = true;
	} else if ((insn & INSN_MASK_SH) == INSN_MATCH_SH) {
		len = 2;
	} else if ((insn & INSN_MASK_C_SH) == INSN_MATCH_C_SH) {
		/* Zcb */
		len = 2;
		imm = RVC_SH_IMM(insn);
		c_store = true;
	} else if ((insn & INSN_MASK_SW) == INSN_MATCH_SW) {
		len = 4;
	} else if ((insn & INSN_MASK_C_SW) == INSN_MATCH_C_SW) {
		/* Zca */
		len = 4;
		imm = RVC_SW_IMM(insn);
		c_store = true;
	} else if ((insn & INSN_MASK_C_SWSP) == INSN_MATCH_C_SWSP) {
		/* Zca */
		len = 4;
		imm = RVC_SWSP_IMM(insn);
		c_stsp = true;
#ifdef __riscv_flen
	} else if ((insn & INSN_MASK_FSW) == INSN_MATCH_FSW) {
		fp = 4;
	} else if ((insn & INSN_MASK_FSD) == INSN_MATCH_FSD) {
		fp = 8;
	} else if ((insn & INSN_MASK_C_FSD) == INSN_MATCH_C_FSD) {
		/* Zcd */
		fp = 8;
		imm = RVC_SD_IMM(insn);
		c_store = true;
	} else if ((insn & INSN_MASK_C_FSDSP) == INSN_MATCH_C_FSDSP) {
		/* Zcd */
		fp = 8;
		imm = RVC_SDSP_IMM(insn);
		c_stsp = true;
#endif
	} else {
		prev_xlen = sbi_regs_prev_xlen(regs);
	}

	/**
	 * Must distinguish between rv64 and rv32, RVC instructions have
	 * overlapping encoding:
	 *     c.sd in rv64 == c.fsw in rv32
	 *     c.sdsp in rv64 == c.fswsp in rv32
	 */
	if (prev_xlen == 64) {
		/* RV64 Only: sd, c.sd, c.sdsp */
		if ((insn & INSN_MASK_SD) == INSN_MATCH_SD) {
			len = 8;
		} else if ((insn & INSN_MASK_C_SD) == INSN_MATCH_C_SD) {
			/* Zca */
			len = 8;
			imm = RVC_SD_IMM(insn);
			c_store = true;
		} else if ((insn & INSN_MASK_C_SDSP) == INSN_MATCH_C_SDSP) {
			/* Zca */
			len = 8;
			imm = RVC_SDSP_IMM(insn);
			c_stsp = true;
		}
#ifdef __riscv_flen
	} else if (prev_xlen == 32) {
		/* RV32 Only: c.fsw, c.fswsp */
		if ((insn & INSN_MASK_C_FSW) == INSN_MATCH_C_FSW) {
			/* Zcf */
			fp = 4;
			imm = RVC_SW_IMM(insn);
			c_store = true;
		} else if ((insn & INSN_MASK_C_FSWSP) == INSN_MATCH_C_FSWSP) {
			/* Zcf */
			fp = 4;
			imm = RVC_SWSP_IMM(insn);
			c_stsp = true;
		}
#endif
	}

	if (fp)
		len = fp;

	if (!fp) {
		if (c_store)
			val.data_ulong = GET_RS2S(insn, regs);
		else if (c_stsp)
			val.data_ulong = GET_RS2C(insn, regs);
		else
			val.data_ulong = GET_RS2(insn, regs);
#ifdef __riscv_flen
	} else if (fp == 8) {
		if (c_store)
			val.data_u64 = GET_F64_RS2S(insn, regs);
		else if (c_stsp)
			val.data_u64 = GET_F64_RS2C(insn, regs);
		else
			val.data_u64 = GET_F64_RS2(insn, regs);
	} else {
		if (c_store)
			val.data_ulong = GET_F32_RS2S(insn, regs);
		else if (c_stsp)
			val.data_ulong = GET_F32_RS2C(insn, regs);
		else
			val.data_ulong = GET_F32_RS2(insn, regs);
#endif
	}

	if (!len || orig_trap->cause == CAUSE_MISALIGNED_STORE)
		/* Unknown instruction or no need to calculate offset */
		goto do_emu;

	if (xform)
		/* Transformed insn */
		off = GET_RS1_NUM(insn);
	else if (c_store)
		/* non SP-based compressed store */
		off = orig_trap->tval - GET_RS1S(insn, regs) - imm;
	else if (c_stsp)
		/* SP-based compressed store */
		off = orig_trap->tval - REG_VAL(2, regs) - imm;
	else
		/* S-type non-compressed store */
		off = orig_trap->tval - GET_RS1(insn, regs) - (ulong)IMM_S(insn);
	/**
	 * Normalize offset, in case the XLEN of unpriv mode is smaller,
	 * or pointer masking is in effect
	 */
	//off &= (len - 1);

do_emu:
	rc = emu(xform ? 0 : insn, len, orig_trap->tval - off, val, tcntx);
	if (rc <= 0)
		return rc;

	regs->mepc += insn_len;

	return 0;
}

static int sbi_misaligned_ld_emulator(ulong insn, int rlen, ulong addr,
				      union sbi_ldst_data *out_val,
				      struct sbi_trap_context *tcntx)
{
	const struct sbi_trap_info *orig_trap = &tcntx->trap;
	struct sbi_trap_regs *regs = &tcntx->regs;
	struct sbi_trap_info uptrap;
	int i;

	//sbi_printf("%s: epc=%lx, insn=%lx, rlen=%d, addr=%lx\n", __func__, regs->mepc, insn, rlen, addr);
	if (!rlen) {
		if (!insn) {
			insn = sbi_get_insn(regs->mepc, &uptrap);
			if (uptrap.cause)
				return sbi_trap_redirect(regs, &uptrap);
		}
		if (IS_VECTOR_LOAD_STORE(insn))
			return sbi_misaligned_v_ld_emulator(insn, tcntx);
		else
			/* Customized instruction? Can't emulate it. */
			return sbi_trap_redirect(regs, orig_trap);
	}
	/* For misaligned fault, addr must be the same as orig_trap->tval */
	if (addr != orig_trap->tval)
		return SBI_EFAIL;

	for (i = 0; i < rlen; i++) {
		out_val->data_bytes[i] =
			sbi_load_u8((void *)(orig_trap->tval + i), &uptrap);
		if (uptrap.cause) {
			uptrap.tinst = sbi_misaligned_tinst_fixup(
				orig_trap->tinst, uptrap.tinst, i);
			return sbi_trap_redirect(regs, &uptrap);
		}
	}
	return rlen;
}

int sbi_misaligned_load_handler(struct sbi_trap_context *tcntx)
{
	return sbi_trap_emulate_load(tcntx, sbi_misaligned_ld_emulator);
}

static int sbi_misaligned_st_emulator(ulong insn, int wlen, ulong addr,
				      union sbi_ldst_data in_val,
				      struct sbi_trap_context *tcntx)
{
	const struct sbi_trap_info *orig_trap = &tcntx->trap;
	struct sbi_trap_regs *regs = &tcntx->regs;
	struct sbi_trap_info uptrap;
	int i;

	//sbi_printf("%s: epc=%lx, insn=%lx, wlen=%d, addr=%lx\n", __func__, regs->mepc, insn, wlen, addr);
	if (!wlen) {
		if (!insn) {
			insn = sbi_get_insn(regs->mepc, &uptrap);
			if (uptrap.cause)
				return sbi_trap_redirect(regs, &uptrap);
		}
		if (IS_VECTOR_LOAD_STORE(insn))
			return sbi_misaligned_v_st_emulator(insn, tcntx);
		else
			/* Customized instruction? Can't emulate it. */
			return sbi_trap_redirect(regs, orig_trap);
	}
	/* For misaligned fault, addr must be the same as orig_trap->tval */
	if (addr != orig_trap->tval)
		return SBI_EFAIL;

	for (i = 0; i < wlen; i++) {
		sbi_store_u8((void *)(orig_trap->tval + i),
			     in_val.data_bytes[i], &uptrap);
		if (uptrap.cause) {
			uptrap.tinst = sbi_misaligned_tinst_fixup(
				orig_trap->tinst, uptrap.tinst, i);
			return sbi_trap_redirect(regs, &uptrap);
		}
	}
	return wlen;
}

int sbi_misaligned_store_handler(struct sbi_trap_context *tcntx)
{
	return sbi_trap_emulate_store(tcntx, sbi_misaligned_st_emulator);
}

static int sbi_ld_access_emulator(ulong insn, int rlen, ulong addr,
				  union sbi_ldst_data *out_val,
				  struct sbi_trap_context *tcntx)
{
	const struct sbi_trap_info *orig_trap = &tcntx->trap;
	struct sbi_trap_regs *regs = &tcntx->regs;
	int rc;

	/* If fault came from M mode, just fail */
	if (sbi_mstatus_prev_mode(regs->mstatus) == PRV_M)
		return SBI_EINVAL;

	rc = sbi_platform_emulate_load(sbi_platform_thishart_ptr(),
				       insn, rlen, addr, out_val, tcntx);

	/* If platform emulator failed, we redirect instead of fail */
	if (rc < 0)
		return sbi_trap_redirect(regs, orig_trap);

	return rc;
}

int sbi_load_access_handler(struct sbi_trap_context *tcntx)
{
	return sbi_trap_emulate_load(tcntx, sbi_ld_access_emulator);
}

static int sbi_st_access_emulator(ulong insn, int wlen, ulong addr,
				  union sbi_ldst_data in_val,
				  struct sbi_trap_context *tcntx)
{
	const struct sbi_trap_info *orig_trap = &tcntx->trap;
	struct sbi_trap_regs *regs = &tcntx->regs;
	int rc;

	/* If fault came from M mode, just fail */
	if (sbi_mstatus_prev_mode(regs->mstatus) == PRV_M)
		return SBI_EINVAL;

	rc = sbi_platform_emulate_store(sbi_platform_thishart_ptr(),
					insn, wlen, addr, in_val, tcntx);

	/* If platform emulator failed, we redirect instead of fail */
	if (rc < 0)
		return sbi_trap_redirect(regs, orig_trap);

	return rc;
}

int sbi_store_access_handler(struct sbi_trap_context *tcntx)
{
	return sbi_trap_emulate_store(tcntx, sbi_st_access_emulator);
}
