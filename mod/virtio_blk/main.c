#include "virtio.h"

#include <errno.h>
#include <disk.h>
#include <kmod.h>
#include <uio.h>
#include <std.h>

#define VIRTIO_BLK_F_BARRIER      0
#define VIRTIO_BLK_F_SIZE_MAX     1
#define VIRTIO_BLK_F_SEG_MAX      2
#define VIRTIO_BLK_F_GEOMETRY     4
#define VIRTIO_BLK_F_RO           5
#define VIRTIO_BLK_F_BLK_SIZE     6
#define VIRTIO_BLK_F_SCSI         7
#define VIRTIO_BLK_F_FLUSH        9
#define VIRTIO_BLK_F_TOPOLOGY     10
#define VIRTIO_BLK_F_CONFIG_WCE   11
#define VIRTIO_BLK_F_MQ           12
#define VIRTIO_BLK_F_DISCARD      13
#define VIRTIO_BLK_F_WRITE_ZEROES 14
#define VIRTIO_BLK_F_LIFETIME     15
#define VIRTIO_BLK_F_SECURE_ERASE 16

#define VIRTIO_BLK_F_DISCARD_UNMAP 1

#define VIRTIO_BLK_T_IN           0
#define VIRTIO_BLK_T_OUT          1
#define VIRTIO_BLK_T_FLUSH        4
#define VIRTIO_BLK_T_GET_ID       8
#define VIRTIO_BLK_T_GET_LIFETIME 10
#define VIRTIO_BLK_T_DISCARD      11
#define VIRTIO_BLK_T_WRITE_ZEROES 13
#define VIRTIO_BLK_T_SECURE_ERASE 14

#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

#define VIRTIO_BLK_C_CAPACITY             0x00
#define VIRTIO_BLK_C_SIZE_MAX             0x08
#define VIRTIO_BLK_C_SEG_MAX              0x0C
#define VIRTIO_BLK_C_CYLINDERS            0x10
#define VIRTIO_BLK_C_HEADS                0x12
#define VIRTIO_BLK_C_SECTORS              0x13
#define VIRTIO_BLK_C_BLK_SIZE             0x14
#define VIRTIO_BLK_C_PHYSICAL_BLOCK_EXP   0x18
#define VIRTIO_BLK_C_ALIGN_OFF            0x19
#define VIRTIO_BLK_C_MIN_IO_SIZE          0x1A
#define VIRTIO_BLK_C_MAX_IO_SIZE          0x1C
#define VIRTIO_BLK_C_WRITEBACK            0x20
#define VIRTIO_BLK_C_MAX_DISCARD_SECTORS  0x24
#define VIRTIO_BLK_C_MAX_DISCARD_SEG      0x28
#define VIRTIO_BLK_C_DISCARD_SECTOR_ALIGN 0x2C
#define VIRTIO_BLK_C_MAX_ZEROES_SECTORS   0x30
#define VIRTIO_BLK_C_MAX_ZEROES_SEG       0x34
#define VIRTIO_BLK_C_ZEROES_MAY_UNMAP     0x38

#define BLOCK_SIZE 512

struct virtio_blk_req
{
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
};

struct virtio_blk_discard_write_zeroes
{
	uint64_t sectors;
	uint32_t num_sectors;
	uint32_t flags;
};

struct virtio_blk
{
	struct virtio_dev dev;
	struct pci_map blk_cfg;
	struct disk *disk;
	struct mutex mutex;
	struct page *buf_page;
	uint8_t *buf;
	struct waitq waitq;
	struct spinlock waitq_sl;
};

static ssize_t dread(struct disk *disk, struct uio *uio);
static ssize_t dwrite(struct disk *disk, struct uio *uio);

static const struct disk_op g_op =
{
	.read = dread,
	.write = dwrite,
};

static void on_msg(struct virtq *queue, uint16_t id, uint32_t len)
{
	(void)id; /* XXX used for parallel requests */
	(void)len;
	struct virtio_blk *blk = (struct virtio_blk*)queue->dev;
	spinlock_lock(&blk->waitq_sl);
	waitq_signal(&blk->waitq, 0);
	spinlock_unlock(&blk->waitq_sl);
}

static int wait_buf(struct virtio_blk *blk)
{
	spinlock_lock(&blk->waitq_sl);
	int ret = waitq_wait_head(&blk->waitq, &blk->waitq_sl, NULL);
	spinlock_unlock(&blk->waitq_sl);
	return ret;
}

