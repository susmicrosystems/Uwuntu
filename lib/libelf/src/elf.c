#include <libelf.h>
#include <string.h>

const char *elf_ehdr_class_str(uint8_t v)
{
	switch (v)
	{
		case ELFCLASSNONE:
			return "none";
		case ELFCLASS32:
			return "ELF32";
		case ELFCLASS64:
			return "ELF64";
	}
	return NULL;
}

const char *elf_ehdr_data_str(uint8_t v)
{
	switch (v)
	{
		case ELFDATANONE:
			return "none";
		case ELFDATA2LSB:
			return "2's complement, little endian";
		case ELFDATA2MSB:
			return "2's complement, big endian";
	}
	return NULL;
}

const char *elf_ehdr_abi_str(uint8_t v)
{
	switch (v)
	{
		case ELFOSABI_NONE:
			return "UNIX - System V";
		case ELFOSABI_HPUX:
			return "Hewlett-Packard HP-UX";
		case ELFOSABI_NETBSD:
			return "NetBSD";
		case ELFOSABI_LINUX:
			return "Linux";
		case ELFOSABI_SOLARIS:
			return "Sun Solaris";
		case ELFOSABI_AIX:
			return "AIX";
		case ELFOSABI_IRIX:
			return "IRIX";
		case ELFOSABI_FREEBSD:
			return "FreeBSD";
		case ELFOSABI_TRU64:
			return "Compaq TRU64";
		case ELFOSABI_MODESTO:
			return "Novell Modesto";
		case ELFOSABI_OPENBSD:
			return "OpenBSD";
		case ELFOSABI_OPENVMS:
			return "Open VMS";
		case ELFOSABI_NSK:
			return "Hewlett-Packard Non-Stop Kernel";
	}
	return NULL;
}

const char *elf_ehdr_type_str(uint16_t v)
{
	switch (v)
	{
		case ET_NONE:
			return "NONE (None)";
		case ET_REL:
			return "REL (Relocatable file)";
		case ET_EXEC:
			return "EXEC (Executable file)";
		case ET_DYN:
			return "DYN (Shared object file)";
		case ET_CORE:
			return "CORE (Core file)";
	}
	return NULL;
}

const char *elf_ehdr_machine_str(uint16_t v)
{
	switch (v)
	{
		case EM_NONE:
			return "None";
		case EM_M32:
			return "AT&T WE 32100";
		case EM_SPARC:
			return "SPARC";
		case EM_386:
			return "Intel 80386";
		case EM_68K:
			return "Motorola 68000";
		case EM_88K:
			return "Motorola 88000";
		case EM_860:
			return "Intel 80860";
		case EM_MIPS:
			return "MIPS 1";
		case EM_ARM:
			return "ARM";
		case EM_X86_64:
			return "Advanced Micro Devices X86-64";
		case EM_AARCH64:
			return "AArch64";
		case EM_RISCV:
			return "RISC-V";
	}
	return NULL;
}

const char *elf_phdr_type_str(uint32_t v)
{
	switch (v)
	{
#define TEST_PT(n) case PT_##n: return #n

		TEST_PT(NULL);
		TEST_PT(LOAD);
		TEST_PT(DYNAMIC);
		TEST_PT(INTERP);
		TEST_PT(NOTE);
		TEST_PT(SHLIB);
		TEST_PT(PHDR);
		TEST_PT(TLS);
		TEST_PT(GNU_EH_FRAME);
		TEST_PT(GNU_STACK);
		TEST_PT(GNU_RELRO);
		TEST_PT(EXIDX);
		TEST_PT(RISCV_ATTRIBUTES);

#undef TEST_PT
	}
	return NULL;
}

const char *elf_phdr_flags_str(uint8_t v)
{
	static const char str[8][4] =
	{
		"   ",
		"  E",
		" W ",
		" WE",
		"R  ",
		"R E",
		"RW ",
		"RWE",
	};
	return str[v & 0x7];
}

