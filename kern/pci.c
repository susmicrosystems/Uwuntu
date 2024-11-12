#define ENABLE_TRACE

#include <queue.h>
#include <file.h>
#if WITH_ACPI
#include <acpi.h>
#endif
#include <std.h>
#include <uio.h>
#include <pci.h>
#include <sma.h>
#include <vfs.h>
#if WITH_FDT
#include <fdt.h>
#endif

static struct spinlock devices_lock = SPINLOCK_INITIALIZER(); /* XXX rwlock */
static TAILQ_HEAD(, pci_device) devices = TAILQ_HEAD_INITIALIZER(devices);

static struct sma pci_device_sma;
static struct sma pci_msix_sma;

static struct file_op sysfs_pci_fop;
static struct file_op sysfs_pci_dev_fop;

void pci_init_sma(void)
{
	sma_init(&pci_device_sma, sizeof(struct pci_device), NULL, NULL, "pci_device");
	sma_init(&pci_msix_sma, sizeof(struct pci_msix), NULL, NULL, "pci_msix");
}

static int setup_msix(struct pci_device *device)
{
	uint8_t ptr;
	int ret = pci_find_capability(device, PCI_CAP_MSIX, &ptr);
	if (ret)
		return ret;
	device->msix = sma_alloc(&pci_msix_sma, M_ZERO);
	if (!device->msix)
		return -ENOMEM;
	union pci_cap_msix cap;
	cap.v0 = pci_dev_read(device, ptr + 0x0);
	cap.v4 = pci_dev_read(device, ptr + 0x4);
	cap.v8 = pci_dev_read(device, ptr + 0x8);
	uint8_t bir = cap.table_offset & 0x7;
	if (bir > 5)
	{
		printf("pci: invalid bir: %" PRIu8 "\n", bir);
		return -EINVAL;
	}
	device->msix->size = (cap.control & 0x7FF) + 1;
	ret = pci_map(&device->msix->map, (&device->header0.bar0)[bir],
	              device->msix->size * 16, cap.table_offset & ~0x7);
	if (ret)
	{
		printf("pci: failed to map msix\n");
		sma_free(&pci_msix_sma, device->msix);
		device->msix = NULL;
		return ret;
	}
	/* mask all the interrupts */
	for (uint16_t i = 0; i < device->msix->size; ++i)
		pci_wu32(&device->msix->map, i * 16 + 12, 1);
	device->header.command |= (1 << 10);
	pci_dev_write(device, 0x4, device->header.v4);
	pci_dev_write(device, ptr + 0x0, cap.v0 | (1 << 31));
	return 0;
}

static void checkdev(uint8_t bus, uint8_t slot, uint8_t func)
{
	struct pci_device *device = sma_alloc(&pci_device_sma, M_ZERO);
	if (!device)
		panic("failed to allocate pci device\n");
	device->bus = bus;
	device->slot = slot;
	device->func = func;
	device->base = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8);
	uintptr_t ecam_poff;
#if WITH_ACPI
	int ret = acpi_get_ecam_addr(device, &ecam_poff);
#elif WITH_FDT
	int ret = fdt_get_ecam_addr(device, &ecam_poff);
#endif
#if !defined(__i386__) && !defined(__amd64__)
	if (ret)
	{
		sma_free(&pci_device_sma, device);
		return;
	}
