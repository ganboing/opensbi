.altmacro

.macro mem_repeat name:req, ldst:req, op:req, rx:req, ry:req, len:req, signext=0, step=1, n=4096, max=2048
.globl mem_\name
.type mem_\name, @function
mem_\name\():
.set i, 0
.rept \n
.set j, \max - \n * \step + i * \step
1:
	mem_\ldst \op \rx \ry j
	ret
.ifne . - mem_\name - (. - 1b) * (i + 1)
.error "mem_\name is not aligned"
.endif
.set i, i + 1
.endr
.size mem_\name, . - mem_\name

.align 3
.globl mem_\name\()_desc
.type mem_\name\()_desc @object
mem_\name\()_desc:
	.byte \len, \signext, \step, (. - mem_\name) / \n
	.half \max - \n * \step, \n
#if __riscv_xlen == 32
	.word mem_\name
#elif __riscv_xlen == 64
	.dword mem_\name
#endif
.size mem_\name\()_desc, . - mem_\name\()_desc
.endm

.macro mem_load op rs rd imm
	mv \rs, a0
	\op \rd, \imm(\rs)
	mv a0, \rd
.endm

.macro mem_fpld op rs rd imm
	mv \rs, a0
	\op \rd, \imm(\rs)
	fmv.d fa0, \rd
.endm

.macro mem_store op rs1 rs2 imm
	mv \rs2, a1
	mv \rs1, a0
	\op \rs2, \imm(\rs1)
.endm

.macro mem_fpst op rs1 rs2 imm
	fmv.d \rs2, fa0
	mv \rs1, a0
	\op \rs2, \imm(\rs1)
.endm

.macro mem_ldsp op tmp rd imm
	mv \tmp, sp
	mv sp, a0
	\op \rd, \imm(sp)
	mv sp, \tmp
	mv a0, \rd
.endm

.macro mem_fpldsp op tmp rd imm
	mv \tmp, sp
	mv sp, a0
	\op \rd, \imm(sp)
	mv sp, \tmp
	fmv.d fa0, \rd
.endm

.macro mem_stsp op tmp rs2 imm
	mv \tmp, sp
	mv sp, a0
	mv \rs2, a1
	\op \rs2, \imm(sp)
	mv sp, \tmp
.endm

.macro mem_fpstsp op tmp rs2 imm
	mv \tmp, sp
	mv sp, a0
	fmv.d \rs2, fa0
	\op \rs2, \imm(sp)
	mv sp, \tmp
.endm

mem_repeat lb     load   lb    t0 a1   1 1
mem_repeat lbu    load   lbu   t1 a2   1
mem_repeat sb     store  sb    t2 a3   1
mem_repeat lh     load   lh    t4 a5   2 1
mem_repeat lhu    load   lhu   t5 a6   2
mem_repeat sh     store  sh    t6 a7   2
mem_repeat lw     load   lw    a7 t6   4 1
#if __riscv_xlen == 64
mem_repeat lwu    load   lwu   a6 t5   4
#endif
mem_repeat sw     store  sw    a5 t4   4
#if __riscv_xlen == 64
mem_repeat ld     load   ld    a4 t3   8 1
mem_repeat sd     store  sd    a3 t2   8
#endif
mem_repeat flw    fpld   flw   t3 ft8  4
mem_repeat fsw    fpst   fsw   t4 ft9  4
mem_repeat fld    fpld   fld   t5 ft10 8
mem_repeat fsd    fpst   fsd   t6 ft11 8
#ifdef __riscv_zcb
mem_repeat clbu   load   c.lbu a5 a1   1 0 1 4  4
mem_repeat csb    store  c.sb  a4 a2   1 0 1 4  4
mem_repeat clh    load   c.lh  a3 a3   2 1 2 2  4
mem_repeat clhu   load   c.lhu a2 a4   2 0 2 2  4
mem_repeat csh    store  c.sh  a1 a5   2 0 2 2  4
#endif
mem_repeat clw    load   c.lw  a5 a1   4 1 4 32 128
mem_repeat csw    store  c.sw  a4 a2   4 0 4 32 128
mem_repeat clwsp  ldsp   lw    t0 t6   4 1 4 64 256
mem_repeat cswsp  stsp   sw    t1 t5   4 0 4 64 256
mem_repeat cfld   fpld   c.fld a3 fa2  8 0 8 32 256
mem_repeat cfsd   fpst   c.fsd a2 fa3  8 0 8 32 256
mem_repeat cfldsp fpldsp fld   t2 ft10 8 0 8 64 512
mem_repeat cfsdsp fpstsp fsd   t3 ft11 8 0 8 64 512
#if __riscv_xlen == 32
mem_repeat cflw   fpld   c.flw a5 fa4  4 0 4 32 128
mem_repeat cfsw   fpst   c.fsw a4 fa5  4 0 4 32 128
mem_repeat cflwsp fpldsp flw   t3 ft11 4 0 4 64 256
mem_repeat cfswsp fpstsp fsw   t4 ft10 4 0 4 64 256
#elif __riscv_xlen == 64
mem_repeat cld    load   c.ld  a5 a4   8 1 8 32 256
mem_repeat csd    store  c.sd  a4 a5   8 0 8 32 256
mem_repeat cldsp  ldsp   ld    t3 t6   8 1 8 64 512
mem_repeat csdsp  stsp   sd    t4 t5   8 0 8 64 512
#endif
