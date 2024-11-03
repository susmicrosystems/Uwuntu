#include <errno.h>
#include <kmod.h>
#include <pci.h>

int uhci_init(struct pci_device *device, void *userdata);

int init(void)
{
	pci_probe(0x8086, 0x2934, uhci_init, NULL);
	return 0;
}

void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "usb",
	.init = init,
	.fini = fini,
};