#endif
	if (!ret)
	{
		pm_init_page(&device->ecam_page, ecam_poff);
		device->ecam = vm_map(&device->ecam_page, PAGE_SIZE,
		                      VM_PROT_RW | VM_MMIO);
		if (!device->ecam)
		{
			TRACE("pci: failed to vmap ecam");
			sma_free(&pci_device_sma, device);
			return;
		}
	}
	device->header.v0 = pci_dev_read(device, 0x0);
	if (device->header.vendor == 0xFFFF)
	{
		sma_free(&pci_device_sma, device);
		return;
	}
	device->header.v4 = pci_dev_read(device, 0x4);
	device->header.v8 = pci_dev_read(device, 0x8);
	device->header.vC = pci_dev_read(device, 0xC);
	device->group = 0;
	device->bus = bus;
	device->slot = slot;
	device->func = func;
	switch (device->header.headertype & 0x7F)
	{
		case 0:
		{
			struct pci_header0 *header0 = &device->header0;
			header0->v0 = pci_dev_read(device, 0x10);
			header0->v1 = pci_dev_read(device, 0x14);
			header0->v2 = pci_dev_read(device, 0x18);
			header0->v3 = pci_dev_read(device, 0x1C);
			header0->v4 = pci_dev_read(device, 0x20);
			header0->v5 = pci_dev_read(device, 0x24);
			header0->v6 = pci_dev_read(device, 0x28);
			header0->v7 = pci_dev_read(device, 0x2C);
			header0->v8 = pci_dev_read(device, 0x30);
			header0->v9 = pci_dev_read(device, 0x34);
			header0->vA = pci_dev_read(device, 0x38);
			header0->vB = pci_dev_read(device, 0x3C);
			break;
		}
		case 1:
		{
			struct pci_header1 *header1 = &device->header1;
			header1->v0 = pci_dev_read(device, 0x10);
			header1->v1 = pci_dev_read(device, 0x14);
			header1->v2 = pci_dev_read(device, 0x18);
			header1->v3 = pci_dev_read(device, 0x1C);
			header1->v4 = pci_dev_read(device, 0x20);
			header1->v5 = pci_dev_read(device, 0x24);
			header1->v6 = pci_dev_read(device, 0x28);
			header1->v7 = pci_dev_read(device, 0x2C);
			header1->v8 = pci_dev_read(device, 0x30);
			header1->v9 = pci_dev_read(device, 0x34);
			header1->vA = pci_dev_read(device, 0x38);
			header1->vB = pci_dev_read(device, 0x3C);
			header1->vC = pci_dev_read(device, 0x40);
			header1->vD = pci_dev_read(device, 0x44);
			header1->vE = pci_dev_read(device, 0x48);
			header1->vF = pci_dev_read(device, 0x4C);
			break;
		}
	}
	pci_enable_mmio(device);
	pci_enable_pio(device);
	char node_name[64];
	snprintf(node_name, sizeof(node_name), "pci/%04x:%02x:%02x.%1x",
	         device->group, device->bus, device->slot, device->func);
	ret = sysfs_mknode(node_name, 0, 0, 0400, &sysfs_pci_dev_fop,
	                   &device->sysfs_node);
	if (ret)
		printf("pci: failed to create sysfs node: %s\n", strerror(ret));
	else
		device->sysfs_node->userdata = device;
	setup_msix(device);
	spinlock_lock(&devices_lock);
	TAILQ_INSERT_TAIL(&devices, device, chain);
	spinlock_unlock(&devices_lock);
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device)
{
	struct pci_device *dev;
	spinlock_lock(&devices_lock);
	TAILQ_FOREACH(dev, &devices, chain)
	{
		if (dev->header.vendor == vendor
		 && dev->header.device == device)
			break;
	}
	spinlock_unlock(&devices_lock);
	return dev;
}

void pci_probe(uint16_t vendor, uint16_t device, pci_probe_t probe,
               void *userdata)
{
	struct pci_device *dev;
	spinlock_lock(&devices_lock);
	TAILQ_FOREACH(dev, &devices, chain)
	{
		if (dev->header.vendor == vendor
		 && dev->header.device == device)
		{
			if (probe(dev, userdata))
				break;
		}
	}
	spinlock_unlock(&devices_lock);
}

void pci_init(void)
{
	for (uint32_t bus = 0; bus < 256; ++bus)
	{
		for (uint32_t slot = 0; slot < 32; ++slot)
		{
			for (uint32_t func = 0; func < 8; ++func)
			{
				checkdev(bus, slot, func);
			}
		}
	}
	if (sysfs_mknode("pci/list", 0, 0, 0444, &sysfs_pci_fop, NULL))
		panic("failed to create /sys/pci/list\n");
}