const char *elf_shdr_type_str(uint32_t v)
{
	switch (v)
	{
#define TEST_SHT(n) case SHT_##n: return #n

		TEST_SHT(NULL);
		TEST_SHT(PROGBITS);
		TEST_SHT(SYMTAB);
		TEST_SHT(STRTAB);
		TEST_SHT(RELA);
		TEST_SHT(HASH);
		TEST_SHT(DYNAMIC);
		TEST_SHT(NOTE);
		TEST_SHT(NOBITS);
		TEST_SHT(REL);
		TEST_SHT(SHLIB);
		TEST_SHT(DYNSYM);
		TEST_SHT(INIT_ARRAY);
		TEST_SHT(FINI_ARRAY);
		TEST_SHT(GNU_HASH);
		TEST_SHT(VERDEF);
		TEST_SHT(VERNEED);
		TEST_SHT(VERSYM);
		TEST_SHT(RISCV_ATTRIBUTES);

#undef TEST_SHT
	}
	return NULL;
}

const char *elf_shdr_flags_str(char *buf, size_t size, uint32_t v)
{
#define TEST_FLAG(v, f, c) \
do \
{ \
	if (!((v) & (f))) \
		break; \
	if (n >= size) \
		break;\
	buf[n++] = c; \
} while (0)

	size_t n = 0;
	TEST_FLAG(v, SHF_WRITE, 'W');
	TEST_FLAG(v, SHF_ALLOC, 'A');
	TEST_FLAG(v, SHF_EXECINSTR, 'X');
	TEST_FLAG(v, SHF_MERGE, 'M');
	TEST_FLAG(v, SHF_STRINGS, 'S');
	TEST_FLAG(v, SHF_INFO_LINK, 'I');
	TEST_FLAG(v, SHF_LINK_ORDER, 'L');
	TEST_FLAG(v, SHF_OS_NONCONFORMING, 'O');
	TEST_FLAG(v, SHF_GROUP, 'G');
	TEST_FLAG(v, SHF_TLS, 'T');
	TEST_FLAG(v, SHF_COMPRESSED, 'C');

	if (n < size)
		buf[n] = '\0';
	else if (n)
		buf[n - 1] = '\0';

	return buf;

#undef TEST_FLAG
}

const char *elf_dyn_str(uint32_t v)
{
	switch (v)
	{
#define TEST_DT(n) case DT_##n: return #n

		TEST_DT(NULL);
		TEST_DT(NEEDED);
		TEST_DT(PLTRELSZ);
		TEST_DT(PLTGOT);
		TEST_DT(HASH);
		TEST_DT(STRTAB);
		TEST_DT(SYMTAB);
		TEST_DT(RELA);
		TEST_DT(RELASZ);
		TEST_DT(RELAENT);
		TEST_DT(STRSZ);
		TEST_DT(SYMENT);
		TEST_DT(INIT);
		TEST_DT(FINI);
		TEST_DT(SONAME);
		TEST_DT(RPATH);
		TEST_DT(SYMBOLIC);
		TEST_DT(REL);
		TEST_DT(RELSZ);
		TEST_DT(RELENT);
		TEST_DT(PLTREL);
		TEST_DT(DEBUG);
		TEST_DT(TEXTREL);
		TEST_DT(JMPREL);
		TEST_DT(BIND_NOW);
		TEST_DT(INIT_ARRAY);
		TEST_DT(FINI_ARRAY);
		TEST_DT(INIT_ARRAYSZ);
		TEST_DT(FINI_ARRAYSZ);
		TEST_DT(FLAGS);
		TEST_DT(GNU_HASH);
		TEST_DT(VERSYM);
		TEST_DT(RELACOUNT);
		TEST_DT(RELCOUNT);
		TEST_DT(FLAGS_1);
		TEST_DT(VERDEF);
		TEST_DT(VERDEFNUM);
		TEST_DT(VERNEED);
		TEST_DT(VERNEEDNUM);

#undef TEST_DT
	}
	return NULL;
}

