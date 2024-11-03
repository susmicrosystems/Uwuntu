#include <kmod.h>
#include <pci.h>

int init_pci_8139(struct pci_device *device, void *userdata);
int init_pci_8169(struct pci_device *device, void *userdata);

int init(void)
{
	pci_probe(0x10EC, 0x8139, init_pci_8139, NULL);
	pci_probe(0x10EC, 0x8168, init_pci_8169, NULL);
	return 0;
}

void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "rtl",
	.init = init,
	.fini = fini,
};