static void pci_enable_cmd(struct pci_device *device, uint16_t cmd)
{
	if (device->header.command & cmd)
		return;
	device->header.command |= cmd;
	pci_dev_write(device, 0x4, device->header.v4);
}

static void pci_disable_cmd(struct pci_device *device, uint16_t cmd)
{
	if (!(device->header.command & cmd))
		return;
	device->header.command &= ~cmd;
	pci_dev_write(device, 0x4, device->header.v4);
}

void pci_enable_bus_mastering(struct pci_device *device)
{
	pci_enable_cmd(device, 1 << 2);
}

void pci_enable_mmio(struct pci_device *device)
{
	pci_enable_cmd(device, 1 << 1);
}

void pci_enable_pio(struct pci_device *device)
{
	pci_enable_cmd(device, 1 << 0);
}

int pci_find_capability(struct pci_device *device, uint8_t id, uint8_t *ptr)
{
	if (!(device->header.status & (1 << 4)))
		return -EXDEV;
	*ptr = device->header0.capabilities & ~0x3;
	while (*ptr)
	{
		uint32_t value = pci_dev_read(device, *ptr);
		if ((value & 0xFF) == id)
			return 0;
		*ptr = (value >> 8) & 0xFF;
	}
	return -EXDEV;
}

int pci_enable_msi(struct pci_device *device, uint64_t addr, uint32_t data)
{
	uint8_t ptr;
	int ret = pci_find_capability(device, PCI_CAP_MSI, &ptr);
	if (ret)
		return ret;
	union pci_cap_msi cap;
	cap.v0 = pci_dev_read(device, ptr + 0x0);
	cap.v4 = pci_dev_read(device, ptr + 0x4);
	cap.v8 = pci_dev_read(device, ptr + 0x8);
	cap.vC = pci_dev_read(device, ptr + 0xC);
	pci_enable_cmd(device, 1 << 10);
	pci_dev_write(device, ptr + 0x0, cap.v0 | (1 << 16));
	pci_dev_write(device, ptr + 0x4, addr);
	pci_dev_write(device, ptr + 0x8, addr >> 32);
	pci_dev_write(device, ptr + 0xC, data);
	return 0;
}

int pci_disable_msi(struct pci_device *device)
{
	pci_disable_cmd(device, 1 << 10);
	return 0;
}

int pci_enable_msix(struct pci_device *device, uint64_t addr, uint32_t data,
                    uint16_t *vector)
{
	if (!device->msix)
		return -EXDEV;
	uint16_t id = UINT16_MAX;
	for (uint16_t i = 0; i < device->msix->size; ++i)
	{
		if (pci_ru32(&device->msix->map, i * 16 + 12) & 1)
		{
			id = i;
			break;
		}
	}
	if (id == UINT16_MAX)
	{
		printf("pci: no more msix vector available\n");
		return -EINVAL;
	}
	pci_wu32(&device->msix->map, id * 16 + 0x0, addr);
	pci_wu32(&device->msix->map, id * 16 + 0x4, addr >> 32);
	pci_wu32(&device->msix->map, id * 16 + 0x8, data);
	pci_wu32(&device->msix->map, id * 16 + 0xC, 0);
	*vector = id;
	return 0;
}

int pci_disable_msix(struct pci_device *device, uint16_t vector)
{
	assert(device->msix, "disabling unexisting msix\n");
	assert(vector < device->msix->size, "invalid msix vector\n");
	assert(!(pci_ru32(&device->msix->map, vector * 16 + 12) & 1), "disabling masked msix vector\n");
	pci_wu32(&device->msix->map, vector * 16 + 12, 1);
	return 0;
}

