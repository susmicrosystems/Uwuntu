#include <errno.h>
#include <std.h>
#include <pci.h>

#define REG_USBCMD    0x00 /* USB command */
#define REG_USBSTS    0x02 /* USB status */
#define REG_USBINTR   0x04 /* USB interrupt enable */
#define REG_FRNUM     0x06 /* frame number */
#define REG_FRBASEADD 0x08 /* frame list base */
#define REG_SOFMOD    0x0C /* start of frame modify */
#define REG_PORTSC1   0x10 /* port 1 status / control */
#define REG_PORTSC2   0x12 /* port 2 status / control */

#define CMD_RS      (1 << 0) /* run / stop */
#define CMD_HCRESET (1 << 1) /* host controller reset */
#define CMD_GRESET  (1 << 2) /* global reset */
#define CMD_EGSM    (1 << 3) /* enter global suspend mode */
#define CMD_FGR     (1 << 4) /* force global resume */
#define CMD_SWDBG   (1 << 5) /* software debug */
#define CMD_CF      (1 << 6) /* configure flag */
#define CMD_MAXP    (1 << 7) /* max packet */

#define PORTSC_CCS  (1 << 0) /* current connect status */
#define PORTSC_CSC  (1 << 1) /* connect status change */
#define PORTSC_PE   (1 << 2) /* port enable / disable */
#define PORTSC_PEC  (1 << 3) /* port enable / disable change */
#define PORTSC_LS   (3 << 4) /* line status */
#define PORTSC_RD   (1 << 6) /* resume detect */
#define PORTSC_LSDA (1 << 8) /* low speed device attached */
#define PORTSC_RST  (1 << 9) /* port reset */
#define PORTSC_SUS  (1 << 12) /* suspend */

#define UHCI_RU8(uhci, reg) pci_ru8(&(uhci)->pci_map, reg)
#define UHCI_RU16(uhci, reg) pci_ru16(&(uhci)->pci_map, reg)
#define UHCI_RU32(uhci, reg) pci_ru32(&(uhci)->pci_map, reg)
#define UHCI_WU8(uhci, reg, val) pci_wu8(&(uhci)->pci_map, reg, val)
#define UHCI_WU16(uhci, reg, val) pci_wu16(&(uhci)->pci_map, reg, val)
#define UHCI_WU32(uhci, reg, val) pci_wu32(&(uhci)->pci_map, reg, val)
#define UHCI_LOCK(uhci) mutex_lock(&(uhci)->mutex)
#define UHCI_UNLOCK(uhci) mutex_unlock(&(uhci)->mutex)

struct uhci
{
	struct pci_device *device;
	struct pci_map pci_map;
	struct irq_handle irq_handle;
	struct page *frame_list_page;
	uint32_t *frame_list;
	struct page *td_page;
	struct uhci_td *td;
	struct page *qh_page;
	struct uhci_qh *qh;
};

struct uhci_td
{
	uint32_t link;
	uint32_t flags;
	uint32_t token;
	uint32_t buffer;
	uint8_t reserved[16];
};

struct uhci_qh
{
	uint32_t link;
	uint32_t next;
	uint8_t reserved[8];
};

static void int_handler(void *userptr)
{
	struct uhci *uhci = userptr;
	uint16_t status = UHCI_RU16(uhci, REG_USBSTS);
	if (!status)
		return;
	UHCI_WU16(uhci, REG_USBSTS, status);
	printf("uhci irq 0x%04x\n", status);
	printf("portsc1: 0x%04x\n", UHCI_RU16(uhci, REG_PORTSC1));
	printf("portsc2: 0x%04x\n", UHCI_RU16(uhci, REG_PORTSC2));
}

static void setup_port(struct uhci *uhci, uint8_t port)
{
	uint16_t status = UHCI_RU16(uhci, port);
	if (!(status & PORTSC_CCS))
		return;
	UHCI_WU16(uhci, port, PORTSC_PE);
	printf("portsc: 0x%04x\n", UHCI_RU16(uhci, port));
}

static int setup_frame_list(struct uhci *uhci)
{
	int ret = pm_alloc_page(&uhci->frame_list_page);
	if (ret)
	{
		printf("uhci: failed to allocate frame list page\n");
		return ret;
	}
	uhci->frame_list = vm_map(uhci->frame_list_page, PAGE_SIZE, VM_PROT_RW);
	if (!uhci->frame_list)
	{
		printf("uhci: failed to map frame list\n");
		return -ENOMEM;
	}
	for (size_t i = 0; i < 1024; ++i)
		uhci->frame_list[i] = 1; /* invalid bit */
	ret = pm_alloc_page(&uhci->td_page);
	if (ret)
	{
		printf("uhci: failed to allocate descriptors page\n");
		return ret;
	}
	uhci->td = vm_map(uhci->td_page, PAGE_SIZE, VM_PROT_RW);
	if (!uhci->td)
	{
		printf("uhci: failed to map descriptors\n");
		return -ENOMEM;
	}
	ret = pm_alloc_page(&uhci->qh_page);
	if (ret)
	{
		printf("uhci: failed to allocate queue heads page\n");
		return ret;
	}
	uhci->qh = vm_map(uhci->qh_page, PAGE_SIZE, VM_PROT_RW);
	if (!uhci->qh)
	{
		printf("uhci: failed to map queue heads\n");
		return -ENOMEM;
	}
	return 0;
}

int uhci_init(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct uhci *uhci = malloc(sizeof(*uhci), M_ZERO);
	if (!uhci)
	{
		printf("uhci: uhci allocation failed\n");
		return -ENOMEM;
	}
	uhci->device = device;
	pci_enable_bus_mastering(device);
	int ret = pci_map(&uhci->pci_map, device->header0.bar4, PAGE_SIZE, 0);
	if (ret)
	{
		printf("uhci: failed to init bar4\n");
		goto err;
	}
	UHCI_WU16(uhci, REG_USBCMD, CMD_HCRESET);
	while (UHCI_RU16(uhci, REG_USBCMD) & CMD_HCRESET)
		;
	ret = setup_frame_list(uhci);
	if (ret)
		goto err;
	UHCI_WU32(uhci, REG_FRBASEADD, pm_page_addr(uhci->frame_list_page));
	ret = register_pci_irq(device, int_handler, uhci, &uhci->irq_handle);
	if (ret)
	{
		printf("uhci: failed to enable irq\n");
		goto err;
	}
	UHCI_WU16(uhci, REG_USBINTR, 0x000F);
	UHCI_WU32(uhci, REG_USBCMD, CMD_RS);
	setup_port(uhci, REG_PORTSC1);
	setup_port(uhci, REG_PORTSC2);
	return 0;

err:
	if (uhci->td)
		vm_unmap(uhci->td, PAGE_SIZE);
	if (uhci->td_page)
		pm_free_page(uhci->td_page);
	if (uhci->frame_list)
		vm_unmap(uhci->frame_list, PAGE_SIZE);
	if (uhci->frame_list_page)
		pm_free_page(uhci->frame_list_page);
	free(uhci);
	return ret;
}
