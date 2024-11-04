#include "elf_32.h"

#define ELFEM EM_386

#define R_NONE         R_386_NONE
#define R_RELATIVE32   R_386_RELATIVE
#define R_JMP_SLOT32   R_386_JMP_SLOT
#define R_GLOB_DAT32   R_386_GLOB_DAT
#define R_ABS32        R_386_32
#define R_PC32         R_386_PC32
#define R_TLS_DTPMOD32 R_386_TLS_DTPMOD32
#define R_TLS_DTPOFF32 R_386_TLS_DTPOFF32
#define R_TLS_TPOFF32  R_386_TLS_TPOFF32