int pci_map(struct pci_map *map, size_t addr, size_t size, size_t offset)
{
	map->addr = addr;
	map->size = size;
	map->offset = offset;
	if (addr & 1)
	{
#if !defined(__i386__) && !defined(__amd64__)
		map->data = NULL;
		printf("pci: PIO not supported\n");
		return -EINVAL;
#endif
		map->base = (map->addr & ~3) + map->offset;
		return 0;
	}
	uint32_t flags = VM_PROT_RW;
	if (!(addr & 0x8))
		flags |= VM_MMIO;
	size_t base = (addr & ~0xF) + offset;
	size_t pad = base & PAGE_MASK;
	size_t map_size = (size + pad + PAGE_MASK) & ~PAGE_MASK;
	pm_init_page(&map->page, base / PAGE_SIZE);
	map->data = vm_map(&map->page, map_size, flags);
	if (!map->data)
		return -ENOMEM;
	map->data += pad;
	return 0;
}

int pci_map_bar(struct pci_map *map, struct pci_device *device, size_t bar,
                size_t size, size_t offset)
{
	switch (device->header.headertype & 0x7F)
	{
		case 0:
		{
			if (bar > 5)
			{
				TRACE("invalid bar id: %zu", bar);
				return -EINVAL;
			}
			size_t addr = (&device->header0.bar0)[bar];
			if (addr & 0x4)
			{
				if (bar > 4)
				{
					TRACE("invalid 64bit bar id: %zu", bar);
					return -EINVAL;
				}
				uint32_t next_bar = (&device->header0.bar0)[bar + 1];
#if __SIZE_WIDTH__ == 32
				if (next_bar)
				{
					TRACE("64bit on 32bit system");
					return -EINVAL;
				}
#else
				addr |= (uint64_t)next_bar << 32;
#endif
			}
			return pci_map(map, addr, size, offset);
		}
		case 1:
			if (bar > 1)
			{
				TRACE("invalid bar id: %zu", bar);
				return -EINVAL;
			}
			size_t addr = (&device->header1.bar0)[bar];
			if (addr & 0x4)
			{
#if __SIZE_WIDTH__ == 32
				TRACE("64bit on 32bit system");
				return -EINVAL;
#endif
				if (bar > 0)
				{
					TRACE("invalid 64bit bar id: %zu", bar);
					return -EINVAL;
				}
				uint32_t next_bar = (&device->header1.bar0)[bar + 1];
#if __SIZE_WIDTH__ == 32
				if (next_bar)
				{
					TRACE("64bit on 32bit system");
					return -EINVAL;
				}
#else
				addr |= (uint64_t)next_bar << 32;
#endif
			}
			return pci_map(map, addr, size, offset);
		default:
			TRACE("unknown pci header type %x", (unsigned)(device->header.headertype & 0x7F));
			return -EINVAL;
	}
}

void pci_unmap(struct pci_map *map)
{
	if ((map->addr & 1) || !map->data)
		return;
	uint32_t base = (map->addr & ~0xF) + map->offset;
	uint32_t pad = base & PAGE_MASK;
	vm_unmap((void*)((uintptr_t)map->data & ~PAGE_MASK),
	         (map->size + pad + PAGE_MASK) & ~PAGE_MASK);
	map->data = NULL;
}