const char *elf_dyn_flags_str(char *buf, size_t size, uint32_t v)
{
#define TEST_DF(name) \
do \
{ \
	if (!(v & DF_##name)) \
		break; \
	if (first) \
		first = 0; \
	else \
		strlcat(buf, ", ", size); \
	strlcat(buf, #name, size); \
} while (0)

	int first = 1;
	if (size)
		buf[0] = '\0';
	TEST_DF(ORIGIN);
	TEST_DF(SYMBOLIC);
	TEST_DF(TEXTREL);
	TEST_DF(BIND_NOW);
	TEST_DF(STATIC_TLS);
	return buf;

#undef TEST_DF
}

const char *elf_dyn_flags1_str(char *buf, size_t size, uint32_t v)
{
#define TEST_DF_1(name) \
do \
{ \
	if (!(v & DF_1_##name)) \
		break; \
	if (first) \
		first = 0; \
	else \
		strlcat(buf, ", ", size); \
	strlcat(buf, #name, size); \
} while (0)

	int first = 1;
	if (size)
		buf[0] = '\0';
	TEST_DF_1(NOW);
	TEST_DF_1(GLOBAL);
	TEST_DF_1(GROUP);
	TEST_DF_1(NODELETE);
	TEST_DF_1(LOADFLTR);
	TEST_DF_1(INITFIRST);
	TEST_DF_1(NOOPEN);
	TEST_DF_1(ORIGIN);
	TEST_DF_1(DIRECT);
	TEST_DF_1(TRANS);
	TEST_DF_1(INTERPOSE);
	TEST_DF_1(NODEFLIB);
	TEST_DF_1(NODUMP);
	TEST_DF_1(CONFALT);
	TEST_DF_1(ENDFILTEE);
	TEST_DF_1(DISPRELDNE);
	TEST_DF_1(DIPPRELPND);
	TEST_DF_1(NODIRECT);
	TEST_DF_1(IGNMULDEF);
	TEST_DF_1(NOKSYMS);
	TEST_DF_1(NOHDR);
	TEST_DF_1(EDITED);
	TEST_DF_1(NORELOC);
	TEST_DF_1(SYMINTPOSE);
	TEST_DF_1(GLOBAUDIT);
	TEST_DF_1(SINGLETON);
	TEST_DF_1(STUB);
	TEST_DF_1(PIE);
	TEST_DF_1(KMOD);
	TEST_DF_1(WEAKFILTER);
	TEST_DF_1(NOCOMMON);
	return buf;

#undef TEST_DF_1
}

const char *elf_stb_str(uint8_t v)
{
	switch (v)
	{
#define TEST_STB(n) case STB_##n: return #n

		TEST_STB(LOCAL);
		TEST_STB(GLOBAL);
		TEST_STB(WEAK);

#undef TEST_STB
	}
	return NULL;
}

const char *elf_stt_str(uint8_t v)
{
	switch (v)
	{
#define TEST_STT(n) case STT_##n: return #n

		TEST_STT(NOTYPE);
		TEST_STT(OBJECT);
		TEST_STT(FUNC);
		TEST_STT(SECTION);
		TEST_STT(FILE);

#undef TEST_STT
	}
	return NULL;
}

const char *elf_stv_str(uint8_t v)
{
	switch (v)
	{
#define TEST_STV(n) case STV_##n: return #n

		TEST_STV(DEFAULT);
		TEST_STV(INTERNAL);
		TEST_STV(HIDDEN);
		TEST_STV(PROTECTED);

#undef TEST_STV
	}
	return NULL;
}

const char *elf_r_str(uint32_t v)
{
	switch (v)
	{
#define TEST_R(n) case EM_##n: return #n;

		TEST_R(386);
		TEST_R(X86_64);
		TEST_R(ARM);
		TEST_R(AARCH64);
		TEST_R(RISCV);

#undef TEST_R
	}
	return NULL;
}

const char *elf_r_386_str(uint32_t v)
{
	switch (v)
	{
#define TEST_R(n) case R_386_##n: return #n

		TEST_R(NONE);
		TEST_R(32);
		TEST_R(PC32);
		TEST_R(GOT32);
		TEST_R(PLT32);
		TEST_R(COPY);
		TEST_R(GLOB_DAT);
		TEST_R(JMP_SLOT);
		TEST_R(RELATIVE);
		TEST_R(GOTOFF);
		TEST_R(GOTPC);
		TEST_R(32PLT);
		TEST_R(TLS_GD_PLT);
		TEST_R(TLS_LDM_PLT);
		TEST_R(TLS_TPOFF);
		TEST_R(TLS_IE);
		TEST_R(TLS_GOTIE);
		TEST_R(TLS_LE);
		TEST_R(TLS_GD);
		TEST_R(TLS_LDM);
		TEST_R(16);
		TEST_R(PC16);
		TEST_R(8);
		TEST_R(PC8);
		TEST_R(TLS_LDO_32);
		TEST_R(TLS_DTPMOD32);
		TEST_R(TLS_DTPOFF32);
		TEST_R(TLS_TPOFF32);
		TEST_R(SIZE32);

#undef TEST_R
	}
	return NULL;
}

const char *elf_r_x86_64_str(uint32_t v)
{
	switch (v)
	{
#define TEST_R(n) case R_X86_64_##n: return #n

		TEST_R(NONE);
		TEST_R(64);
		TEST_R(PC32);
		TEST_R(GOT32);
		TEST_R(PLT32);
		TEST_R(COPY);
		TEST_R(GLOB_DAT);
		TEST_R(JUMP_SLOT);
		TEST_R(RELATIVE);
		TEST_R(GOTPCREL);
		TEST_R(32);
		TEST_R(32S);
		TEST_R(16);
		TEST_R(PC16);
		TEST_R(8);
		TEST_R(PC8);
		TEST_R(DTPMOD64);
		TEST_R(DTPOFF64);
		TEST_R(TPOFF64);

#undef TEST_R
	}
	return NULL;
}

const char *elf_r_arm_str(uint32_t v)
{
	switch (v)
	{
#define TEST_R(n) case R_ARM_##n: return #n

		TEST_R(NONE);
		TEST_R(PC24);
		TEST_R(ABS32);
		TEST_R(REL32);
		TEST_R(LDR_PC_G0);
		TEST_R(ABS16);
		TEST_R(ABS12);
		TEST_R(THM_ABS5);
		TEST_R(ABS8);
		TEST_R(SBREL32);
		TEST_R(THM_CALL);
		TEST_R(THM_PC8);
		TEST_R(BREL_ADJ);
		TEST_R(TLS_DESC);
		TEST_R(THM_SWI8);
		TEST_R(XPC25);
		TEST_R(THM_XPC22);
		TEST_R(TLS_DTPMOD32);
		TEST_R(TLS_DTPOFF32);
		TEST_R(TLS_TPOFF32);
		TEST_R(COPY);
		TEST_R(GLOB_DAT);
		TEST_R(JUMP_SLOT);
		TEST_R(RELATIVE);

#undef TEST_R
	}
	return NULL;
}

const char *elf_r_aarch64_str(uint32_t v)
{
	switch (v)
	{
#define TEST_R(n) case R_AARCH64_##n: return #n

		TEST_R(NONE);
		TEST_R(ABS64);
		TEST_R(COPY);
		TEST_R(GLOB_DAT);
		TEST_R(JUMP_SLOT);
		TEST_R(RELATIVE);
		TEST_R(TLS_DTPMOD);
		TEST_R(TLS_DTPREL);
		TEST_R(TLS_TPREL);
		TEST_R(TLSDESC);

#undef TEST_R
	}
	return NULL;
}

const char *elf_r_riscv_str(uint32_t v)
{
	switch (v)
	{
#define TEST_R(n) case R_RISCV_##n: return #n

		TEST_R(NONE);
		TEST_R(32);
		TEST_R(64);
		TEST_R(RELATIVE);
		TEST_R(COPY);
		TEST_R(JUMP_SLOT);
		TEST_R(TLS_DTPMOD32);
		TEST_R(TLS_DTPMOD64);
		TEST_R(TLS_DTPREL32);
		TEST_R(TLS_DTPREL64);
		TEST_R(TLS_TPREL32);
		TEST_R(TLS_TPREL64);
		TEST_R(TLS_TLSDESC);

#undef TEST_R
	}
	return NULL;
}

const char *elf_ver_flags_str(char *buf, size_t size, uint32_t v)
{
#define TEST_VER_FLAG(name) \
do \
{ \
	if (!(v & VER_FLG_##name)) \
		break; \
	if (first) \
		first = 0; \
	else \
		strlcat(buf, ", ", size); \
	strlcat(buf, #name, size); \
} while (0)

	int first = 1;
	if (size)
		buf[0] = '\0';
	TEST_VER_FLAG(BASE);
	TEST_VER_FLAG(WEAK);
	return buf;

#undef TEST_VER_FLAG
}
