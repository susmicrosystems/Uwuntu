#if __riscv_xlen == 64
#define ll ld
#define sl sd
#else
#define ll lw
#define sl sw
#endif

#define TF_PC  (0 * __SIZEOF_POINTER__)
#define TF_RA  (1 * __SIZEOF_POINTER__)
#define TF_SP  (2 * __SIZEOF_POINTER__)
#define TF_GP  (3 * __SIZEOF_POINTER__)
#define TF_TP  (4 * __SIZEOF_POINTER__)
#define TF_T0  (5 * __SIZEOF_POINTER__)
#define TF_T1  (6 * __SIZEOF_POINTER__)
#define TF_T2  (7 * __SIZEOF_POINTER__)
#define TF_FP  (8 * __SIZEOF_POINTER__)
#define TF_S1  (9 * __SIZEOF_POINTER__)
#define TF_A0  (10 * __SIZEOF_POINTER__)
#define TF_A1  (11 * __SIZEOF_POINTER__)
#define TF_A2  (12 * __SIZEOF_POINTER__)
#define TF_A3  (13 * __SIZEOF_POINTER__)
#define TF_A4  (14 * __SIZEOF_POINTER__)
#define TF_A5  (15 * __SIZEOF_POINTER__)
#define TF_A6  (16 * __SIZEOF_POINTER__)
#define TF_A7  (17 * __SIZEOF_POINTER__)
#define TF_S2  (18 * __SIZEOF_POINTER__)
#define TF_S3  (19 * __SIZEOF_POINTER__)
#define TF_S4  (20 * __SIZEOF_POINTER__)
#define TF_S5  (21 * __SIZEOF_POINTER__)
#define TF_S6  (22 * __SIZEOF_POINTER__)
#define TF_S7  (23 * __SIZEOF_POINTER__)
#define TF_S8  (24 * __SIZEOF_POINTER__)
#define TF_S9  (25 * __SIZEOF_POINTER__)
#define TF_S10 (26 * __SIZEOF_POINTER__)
#define TF_S11 (27 * __SIZEOF_POINTER__)
#define TF_T3  (28 * __SIZEOF_POINTER__)
#define TF_T4  (29 * __SIZEOF_POINTER__)
#define TF_T5  (30 * __SIZEOF_POINTER__)
#define TF_T6  (31 * __SIZEOF_POINTER__)

.global arch_waitq_sleep
.type arch_waitq_sleep, %function
arch_waitq_sleep:
	ll t0, __SIZEOF_POINTER__ * 1(tp)
	addi t0, t0, 520
	sl ra, TF_PC(t0)
	sl sp, TF_SP(t0)
	sl gp, TF_GP(t0)
	sl tp, TF_TP(t0)
	sl fp, TF_FP(t0)
	sl s1, TF_S1(t0)
	sl s2, TF_S2(t0)
	sl s3, TF_S3(t0)
	sl s4, TF_S4(t0)
	sl s5, TF_S5(t0)
	sl s6, TF_S6(t0)
	sl s7, TF_S7(t0)
	sl s8, TF_S8(t0)
	sl s9, TF_S9(t0)
	sl s10, TF_S10(t0)
	sl s11, TF_S11(t0)
	j waitq_sleep_return