static const struct
{
	uint16_t vendor;
	uint16_t device;
	const char *name;
} devices_ref[] =
{
	{0x10DE, 0x0BEE, "NVidia GF116 HD Audio Controller"},
	{0x10DE, 0x1251, "NVidia GF116M"},
	{0x10EC, 0x8029, "RTL8029(AS)"},
	{0x10EC, 0x8139, "RTL8139 Gigabit Ethernet Controller"},
	{0x10EC, 0x8168, "RTL8168 Gigabit Ethernet Controller"},
	{0x1234, 0x1111, "QEMU Virtual Video Controller"},
	{0x168C, 0x002B, "AR9285 Wireless Network Adapter"},
	{0x1AF4, 0x1000, "Virtio network device"},
	{0x1AF4, 0x1001, "Virtio block device"},
	{0x1AF4, 0x1002, "Virtio memory balloon"},
	{0x1AF4, 0x1003, "Virtio console"},
	{0x1AF4, 0x1004, "Virtio SCSI"},
	{0x1AF4, 0x1005, "Virtio RNG"},
	{0x1AF4, 0x1009, "Virtio filesystem"},
	{0x1AF4, 0x1001, "Virtio 1.0 network device"},
	{0x1AF4, 0x1042, "Virtio 1.0 block device"},
	{0x1AF4, 0x1043, "Virtio 1.0 console"},
	{0x1AF4, 0x1044, "Virtio 1.0 RNG"},
	{0x1AF4, 0x1045, "Virtio 1.0 memory balloon"},
	{0x1AF4, 0x1048, "Virtio 1.0 SCSI"},
	{0x1AF4, 0x1049, "Virtio 1.0 filesystem"},
	{0x1AF4, 0x1050, "Virtio 1.0 GPU"},
	{0x1AF4, 0x1052, "Virtio 1.0 input"},
	{0x1AF4, 0x1053, "Virtio 1.0 socket"},
	{0x1AF4, 0x105A, "Virtio filesystem"},
	{0x1B36, 0x0008, "QEMU PCIe Host bridge"},
	{0x1B36, 0x000D, "QEMU XHCI Host Controller"},
	{0x1B73, 0x1000, "FL1000G USB 3.0 Host Controller"},
	{0x8086, 0x0101, "Intel 2nd Gen PCIe Root Port"},
	{0x8086, 0x0104, "Intel 2nd Gen DRAM Controller"},
	{0x8086, 0x0697, "Comet Lake ISA Bridge"},
	{0x8086, 0x06A3, "Comet Lake PCH SMBus Controller"},
	{0x8086, 0x06A4, "Comet Lake PCH SPI Controller"},
	{0x8086, 0x06AC, "Comet Lake PCI Express Root Port #21"},
	{0x8086, 0x06B0, "Comet Lake PCI Express Root Port #9"},
	{0x8086, 0x06BD, "Comet Lake PCIe Port #6"},
	{0x8086, 0x06BF, "Comet Lake PCIe Port #8"},
	{0x8086, 0x06C0, "Comet Lake PCI Express Root Port #17"},
	{0x8086, 0x06D2, "Comet Lake SATA AHCI Controller"},
	{0x8086, 0x06E0, "Comet Lake HECI Controller"},
	{0x8086, 0x06ED, "Comet Lake USB 3.1 xHCI Host Controller"},
	{0x8086, 0x06EF, "Comet Lake PCH Shared SRAM"},
	{0x8086, 0x06F9, "Comet Lake PCH Thermal Controller"},
	{0x8086, 0x100E, "82540EM Gigabit Ethernet Controller"},
	{0x8086, 0x10D3, "82574L Gigabit Ethernet Controller"},
	{0x8086, 0x1237, "440FX - 82441FX PMC"},
	{0x8086, 0x1536, "I210 Gigabit Fiber Network Connection"},
	{0x8086, 0x1572, "Ethernet Controller X710 for 10GbE SFP+"},
	{0x8086, 0x15F2, "Ethernet Controller I225-LM"},
	{0x8086, 0x1901, "6th-10th Gen Core Processor PCIe Controller (x16)"},
	{0x8086, 0x1905, "Xeon E3-1200 v5/E3-1500 v5/6th Gen Core Processor PCIe Controller (x8)"},
	{0x8086, 0x1909, "Xeon E3-1200 v5/E3-1500 v5/6th Gen Core Processor PCIe Controller (x4)"},
	{0x8086, 0x1911, "Xeon E3-1200 v5/v6 / E3-1500 v5 / 6th/7th/8th Gen Core Processor Gaussian Mixture Model"},
	{0x8086, 0x1C03, "6 Series/C200 Mobile SATA AHCI Controller"},
	{0x8086, 0x1C10, "6 Series/C200 PCIe Root Port 1"},
	{0x8086, 0x1C12, "6 Series/C200 PCIe Root Port 2"},
	{0x8086, 0x1C16, "6 Series/C200 PCIe Root Port 4"},
	{0x8086, 0x1C1A, "6 Series/C200 PCIe Root Port 6"},
	{0x8086, 0x1C20, "6 Series/C200 HD Audio Controller"},
	{0x8086, 0x1C22, "6 Series/C200 SMBus Controller"},
	{0x8086, 0x1C26, "6 Series/C200 USB EHC #1"},
	{0x8086, 0x1C2D, "6 Series/C200 USB EHC #2"},
	{0x8086, 0x1C3A, "6 Series/C200 MEI Controller #1"},
	{0x8086, 0x1C49, "HM65 Express Chipset LPC Controller"},
	{0x8086, 0x2918, "82801IB (ICH9) LPC Interface Controller"},
	{0x8086, 0x2922, "82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA Controller"},
	{0x8086, 0x2930, "82801I (ICH9 Family) SMBus Controller"},
	{0x8086, 0x2932, "82801I (ICH9 Family) Thermal Subsystem"},
	{0x8086, 0x2934, "82801I (ICH9 Family) USB UHCI Controller #1"},
	{0x8086, 0x2935, "82801I (ICH9 Family) USB UHCI Controller #2"},
	{0x8086, 0x2936, "82801I (ICH9 Family) USB UHCI Controller #3"},
	{0x8086, 0x2937, "82801I (ICH9 Family) USB UHCI Controller #4"},
	{0x8086, 0x2938, "82801I (ICH9 Family) USB UHCI Controller #5"},
	{0x8086, 0x2939, "82801I (ICH9 Family) USB UHCI Controller #6"},
	{0x8086, 0x293A, "82801I (ICH9 Family) USB2 EHCI Controller #1"},
	{0x8086, 0x293C, "82801I (ICH9 Family) USB2 EHCI Controller #2"},
	{0x8086, 0x293E, "82801I (ICH9 Family) HD Audio Controller"},
	{0x8086, 0x29C0, "82G33/G31/P35/P31 Express DRAM Controller"},
	{0x8086, 0x2415, "82801AA AC'97 Audio Controller"},
	{0x8086, 0x24CD, "82801DB/DBM USB2 EHCI Controller"},
	{0x8086, 0x2668, "82801FB HDA Controller"},
	{0x8086, 0x4511, "Elkhart Lake Gaussian and Neural Accelerator"},
	{0x8086, 0x4571, "Elkhart Lake [UHD Graphics Gen11 32EU]"},
	{0x8086, 0x452A, "Elkhart Lake IBECC"},
	{0x8086, 0x4B00, "Elkhart Lake eSPI Controller"},
	{0x8086, 0x4B23, "Elkhart Lake High Density Audio bus interface"},
	{0x8086, 0x4B24, "Elkhart Lake SPI (Flash) Controller"},
	{0x8086, 0x4B38, "Elkhart Lake PCH PCI Express Root Port #0"},
	{0x8086, 0x4B3C, "Elkhart Lake PCIe Root Port #4"},
	{0x8086, 0x4B3E, "Elkhart Lake PCH PCI Express Root Port #6"},
	{0x8086, 0x4B47, "Elkhart Lake Atom SD Controller"},
	{0x8086, 0x4B58, "Elkhart Lake High Density Audio bus interface"},
	{0x8086, 0x4B63, "Elkhart Lake SATA AHCI"},
	{0x8086, 0x4B70, "Elkhart Lake Management Engine Interface"},
	{0x8086, 0x4B7D, "Elkhart Lake Gaussian and Neural Accelerator"},
	{0x8086, 0x4B7F, "Elkhart Lake PMC SRAM"},
	{0x8086, 0x7000, "82371SB PIIX3 ISA"},
	{0x8086, 0x7010, "82371SB PIIX3 IDE"},
	{0x8086, 0x7110, "82371AB/EB/MB PIIX4 ISA"},
	{0x8086, 0x7111, "82371AB/EB/MB PIIX4 IDE"},
	{0x8086, 0x7112, "82371AB/EB/MB PIIX4 USB"},
	{0x8086, 0x7113, "82371AB/EB/MB PIIX4 ACPI"},
	{0x8086, 0x7020, "82371SB PIIX3 USB"},
	{0x8086, 0x9B63, "10th Gen Core Processor Host Bridge/DRAM Registers"},
	{0x8086, 0x9BC8, "CometLake-S GT2 [UHD Graphics 630]"},
};

