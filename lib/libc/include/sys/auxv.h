#ifndef SYS_AUXV_H
#define SYS_AUXV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AT_NULL   0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_NOTELF 10
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_RANDOM 15
#define AT_HWCAP  16
#define AT_HWCAP2 17

#if defined(__aarch64__)

#define HWCAP_FP       (1 << 0)
#define HWCAP_ASIMD    (1 << 1)
#define HWCAP_EVTSTRM  (1 << 2)
#define HWCAP_AES      (1 << 3)
#define HWCAP_PMULL    (1 << 4)
#define HWCAP_SHA1     (1 << 5)
#define HWCAP_SHA2     (1 << 6)
#define HWCAP_CRC32    (1 << 7)
#define HWCAP_ATOMICS  (1 << 8)
#define HWCAP_FPHP     (1 << 9)
#define HWCAP_ASIMDHP  (1 << 10)
#define HWCAP_CPUID    (1 << 11)
#define HWCAP_ASIMDRDM (1 << 12)
#define HWCAP_JSCVT    (1 << 13)
#define HWCAP_FCMA     (1 << 14)
#define HWCAP_LRCPC    (1 << 15)
#define HWCAP_DCPOP    (1 << 16)
#define HWCAP_SHA3     (1 << 17)
#define HWCAP_SM3      (1 << 18)
#define HWCAP_SM4      (1 << 19)
#define HWCAP_ASIMDDP  (1 << 20)
#define HWCAP_SHA512   (1 << 21)
#define HWCAP_SVE      (1 << 22)
#define HWCAP_ASIMDFHM (1 << 23)
#define HWCAP_DIT      (1 << 24)
#define HWCAP_USCAT    (1 << 25)
#define HWCAP_ILRCPC   (1 << 26)
#define HWCAP_FLAGM    (1 << 27)
#define HWCAP_SSBS     (1 << 28)
#define HWCAP_SB       (1 << 29)
#define HWCAP_PACA     (1 << 30)
#define HWCAP_PACG     (1 << 31)

#define HWCAP2_DCPODP     (1UL << 0)
#define HWCAP2_SVE2       (1UL << 1)
#define HWCAP2_SVEAES     (1UL << 2)
#define HWCAP2_SVEPMULL   (1UL << 3)
#define HWCAP2_SVEBITPERM (1UL << 4)
#define HWCAP2_SVESHA3    (1UL << 5)
#define HWCAP2_SVESM4     (1UL << 6)
#define HWCAP2_FLAGM2     (1UL << 7)
#define HWCAP2_FRINT      (1UL << 8)
#define HWCAP2_SVEI8MM    (1UL << 9)
#define HWCAP2_SVEF32MM   (1UL << 10)
#define HWCAP2_SVEF64MM   (1UL << 11)
#define HWCAP2_SVEBF16    (1UL << 12)
#define HWCAP2_I8MM       (1UL << 13)
#define HWCAP2_BF16       (1UL << 14)
#define HWCAP2_DGH        (1UL << 15)
#define HWCAP2_RNG        (1UL << 16)
#define HWCAP2_BTI        (1UL << 17)
#define HWCAP2_MTE        (1UL << 18)
#define HWCAP2_ECV        (1UL << 19)
#define HWCAP2_AFP        (1UL << 20)
#define HWCAP2_RPRES      (1UL << 21)
#define HWCAP2_MTE3       (1UL << 22)
#define HWCAP2_SME        (1UL << 23)
#define HWCAP2_SME_I16I64 (1UL << 24)
#define HWCAP2_SME_F64F64 (1UL << 25)
#define HWCAP2_SME_I8I32  (1UL << 26)
#define HWCAP2_SME_F16F32 (1UL << 27)
#define HWCAP2_SME_B16F32 (1UL << 28)
#define HWCAP2_SME_F32F32 (1UL << 29)
#define HWCAP2_SME_FA64   (1UL << 30)
#define HWCAP2_WFXT       (1UL << 31)
#define HWCAP2_EBF16      (1UL << 32)
#define HWCAP2_SVE_EBF16  (1UL << 33)
#define HWCAP2_CSSC       (1UL << 34)
#define HWCAP2_RPRFM      (1UL << 35)
#define HWCAP2_SVE2P1     (1UL << 36)
#define HWCAP2_SME2       (1UL << 37)
#define HWCAP2_SME2P1     (1UL << 38)
#define HWCAP2_SMEI16I32  (1UL << 39)
#define HWCAP2_SMEBI32I32 (1UL << 40)
#define HWCAP2_SMEB16B16  (1UL << 41)
#define HWCAP2_SMEF16F16  (1UL << 42)
#define HWCAP2_MOPS       (1UL << 43)
#define HWCAP2_HBC        (1UL << 44)
#define HWCAP2_SVE_B16B16 (1UL << 45)
#define HWCAP2_LRCPC3     (1UL << 46)
#define HWCAP2_LSE128     (1UL << 47)
#define HWCAP2_FPMR       (1UL << 48)
#define HWCAP2_LUT        (1UL << 49)
#define HWCAP2_FAMINMAX   (1UL << 50)
#define HWCAP2_F8CVT      (1UL << 51)
#define HWCAP2_F8FMA      (1UL << 52)
#define HWCAP2_F8DP4      (1UL << 53)
#define HWCAP2_F8DP2      (1UL << 54)
#define HWCAP2_F8E4M3     (1UL << 55)
#define HWCAP2_F8E5M2     (1UL << 56)
#define HWCAP2_SME_LUTV2  (1UL << 57)
#define HWCAP2_SME_F8F16  (1UL << 58)
#define HWCAP2_SME_F8F32  (1UL << 59)
#define HWCAP2_SME_SF8FMA (1UL << 60)
#define HWCAP2_SME_SF8DP4 (1UL << 61)
#define HWCAP2_SME_SF8DP2 (1UL << 62)

#endif

size_t getauxval(size_t type);

#ifdef __cplusplus
}
#endif

#endif