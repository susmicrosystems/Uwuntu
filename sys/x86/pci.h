#ifndef X86_PCI_H
#define X86_PCI_H

#include "arch/x86/asm.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static inline uint32_t pci_read(uint32_t addr)
{
	outl(PCI_ADDR, addr);
	return inl(PCI_DATA);
}

static inline void pci_write(uint32_t addr, uint32_t data)
{
	outl(PCI_ADDR, addr);
	outl(PCI_DATA, data);
}

#endif