static const char *dev_name(const struct pci_device *device)
{
	for (size_t i = 0; i < sizeof(devices_ref) / sizeof(*devices_ref); ++i)
	{
		if (devices_ref[i].vendor == device->header.vendor
		 && devices_ref[i].device == device->header.device)
			return devices_ref[i].name;
	}
	return "";
}

static void print_capabilities(struct uio *uio, struct pci_device *device,
                               uint8_t ptr)
{
	if (!(device->header.status & (1 << 4)))
		return;
	uprintf(uio, "capabilities:");
	while (ptr)
	{
		uint32_t value = pci_dev_read(device, ptr);
		uprintf(uio, " %02" PRIx32, value & 0xFF);
		ptr = (value >> 8) & 0xFF;
	}
	uprintf(uio, "\n");
}

static int pci_dev_print(struct uio *uio, struct pci_device *device)
{
	const char *name = dev_name(device);
	union pci_header *header = &device->header;
	uprintf(uio, "address   : %02" PRIx8 ":%02" PRIx8 ".%01" PRIx8 "\n"
	             "name      : %s\n"
	             "vendor    : 0x%04" PRIx16 "\n"
	             "device    : 0x%04" PRIx16 "\n"
	             "command   : 0x%04" PRIx16 "\n"
	             "status    : 0x%04" PRIx16 "\n"
	             "revision  : 0x%02" PRIx8 "\n"
	             "progif    : 0x%02" PRIx8 "\n"
	             "subclass  : 0x%02" PRIx8 "\n"
	             "class     : 0x%02" PRIx8 "\n"
	             "cacheline : 0x%02" PRIx8 "\n"
	             "latency   : 0x%02" PRIx8 "\n"
	             "headertype: 0x%02" PRIx8 "\n"
	             "bist      : 0x%02" PRIx8 "\n",
	             device->bus,
	             device->slot,
	             device->func,
	             name,
	             header->vendor,
	             header->device,
	             header->command,
	             header->status,
	             header->revision,
	             header->progif,
	             header->subclass,
	             header->class,
	             header->cacheline,
	             header->latency,
	             header->headertype,
	             header->bist);
	switch (header->headertype & 0x7F)
	{
		case 0:
		{
			struct pci_header0 *header0 = &device->header0;
			uprintf(uio, "bar0      : 0x%08" PRIx32 "\n"
			             "bar1      : 0x%08" PRIx32 "\n"
			             "bar2      : 0x%08" PRIx32 "\n"
			             "bar3      : 0x%08" PRIx32 "\n"
			             "bar4      : 0x%08" PRIx32 "\n"
			             "bar5      : 0x%08" PRIx32 "\n"
			             "cardbus   : 0x%08" PRIx32 "\n"
			             "subsystem : 0x%04" PRIx16 "\n"
			             "subvendor : 0x%04" PRIx16 "\n"
			             "ROM bar   : 0x%08" PRIx32 "\n"
			             "cap ptr   : 0x%02" PRIx8 "\n"
			             "max lat   : 0x%02" PRIx8 "\n"
			             "min grant : 0x%02" PRIx8 "\n"
			             "int pin   : 0x%02" PRIx8 "\n"
			             "int line  : 0x%02" PRIx8 "\n",
			             header0->bar0,
			             header0->bar1,
			             header0->bar2,
			             header0->bar3,
			             header0->bar4,
			             header0->bar5,
			             header0->cis_pointer,
			             header0->subsystem_id,
			             header0->subsystem_vendor_id,
			             header0->rom_bar,
			             header0->capabilities,
			             header0->max_latency,
			             header0->min_grant,
			             header0->int_pin,
			             header0->int_line);
			print_capabilities(uio, device, header0->capabilities & ~3);
			break;
		}
		case 1:
		{
			struct pci_header1 *header1 = &device->header1;
			uprintf(uio, "bar0      : 0x%08" PRIx32 "\n"
			             "bar1      : 0x%08" PRIx32 "\n"
			             "pri bus   : 0x%02" PRIx8 "\n"
			             "sec bus   : 0x%02" PRIx8 "\n"
			             "sub bus   : 0x%02" PRIx8 "\n"
			             "sec lat   : 0x%02" PRIx8 "\n"
			             "io base   : 0x%02" PRIx8 "\n"
			             "io limit  : 0x%02" PRIx8 "\n"
			             "sec status: 0x%04" PRIx16 "\n"
			             "mem base  : 0x%04" PRIx16 "\n"
			             "mem limit : 0x%04" PRIx16 "\n"
			             "pre basel : 0x%04" PRIx16 "\n"
			             "pre limitl: 0x%04" PRIx16 "\n"
			             "pre baseh : 0x%08" PRIx32 "\n"
			             "pre limith: 0x%08" PRIx32 "\n"
			             "io baseh  : 0x%04" PRIx16 "\n"
			             "io limith : 0x%04" PRIx16 "\n"
			             "cap ptr   : 0x%02" PRIx8 "\n"
			             "exp rom   : 0x%08" PRIx32 "\n"
			             "int line  : 0x%02" PRIx8 "\n"
			             "int pin   : 0x%02" PRIx8 "\n"
			             "bridge ctl: 0x%04" PRIx16 "\n",
			             header1->bar0,
			             header1->bar1,
			             header1->primary_bus,
			             header1->secondary_bus,
			             header1->subordinate_bus,
			             header1->secondary_latency,
			             header1->io_base,
			             header1->io_limit,
			             header1->secondary_status,
			             header1->memory_base,
			             header1->memory_limit,
			             header1->prefetchable_base,
			             header1->prefetchable_limit,
			             header1->prefetchable_base_upper,
			             header1->prefetchable_limit_upper,
			             header1->io_base_upper,
			             header1->io_limit_upper,
			             header1->capability_pointer,
			             header1->expansion_rom_addr,
			             header1->int_line,
			             header1->int_pin,
			             header1->bridge_control);
			print_capabilities(uio, device, header1->capability_pointer & ~3);
			break;
		}
	}
	return 0;
}