static ssize_t dread(struct disk *disk, struct uio *uio)
{
	struct virtio_blk *blk = disk->userdata;
	size_t numsect = uio->count / BLOCK_SIZE;
	ssize_t ret;
	size_t rd = 0;
	mutex_lock(&blk->mutex);
	for (size_t i = 0; i < numsect; ++i)
	{
		struct virtio_blk_req *req = (struct virtio_blk_req*)blk->buf;
		req->type = VIRTIO_BLK_T_IN;
		req->reserved = 0;
		req->sector = uio->off / BLOCK_SIZE;
		struct virtq_buf bufs[2];
		bufs[0].addr = pm_page_addr(blk->buf_page);
		bufs[0].size = 16;
		bufs[1].addr = bufs[0].addr + 16;
		bufs[1].size = BLOCK_SIZE + 1;
		ret = virtq_send(&blk->dev.queues[0], bufs, 1, 1);
		if (ret < 0)
			goto end;
		virtq_notify(&blk->dev.queues[0]);
		ret = wait_buf(blk);
		if (ret)
			goto end;
		if (blk->buf[16 + BLOCK_SIZE] != VIRTIO_BLK_S_OK)
		{
			printf("virtio_blk: read request failure\n");
			ret = -ENXIO;
			goto end;
		}
		ret = uio_copyin(uio, &blk->buf[16], BLOCK_SIZE);
		if (ret < 0)
			goto end;
		rd += BLOCK_SIZE;
	}
	ret = rd;

end:
	mutex_unlock(&blk->mutex);
	return ret;
}

static ssize_t dwrite(struct disk *disk, struct uio *uio)
{
	struct virtio_blk *blk = disk->userdata;
	size_t numsect = uio->count / BLOCK_SIZE;
	ssize_t ret;
	size_t rd = 0;
	mutex_lock(&blk->mutex);
	for (size_t i = 0; i < numsect; ++i)
	{
		ret = uio_copyout(&blk->buf[16], uio, BLOCK_SIZE);
		if (ret < 0)
			goto end;
		struct virtio_blk_req *req = (struct virtio_blk_req*)blk->buf;
		req->type = VIRTIO_BLK_T_OUT;
		req->reserved = 0;
		req->sector = uio->off / BLOCK_SIZE;
		struct virtq_buf bufs[2];
		bufs[0].addr = pm_page_addr(blk->buf_page);
		bufs[0].size = 16;
		bufs[1].addr = bufs[0].addr + 16 + BLOCK_SIZE;
		bufs[1].size = 1;
		ret = virtq_send(&blk->dev.queues[0], bufs, 2, 0);
		if (ret < 0)
			goto end;
		virtq_notify(&blk->dev.queues[0]);
		ret = wait_buf(blk);
		if (ret)
			goto end;
		if (blk->buf[16 + BLOCK_SIZE] != VIRTIO_BLK_S_OK)
		{
			printf("virtio_blk: write request failure\n");
			ret = -ENXIO;
			goto end;
		}
		rd += BLOCK_SIZE;
	}
	ret = rd;

end:
	mutex_unlock(&blk->mutex);
	return ret;
}

