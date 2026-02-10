#include <inttypes.h>

typedef struct {
	uint8_t len;
	uint8_t signext;
	uint8_t imm_step;
	uint8_t isize;
	int16_t imm_base;
	uint16_t n;
	void *fp;
} mem_op_desc;

typedef union {
	unsigned long u_long;
	long i_long;
	uint32_t u32[2];
	int32_t i32[2];
	uint64_t u64;
	int64_t i64;
	float f32[2];
	double f64;
	uint8_t bytes[8];
} ldst_val;

extern mem_op_desc mem_lb_desc;
extern mem_op_desc mem_lbu_desc;
extern mem_op_desc mem_sb_desc;
extern mem_op_desc mem_lh_desc;
extern mem_op_desc mem_lhu_desc;
extern mem_op_desc mem_sh_desc;
extern mem_op_desc mem_lw_desc;
extern mem_op_desc mem_lwu_desc;
extern mem_op_desc mem_sw_desc;
extern mem_op_desc mem_ld_desc;
extern mem_op_desc mem_sd_desc;
extern mem_op_desc mem_flw_desc;
extern mem_op_desc mem_fsw_desc;
extern mem_op_desc mem_fld_desc;
extern mem_op_desc mem_fsd_desc;
extern mem_op_desc mem_clbu_desc;
extern mem_op_desc mem_csb_desc;
extern mem_op_desc mem_clh_desc;
extern mem_op_desc mem_clhu_desc;
extern mem_op_desc mem_csh_desc;
extern mem_op_desc mem_clw_desc;
extern mem_op_desc mem_csw_desc;
extern mem_op_desc mem_clwsp_desc;
extern mem_op_desc mem_cswsp_desc;
extern mem_op_desc mem_cfld_desc;
extern mem_op_desc mem_cfsd_desc;
extern mem_op_desc mem_cfldsp_desc;
extern mem_op_desc mem_cfsdsp_desc;
extern mem_op_desc mem_cflw_desc;
extern mem_op_desc mem_cfsw_desc;
extern mem_op_desc mem_cflwsp_desc;
extern mem_op_desc mem_cfswsp_desc;
extern mem_op_desc mem_cld_desc;
extern mem_op_desc mem_csd_desc;
extern mem_op_desc mem_cldsp_desc;
extern mem_op_desc mem_csdsp_desc;

typedef struct ldst_func {
	const char *name;
	const mem_op_desc *desc;
} ldst_func;

#define DEF_LDST_FUNC(x) { #x, &mem_ ## x ## _desc }

static const ldst_func load_funcs[] = {
	DEF_LDST_FUNC(lb),
	DEF_LDST_FUNC(lbu),
	DEF_LDST_FUNC(lh),
	DEF_LDST_FUNC(lhu),
	DEF_LDST_FUNC(lw),
#ifdef __riscv_zcb
	DEF_LDST_FUNC(clbu),
	DEF_LDST_FUNC(clh),
	DEF_LDST_FUNC(clhu),
#endif
	DEF_LDST_FUNC(clw),
	DEF_LDST_FUNC(clwsp),
#if __riscv_xlen == 64
	DEF_LDST_FUNC(lwu),
	DEF_LDST_FUNC(ld),
	DEF_LDST_FUNC(cld),
	DEF_LDST_FUNC(cldsp),
#endif
};

static const ldst_func store_funcs[] = {
	DEF_LDST_FUNC(sb),
	DEF_LDST_FUNC(sh),
	DEF_LDST_FUNC(sw),
#ifdef __riscv_zcb
	DEF_LDST_FUNC(csb),
	DEF_LDST_FUNC(csh),
#endif
	DEF_LDST_FUNC(csw),
	DEF_LDST_FUNC(cswsp),
#if __riscv_xlen == 64
	DEF_LDST_FUNC(sd),
	DEF_LDST_FUNC(csd),
	DEF_LDST_FUNC(csdsp),
#endif
};

static const ldst_func loadfp_funcs[] = {
#if __riscv_xlen == 32
	DEF_LDST_FUNC(cflw),
	DEF_LDST_FUNC(cflwsp),
#endif
	DEF_LDST_FUNC(flw),
	DEF_LDST_FUNC(fld),
	DEF_LDST_FUNC(cfld),
	DEF_LDST_FUNC(cfldsp),
};

static const ldst_func storefp_funcs[] = {
#if __riscv_xlen == 32
	DEF_LDST_FUNC(cfsw),
	DEF_LDST_FUNC(cfswsp),
#endif
	DEF_LDST_FUNC(fsw),
	DEF_LDST_FUNC(fsd),
	DEF_LDST_FUNC(cfsd),
	DEF_LDST_FUNC(cfsdsp),
};

static inline unsigned long load_i(const void *p, unsigned func, unsigned sel)
{
	typedef unsigned long (*func_i)(const void *p);
	const mem_op_desc *desc = load_funcs[func].desc;

	long imm = (long)desc->imm_base + desc->imm_step * sel;
	func_i f = desc->fp + desc->isize * sel;

	return f(p - imm);
}

static inline void store_i(void *p, unsigned long val, unsigned func, unsigned sel)
{
	typedef void (*func_i)(void *p, unsigned long val);
	const mem_op_desc *desc = store_funcs[func].desc;

	long imm = (long)desc->imm_base + desc->imm_step * sel;
	func_i f = desc->fp + desc->isize * sel;
	f(p - imm, val);
}

static inline double load_f(const void *p, unsigned func, unsigned sel)
{
	typedef double (*func_f)(const void *p);
	const mem_op_desc *desc = loadfp_funcs[func].desc;

	long imm = (long)desc->imm_base + desc->imm_step * sel;
	func_f f = desc->fp + desc->isize * sel;

	return f(p - imm);
}

static inline void store_f(void *p, double val, unsigned func, unsigned sel)
{
	typedef void (*func_f)(void *p, double val);
	const mem_op_desc *desc = storefp_funcs[func].desc;

	long imm = (long)desc->imm_base + desc->imm_step * sel;
	func_f f = desc->fp + desc->isize * sel;

	f(p - imm, val);
}