static int pci_print(struct uio *uio)
{
	struct pci_device *device;
	spinlock_lock(&devices_lock);
	TAILQ_FOREACH(device, &devices, chain)
	{
		uprintf(uio, "%04" PRIx16 ":%02" PRIx8 ":%02" PRIx8 ".%01" PRIx8 " %04" PRIx16 ":%04" PRIx16 " %s\n",
		        device->group, device->bus, device->slot, device->func,
		        device->header.vendor, device->header.device,
		        dev_name(device));
	}
	spinlock_unlock(&devices_lock);
	return 0;
}

static ssize_t sysfs_pci_read(struct file *file, struct uio *uio)
{
	(void)file;
	size_t count = uio->count;
	off_t off = uio->off;
	int ret = pci_print(uio);
	if (ret < 0)
		return ret;
	uio->off = off + count - uio->count;
	return count - uio->count;
}

static struct file_op sysfs_pci_fop =
{
	.read = sysfs_pci_read,
};

static int sysfs_pci_dev_open(struct file *file, struct node *node)
{
	file->userdata = node->userdata;
	return 0;
}

static ssize_t sysfs_pci_dev_read(struct file *file, struct uio *uio)
{
	size_t count = uio->count;
	off_t off = uio->off;
	int ret = pci_dev_print(uio, file->userdata);
	if (ret < 0)
		return ret;
	uio->off = off + count - uio->count;
	return count - uio->count;
}

static struct file_op sysfs_pci_dev_fop =
{
	.open = sysfs_pci_dev_open,
	.read = sysfs_pci_dev_read,
};
