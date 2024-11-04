#include "elf_64.h"

#define ELFEM EM_X86_64

#define R_NONE         R_X86_64_NONE
#define R_RELATIVE64   R_X86_64_RELATIVE
#define R_JMP_SLOT64   R_X86_64_JUMP_SLOT
#define R_GLOB_DAT64   R_X86_64_GLOB_DAT
#define R_ABS32        R_X86_64_32
#define R_ABS64        R_X86_64_64
#define R_TLS_DTPMOD64 R_X86_64_DTPMOD64
#define R_TLS_DTPOFF64 R_X86_64_DTPOFF64
#define R_TLS_TPOFF64  R_X86_64_TPOFF64