static inline void print_blk_cfg(struct uio *uio, struct pci_map *blk_cfg)
{
	uprintf(uio, "capacity: 0x%" PRIx64 "\n",
	        pci_ru64(blk_cfg, VIRTIO_BLK_C_CAPACITY));
	uprintf(uio, "size_max: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_SIZE_MAX));
	uprintf(uio, "seg_max: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_SEG_MAX));
	uprintf(uio, "cylinders: 0x%" PRIx16 "\n",
	        pci_ru16(blk_cfg, VIRTIO_BLK_C_CYLINDERS));
	uprintf(uio, "heads: 0x%" PRIx16 "\n",
	        pci_ru16(blk_cfg, VIRTIO_BLK_C_HEADS));
	uprintf(uio, "sectors: 0x%" PRIx16 "\n",
	        pci_ru16(blk_cfg, VIRTIO_BLK_C_SECTORS));
	uprintf(uio, "blk_size: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_BLK_SIZE));
	uprintf(uio, "physical_block_exp: 0x%" PRIx8 "\n",
	        pci_ru8(blk_cfg, VIRTIO_BLK_C_PHYSICAL_BLOCK_EXP));
	uprintf(uio, "align_off: 0x%" PRIx8 "\n",
	        pci_ru8(blk_cfg, VIRTIO_BLK_C_ALIGN_OFF));
	uprintf(uio, "min_io_size: 0x%" PRIx16 "\n",
	        pci_ru16(blk_cfg, VIRTIO_BLK_C_MIN_IO_SIZE));
	uprintf(uio, "max_io_size: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_MAX_IO_SIZE));
	uprintf(uio, "writeback: 0x%" PRIx8 "\n",
	        pci_ru8(blk_cfg, VIRTIO_BLK_C_WRITEBACK));
	uprintf(uio, "max_discard_sectors: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_MAX_DISCARD_SECTORS));
	uprintf(uio, "max_discard_seg: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_MAX_DISCARD_SEG));
	uprintf(uio, "discard_sector_alignment: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_DISCARD_SECTOR_ALIGN));
	uprintf(uio, "max_write_zeroes_sectors: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_MAX_ZEROES_SECTORS));
	uprintf(uio, "max_write_zeroes_seg: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_MAX_ZEROES_SEG));
	uprintf(uio, "write_zeroes_may_unmap: 0x%" PRIx32 "\n",
	        pci_ru32(blk_cfg, VIRTIO_BLK_C_ZEROES_MAY_UNMAP));
}

static void virtio_blk_delete(struct virtio_blk *blk)
{
	if (!blk)
		return;
	if (blk->buf)
		vm_unmap(blk->buf, PAGE_SIZE);
	if (blk->buf_page)
		pm_free_page(blk->buf_page);
	pci_unmap(&blk->blk_cfg);
	virtio_dev_destroy(&blk->dev);
	mutex_destroy(&blk->mutex);
	waitq_destroy(&blk->waitq);
	spinlock_destroy(&blk->waitq_sl);
	free(blk);
}

int init_pci(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct virtio_blk *blk = malloc(sizeof(*blk), M_ZERO);
	if (!blk)
	{
		printf("virtio_blk: allocation failed\n");
		return -ENOMEM;
	}
	mutex_init(&blk->mutex, 0);
	waitq_init(&blk->waitq);
	spinlock_init(&blk->waitq_sl);
	uint8_t features[(VIRTIO_F_RING_RESET + 7) / 8];
	memset(features, 0, sizeof(features));
	int ret = virtio_dev_init(&blk->dev, device, features, VIRTIO_F_RING_RESET);
	if (ret)
	{
		virtio_blk_delete(blk);
		return ret;
	}
	if (blk->dev.queues_nb < 1)
	{
		printf("virtio_blk: no queues\n");
		virtio_blk_delete(blk);
		return -EINVAL;
	}
	ret = virtio_get_cfg(device, VIRTIO_PCI_CAP_DEVICE_CFG,
	                     &blk->blk_cfg, 0x40, NULL);
	if (ret)
	{
		virtio_blk_delete(blk);
		return ret;
	}
#if 0
	print_blk_cfg(NULL, &blk->blk_cfg);
#endif
	ret = pm_alloc_page(&blk->buf_page);
	if (ret)
	{
		printf("virtio_blk: failed to allocate page\n");
		virtio_blk_delete(blk);
		return ret;
	}
	blk->buf = vm_map(blk->buf_page, PAGE_SIZE, VM_PROT_RW);
	if (!blk->buf)
	{
		printf("virtio_blk: failed to map page\n");
		virtio_blk_delete(blk);
		return -ENOMEM;
	}
	blk->dev.queues[0].on_msg = on_msg;
	ret = virtq_setup_irq(&blk->dev.queues[0]);
	if (ret)
	{
		printf("virtio_blk: failed to setup irq\n");
		virtio_blk_delete(blk);
		return ret;
	}
	virtio_dev_init_end(&blk->dev);
	uint64_t capacity = pci_ru64(&blk->blk_cfg, VIRTIO_BLK_C_CAPACITY);
	ret = disk_new("vbd", makedev(97, 0), capacity * BLOCK_SIZE,
	               &g_op, &blk->disk);
	if (ret)
	{
		virtio_blk_delete(blk);
		return ret;
	}
	blk->disk->userdata = blk;
	ret = disk_load(blk->disk);
	if (ret)
	{
		virtio_blk_delete(blk);
		return ret;
	}
	return 0;
}

static int init(void)
{
	pci_probe(0x1AF4, 0x1001, init_pci, NULL);
	return 0;
}

static void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "virtio_blk",
	.init = init,
	.fini = fini,
};
