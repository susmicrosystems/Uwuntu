#include <errno.h>
#include <kmod.h>
#include <std.h>
#include <pci.h>
#include <uio.h>
#include <snd.h>

/* Intel® 82801AA (ICH) & Intel ® 82801AB
 * (ICH0) I/O Controller Hub AC ’97
 *
 *
 * Audio Codec ‘97. Revision 2.3
 *
 *
 * Intel ® I/O Controller Hub 7 (ICH7)/
 * Intel ® High Definition Audio/AC’97
 */

/* NAM */
#define REG_NAM_RESET           0x00
#define REG_NAM_MASTER_VOLUME   0x02
#define REG_NAM_AUX_OUT_VOLUME  0x04
#define REG_NAM_MONO_VOLUME     0x06
#define REG_NAM_MASTER_TONE     0x08
#define REG_NAM_PC_BEEP_VOLUME  0x0A
#define REG_NAM_PHONE_VOLUME    0x0C
#define REG_NAM_MIC_VOLUME      0x0E
#define REG_NAM_LINE_IN_VOLUME  0x10
#define REG_NAM_CD_VOLUME       0x12
#define REG_NAM_VIDEO_VOLUME    0x14
#define REG_NAM_AUX_IN_VOLUME   0x16
#define REG_NAM_PCM_OUT_VOLUME  0x18
#define REG_NAM_RECORD_SELECT   0x1A
#define REG_NAM_RECORD_GAIN     0x1C
#define REG_NAM_RECORD_GAIN_MIC 0x1E
#define REG_NAM_GENERAL_PURPOSE 0x20
#define REG_NAM_3D_CONTROL      0x22
#define REG_NAM_AUDIO_INT_PAGE  0x24
#define REG_NAM_POWERDOWN_CTRL  0x26
#define REG_NAM_EXTENDED_AUDIO  0x28
#define REG_NAM_EXTENDED_MODEL  0x3C
#define REG_NAM_VENDOR_RSRVD1   0x5A
#define REG_NAM_PAGE_REGISTERS  0x60
#define REG_NAM_VENDOR_RSRVD2   0x70
#define REG_NAM_VENDOR_ID1      0x7C
#define REG_NAM_VENDOR_ID2      0x7E

/* NABM */
#define REG_NABM_GLOB_CNT 0x2C /* global control */
#define REG_NABM_GLOB_STA 0x30 /* global status */
#define REG_NABM_CAS      0x34 /* codec access semaphore */
#define REG_NABM_SDM      0x80 /* sdata in map */

#define REG_NABMD_BDBAR 0x00 /* buffer descriptor base address register */
#define REG_NABMD_CIV   0x04 /* current index value */
#define REG_NABMD_LVI   0x05 /* last valid index */
#define REG_NABMD_SR    0x06 /* status */
#define REG_NABMD_PICB  0x08 /* position in current buffer */
#define REG_NABMD_PIV   0x0A /* prefetched index value */
#define REG_NABMD_CR    0x0B /* control */

#define REG_NABM_PIBD(r) (0x00 + (r))
#define REG_NABM_POBD(r) (0x10 + (r))
#define REG_NABM_MCBD(r) (0x10 + (r))

#define NABMD_CR_RPBM  (1 << 0) /* run/pause master bus */
#define NABMD_CR_RR    (1 << 1) /* reset */
#define NABMD_CR_LVBIE (1 << 2) /* last valid buffer interrupt enable */
#define NABMD_CR_FEIE  (1 << 3) /* fifo error interrupt enable */
#define NABMD_CR_IOCE  (1 << 4) /* interrupt on completion enable */

#define NABMD_SR_DCH   (1 << 0) /* DMA halted */
#define NABMD_SR_CELV  (1 << 1) /* CIV == LVI */
#define NABMD_SR_LVBCI (1 << 2) /* last valid buffer processed (BUF_DESC_F_BUP) */
#define NABMD_SR_BCIS  (1 << 3) /* buffer completion status (BUF_DESC_F_IOC) */
#define NABMD_SR_FIFOE (1 << 4) /* FIFO error */

#define NAM_RU8(ac97, reg) pci_ru8(&(ac97)->nambar, reg)
#define NAM_RU16(ac97, reg) pci_ru16(&(ac97)->nambar, reg)
#define NAM_RU32(ac97, reg) pci_ru32(&(ac97)->nambar, reg)
#define NAM_WU8(ac97, reg, val) pci_wu8(&(ac97)->nambar, reg, val)
#define NAM_WU16(ac97, reg, val) pci_wu16(&(ac97)->nambar, reg, val)
#define NAM_WU32(ac97, reg, val) pci_wu32(&(ac97)->nambar, reg, val)

#define NABM_RU8(ac97, reg) pci_ru8(&(ac97)->nabmbar, reg)
#define NABM_RU16(ac97, reg) pci_ru16(&(ac97)->nabmbar, reg)
#define NABM_RU32(ac97, reg) pci_ru32(&(ac97)->nabmbar, reg)
#define NABM_WU8(ac97, reg, val) pci_wu8(&(ac97)->nabmbar, reg, val)
#define NABM_WU16(ac97, reg, val) pci_wu16(&(ac97)->nabmbar, reg, val)
#define NABM_WU32(ac97, reg, val) pci_wu32(&(ac97)->nabmbar, reg, val)

#define BUF_DESC_F_BUP (1 << 30)
#define BUF_DESC_F_IOC (1 << 31)

#define DESC_AHEAD 2

struct buf_desc
{
	uint32_t addr;
	uint32_t size;
};

struct ac97
{
	struct pci_device *device;
	struct pci_map nambar;
	struct pci_map nabmbar;
	struct page *out_desc_page;
	struct buf_desc *out_desc;
	struct irq_handle irq_handle;
	struct snd *out;
};

void int_handler(void *userptr)
{
	struct ac97 *ac97 = userptr;
	uint16_t sr = NABM_RU16(ac97, REG_NABM_POBD(REG_NABMD_SR));
	if (!sr)
		return;
	NABM_WU16(ac97, REG_NABM_POBD(REG_NABMD_SR), sr);
#if 0
	printf("ac97 irq: %08" PRIx32 " / %04" PRIx16 "\n",
	       AC97_NABM_WU32(ac97, REG_NABM_GLOB_STA), sr);
#endif
	uint8_t lvi = NABM_RU8(ac97, REG_NABM_POBD(REG_NABMD_LVI));
	uint8_t civ = NABM_RU8(ac97, REG_NABM_POBD(REG_NABMD_CIV));
#if 0
	printf("LVI: %" PRIu8 "\n", lvi);
	printf("CIV: %" PRIu8 "\n", civ);
#endif
	uint8_t buf_to = (civ + DESC_AHEAD) % ac97->out->nbufs;
	while (lvi != buf_to)
	{
#if 1
		snd_fill_buf(ac97->out, &ac97->out->bufs[lvi]);
		lvi = (lvi + 1) % ac97->out->nbufs;
#endif
	}
	NABM_WU8(ac97, REG_NABM_POBD(REG_NABMD_LVI), lvi);
	if (sr & NABMD_SR_DCH) /* dma should always run */
		NABM_WU8(ac97, REG_NABM_POBD(REG_NABMD_CR),
		         NABMD_CR_IOCE | NABMD_CR_RPBM);
}

void ac97_free(struct ac97 *ac97)
{
	if (ac97->out_desc)
		vm_unmap(ac97->out_desc, PAGE_SIZE);
	if (ac97->out_desc_page)
		pm_free_page(ac97->out_desc_page);
	snd_free(ac97->out);
	free(ac97);
}

int init_pci(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct ac97 *ac97 = malloc(sizeof(*ac97), M_ZERO);
	if (!ac97)
	{
		printf("ac97: failed to allocate ac97\n");
		return -ENOMEM;
	}
	int ret = snd_alloc(&ac97->out);
	if (ret)
	{
		printf("ac97: failed to create out snd\n");
		goto err;
	}
	pci_enable_bus_mastering(device);
	ret = pci_map(&ac97->nambar, device->header0.bar0, PAGE_SIZE, 0);
	if (ret)
	{
		printf("ac97: failed to init nambar\n");
		goto err;
	}
	ret = pci_map(&ac97->nabmbar, device->header0.bar1, PAGE_SIZE, 0);
	if (ret)
	{
		printf("ac97: failed to init nabmbar\n");
		goto err;
	}
	NABM_WU32(ac97, REG_NABM_GLOB_CNT, 0x3);
	NAM_WU16(ac97, REG_NAM_RESET, 0);
#if 0
	uint16_t vendor1 = NAM_RU16(ac97, REG_VENDOR_ID1);
	uint16_t vendor2 = NAM_RU16(ac97, REG_VENDOR_ID2);
	printf("vendor: %x%x%x%x\n", (vendor1 >> 8) & 0xFF, vendor1 & 0xFF,
	       (vendor2 >> 8) & 0xFF, vendor2 & 0xFF);
	printf("status: %08lx\n", NABM_WU32(ac97, REG_NABM_GLOB_STA));
#endif
	ret = pm_alloc_page(&ac97->out_desc_page);
	if (ret)
	{
		printf("ac97: failed to allocate output desc page\n");
		goto err;
	}
	ac97->out_desc = vm_map(ac97->out_desc_page, PAGE_SIZE, VM_PROT_RW);
	if (!ac97->out_desc)
	{
		printf("ac97: failed to vmap out desc page\n");
		ret = -ENOMEM;
		goto err;
	}
	for (size_t i = 0; i < ac97->out->nbufs; ++i)
	{
		ac97->out_desc[i].addr = pm_page_addr(ac97->out->bufs[i].page);
		ac97->out_desc[i].size = PAGE_SIZE / 2;
		ac97->out_desc[i].size |= BUF_DESC_F_IOC;
	}
	NAM_WU16(ac97, REG_NAM_MASTER_VOLUME, 0x0000);
	NAM_WU16(ac97, REG_NAM_PCM_OUT_VOLUME, 0x0000);
	NABM_WU8(ac97, REG_NABM_POBD(REG_NABMD_CR), NABMD_CR_RR);
	while (NABM_RU8(ac97, REG_NABM_POBD(REG_NABMD_CR)) & NABMD_CR_RR)
		;
	NABM_WU32(ac97, REG_NABM_POBD(REG_NABMD_BDBAR),
	          pm_page_addr(ac97->out_desc_page));
	NABM_WU8(ac97, REG_NABM_POBD(REG_NABMD_LVI), DESC_AHEAD);
	ret = register_pci_irq(device, int_handler, ac97, &ac97->irq_handle);
	if (ret)
	{
		printf("ac97: failed to enable irq\n");
		goto err;
	}
	NABM_WU8(ac97, REG_NABM_POBD(REG_NABMD_CR),
	          NABMD_CR_IOCE | NABMD_CR_RPBM);
	return 0;

err:
	ac97_free(ac97);
	return ret;
}

static int init(void)
{
	pci_probe(0x8086, 0x2415, init_pci, NULL);
	return 0;
}

static void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "ac97",
	.init = init,
	.fini = fini,
};
