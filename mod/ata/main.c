#include "struct.h"

#include "arch/x86/asm.h"
#include "arch/x86/x86.h"

#include <endian.h>
#include <errno.h>
#include <mutex.h>
#include <file.h>
#include <disk.h>
#include <stat.h>
#include <kmod.h>
#include <std.h>
#include <vfs.h>
#include <uio.h>
#include <pci.h>

/* T13/1699D Revision 2b */

#define BLOCK_SIZE 512

enum ata_status_bit
{
	ATA_SR_ERR  = 0x01, /* error */
	ATA_SR_IDX  = 0x02, /* index */
	ATA_SR_CORR = 0x04, /* corrected data */
	ATA_SR_DRQ  = 0x08, /* data request ready */
	ATA_SR_DSC  = 0x10, /* drive seek complete */
	ATA_SR_DF   = 0x20, /* drive write fault */
	ATA_SR_DRDY = 0x40, /* drive ready */
	ATA_SR_BSY  = 0x80, /* busy */
};

enum ata_err_bit
{
	ATA_ER_AMNF  = 0x01, /* no address mark */
	ATA_ER_TK0NF = 0x02, /* track 0 not found */
	ATA_ER_ABRT  = 0x04, /* command aborted */
	ATA_ER_MCR   = 0x08, /* media change request */
	ATA_ER_IDNF  = 0x10, /* ID mark not found */
	ATA_ER_MC    = 0x20, /* media changed */
	ATA_ER_UNC   = 0x40, /* uncorrectable data */
	ATA_ER_BBK   = 0x80, /* bad block */
};

enum ata_cmd
{
	ATA_CMD_CFA_ERASE_SECTORS                = 0xC0,
	ATA_CMD_CFA_REQUEST_EXTENDED_ERROR_CODE  = 0x03,
	ATA_CMD_CFA_TRANSLATE_SECTOR             = 0x87,
	ATA_CMD_CFA_WRITE_MULTIPLE_WITHOUT_ERASE = 0xCD,
	ATA_CMD_CFA_WRITE_SECTORS_WITHOUT_ERASE  = 0x38,
	ATA_CMD_CHECK_MEDIA_CARD_TYPE            = 0xD1,
	ATA_CMD_CHECK_POWER_MODE                 = 0xE5,
	ATA_CMD_CONFIGURE_STREAM                 = 0x51,
	ATA_CMD_DEVICE_CONFIGURATION             = 0xB1,
	ATA_CMD_DEVICE_RESET                     = 0x08,
	ATA_CMD_DOWNLOAD_MICROCODE               = 0x92,
	ATA_CMD_EXECUTE_DEVICE_DIAGNOSTIC        = 0x90,
	ATA_CMD_FLUSH_CACHE                      = 0xE7,
	ATA_CMD_FLUSH_CACHE_EXT                  = 0xEA,
	ATA_CMD_GET_MEDIA_STATUS                 = 0xDA,
	ATA_CMD_IDENTIFY_DEVICE                  = 0xEC,
	ATA_CMD_IDENTIFY_PACKET_DEVICE           = 0xA1,
	ATA_CMD_IDLE                             = 0xE3,
	ATA_CMD_IDLE_IMMEDIATE                   = 0xE1,
	ATA_CMD_MEDIA_EJECT                      = 0xED,
	ATA_CMD_MEDIA_LOCK                       = 0xDE,
	ATA_CMD_MEDIA_UNLOCK                     = 0xDF,
	ATA_CMD_NOP                              = 0x00,
	ATA_CMD_NV_CACHE                         = 0xB6,
	ATA_CMD_PACKET                           = 0xA0,
	ATA_CMD_READ_BUFFER                      = 0xE4,
	ATA_CMD_READ_DMA                         = 0xC8,
	ATA_CMD_READ_DMA_EXT                     = 0x25,
	ATA_CMD_READ_DMA_QUEUED                  = 0xC7,
	ATA_CMD_READ_DMA_QUEUED_EXT              = 0x26,
	ATA_CMD_READ_LOG_EXT                     = 0x2F,
	ATA_CMD_READ_LOG_DMA_EXT                 = 0x47,
	ATA_CMD_READ_MULTIPLE                    = 0xC4,
	ATA_CMD_READ_NATIVE_MAX_ADDRESS          = 0xF8,
	ATA_CMD_READ_NATIVE_MAX_ADDRESS_EXT      = 0x27,
	ATA_CMD_READ_SECTORS                     = 0x20,
	ATA_CMD_READ_SECTORS_EXT                 = 0x24,
	ATA_CMD_READ_STREAM_DMA_EXT              = 0x2A,
	ATA_CMD_READ_STREAM_EXT                  = 0x2B,
	ATA_CMD_READ_VERIFY_SECTORS              = 0x40,
	ATA_CMD_READ_VERIFY_SECTORS_EXT          = 0x42,
	ATA_CMD_SECURITY_DISABLE_PASSWORD        = 0xF6,
	ATA_CMD_SECURITY_ERASE_PREPARE           = 0xF3,
	ATA_CMD_SECURITY_ERASE_UNIT              = 0xF4,
	ATA_CMD_SECURITY_FREEZE_LOCK             = 0xF5,
	ATA_CMD_SECURITY_SET_PASSWORD            = 0xF1,
	ATA_CMD_SECURITY_UNLOCK                  = 0xF2,
	ATA_CMD_SERVICE                          = 0xA2,
	ATA_CMD_SET_FEATURES                     = 0xEF,
	ATA_CMD_SET_MAX                          = 0xF9,
	ATA_CMD_SET_MAX_ADDRESS_EXT              = 0x37,
	ATA_CMD_SET_MULTIPLE_MODE                = 0xC6,
	ATA_CMD_SLEEP                            = 0xE6,
	ATA_CMD_SMART                            = 0xB0,
	ATA_CMD_SMART_WRITE_LOG                  = 0xD6,
	ATA_CMD_STANDBY                          = 0xE2,
	ATA_CMD_STANDBY_IMMEDIATE                = 0xE0,
	ATA_CMD_WRITE_BUFFER                     = 0xE8,
	ATA_CMD_WRITE_DMA                        = 0xCA,
	ATA_CMD_WRITE_DMA_EXT                    = 0x35,
	ATA_CMD_WRITE_DMA_FUA_EXT                = 0x3D,
	ATA_CMD_WRITE_DMA_QUEUED                 = 0xCC,
	ATA_CMD_WRITE_DMA_QUEUED_EXT             = 0x36,
	ATA_CMD_WRITE_DMA_QUEUED_FUA_EXT         = 0x3E,
	ATA_CMD_WRITE_LOG_EXT                    = 0x3F,
	ATA_CMD_WRITE_LOG_DMA_EXT                = 0x47,
	ATA_CMD_WRITE_MULTIPLE                   = 0xC3,
	ATA_CMD_WRITE_MULTIPLE_EXT               = 0x39,
	ATA_CMD_WRITE_MULTIPLE_FUA_EXT           = 0xCE,
	ATA_CMD_WRITE_SECTORS                    = 0x30,
	ATA_CMD_WRITE_SECTORS_EXT                = 0x34,
	ATA_CMD_WRITE_STREAM_DMA_EXT             = 0x3A,
	ATA_CMD_WRITE_STREAM_EXT                 = 0x3B,
	ATA_CMD_WRITE_UNCORRECTABLE_EXT          = 0x45,
};

enum ata_smart_cmd
{
	ATA_SMART_CMD_DISABLE_OPERATIONS = 0xD9,
	ATA_SMART_CMD_ATTRIBUTE_AUTOSAVE = 0xD2,
	ATA_SMART_CMD_ENABLE_OPERATIONS  = 0xD8,
	ATA_SMART_CMD_EXECUTE_IMMEDIATE  = 0xD4,
	ATA_SMART_CMD_READ_DATA          = 0xD0,
	ATA_SMART_CMD_READ_LOG           = 0xD5,
	ATA_SMART_CMD_RETURN_STATUS      = 0xDA,
	ATA_SMART_CMD_WRITE_LOG          = 0xD6,
};

#define IDE_ATA   0x00
#define IDE_ATAPI 0x01

#define ATA_MASTER 0x00
#define ATA_SLAVE  0x01

#define ATA_BASE_DATA       0x00
#define ATA_BASE_ERROR      0x01
#define ATA_BASE_FEATURES   0x01
#define ATA_BASE_SECCOUNT   0x02
#define ATA_BASE_LBALO      0x03
#define ATA_BASE_LBAMID     0x04
#define ATA_BASE_LBAHI      0x05
#define ATA_BASE_HDSEL      0x06
#define ATA_BASE_COMMAND    0x07
#define ATA_BASE_STATUS     0x07

#define ATA_CTRL_CONTROL 0x02
#define ATA_CTRL_STATUS  0x02

#define ATA_BMIDE_COMMAND 0x00
#define ATA_BMIDE_STATUS  0x02
#define ATA_BMIDE_PRDT    0x04

#define ATA_BMIDE_STATUS_DMA (1 << 0)
#define ATA_BMIDE_STATUS_ERR (1 << 1)
#define ATA_BMIDE_STATUS_IRQ (1 << 2)

#define ATA_PRIMARY   0x00
#define ATA_SECONDARY 0x01

#define ATA_READ  0x00
#define ATA_WRITE 0x01

#define ATA_PRIMARY_PORT_BASE   0x1F1
#define ATA_PRIMARY_PORT_CTRL   0x3F7
#define ATA_SECONDARY_PORT_BASE 0x171
#define ATA_SECONDARY_PORT_CTRL 0x377

#define ATA_CHANNEL_EXISTS (1 << 0)
#define ATA_CHANNEL_BMIDE  (1 << 1)
#define ATA_CHANNEL_INT    (1 << 2)

#define BASE_RU8(channel, reg) pci_ru8(&(channel)->base, reg)
#define BASE_WU8(channel, reg, val) pci_wu8(&(channel)->base, reg, val)
#define CTRL_RU8(channel, reg) pci_ru8(&(channel)->ctrl, reg)
#define CTRL_WU8(channel, reg, val) pci_wu8(&(channel)->ctrl, reg, val)
#define BMIDE_RU8(channel, reg) pci_ru8(&(channel)->bmide, reg)
#define BMIDE_RU32(channel, reg) pci_ru32(&(channel)->bmide, reg)
#define BMIDE_WU8(channel, reg, val) pci_wu8(&(channel)->bmide, reg, val)
#define BMIDE_WU32(channel, reg, val) pci_wu32(&(channel)->bmide, reg, val)

struct ata_channel
{
	struct pci_map base;
	struct pci_map ctrl;
	struct pci_map bmide;
	uint8_t flags;
	uint8_t id;
	struct mutex mutex;
	struct page *buf_page;
	void *buf;
	struct page *prdt_page;
	uint64_t *prdt;
	struct irq_handle irq_handle;
	struct waitq waitq;
	struct spinlock waitq_sl;
};

struct ata_device
{
	struct ata_identify_device identify;
	struct ata_identify_packet identify_packet;
	struct ata_channel *channel;
	uint8_t id;
	uint8_t drive;
	uint16_t type;
	uint64_t size;
	struct disk *disk;
	struct node *sysfs_node;
};

struct ata
{
	struct ata_channel channels[2];
	struct ata_device devices[4];
	uint8_t devices_count;
};

static ssize_t dread(struct disk *disk, struct uio *uio);
static ssize_t dwrite(struct disk *disk, struct uio *uio);

static const struct disk_op dop =
{
	.read = dread,
	.write = dwrite,
};

static int sysnode_open(struct file *file, struct node *node);
static ssize_t sysnode_read(struct file *file, struct uio *uio);

static const struct file_op fop =
{
	.open = sysnode_open,
	.read = sysnode_read,
};

static void int_handler(void *userdata)
{
	struct ata_channel *channel = userdata;
	if (channel->flags & ATA_CHANNEL_BMIDE)
	{
		if (!(BMIDE_RU8(channel, ATA_BMIDE_STATUS) & ATA_BMIDE_STATUS_IRQ))
			return;
		BMIDE_WU8(channel, ATA_BMIDE_STATUS, ATA_BMIDE_STATUS_IRQ);
	}
	if (!(channel->flags & ATA_CHANNEL_INT))
	{
		printf("ata: unexpected interrupt\n");
		return;
	}
	spinlock_lock(&channel->waitq_sl);
	waitq_signal(&channel->waitq, 0);
	spinlock_unlock(&channel->waitq_sl);
}

static inline void wait_busy(struct ata_channel *channel)
{
	/* XXX timeout */
	while (BASE_RU8(channel, ATA_BASE_STATUS) & ATA_SR_BSY)
		;
}

static int check_state(struct ata_channel *channel)
{
	uint8_t state = BASE_RU8(channel, ATA_BASE_STATUS);
	if (state & ATA_SR_ERR)
		return -EINVAL;
	if (state & ATA_SR_DF)
		return -EIO;
	if (!(state & ATA_SR_DRQ))
		return -EINVAL;
	return 0;
}

static void reorder_string(char *str, size_t len)
{
	for (size_t i = 0; i < len; i += 2)
	{
		uint16_t *ptr = (uint16_t*)&str[i];
		*ptr = be16toh(*ptr);
	}
}

static int ata_setup_addr(struct ata_device *device, uint64_t lba,
                          uint8_t numsects)
{
	struct ata_channel *channel = device->channel;

	if (lba + numsects > device->size)
		return -EINVAL;
	wait_busy(channel);
	if (device->identify.command_set_support.big_lba)
	{
		BASE_WU8(channel, ATA_BASE_HDSEL, 0xE0 | (device->drive << 4));
		BASE_WU8(channel, ATA_BASE_SECCOUNT, 0);
		BASE_WU8(channel, ATA_BASE_LBALO, lba >> 24);
		BASE_WU8(channel, ATA_BASE_LBAMID, lba >> 32);
		BASE_WU8(channel, ATA_BASE_LBAHI, lba >> 40);
		BASE_WU8(channel, ATA_BASE_SECCOUNT, numsects);
		BASE_WU8(channel, ATA_BASE_LBALO, lba >> 0);
		BASE_WU8(channel, ATA_BASE_LBAMID, lba >> 8);
		BASE_WU8(channel, ATA_BASE_LBAHI, lba >> 16);
	}
	else
	{
		BASE_WU8(channel, ATA_BASE_HDSEL, 0xE0 | (device->drive << 4) | ((lba >> 24) & 0xF));
		BASE_WU8(channel, ATA_BASE_SECCOUNT, numsects);
		BASE_WU8(channel, ATA_BASE_LBALO, lba >> 0);
		BASE_WU8(channel, ATA_BASE_LBAMID, lba >> 8);
		BASE_WU8(channel, ATA_BASE_LBAHI, lba >> 16);
	}
	return 0;
}

static int ata_wait_dma(struct ata_channel *channel)
{
	/* XXX timeout */
	while (BMIDE_RU8(channel, ATA_BMIDE_STATUS) & ATA_BMIDE_STATUS_DMA)
	{
		if (!(channel->flags & ATA_CHANNEL_INT))
			continue;
		/* XXX timeout */
		int ret = waitq_wait_head(&channel->waitq, &channel->waitq_sl, NULL);
		if (ret)
			return ret;
	}
	return 0;
}

static int ata_wait_pio(struct ata_channel *channel)
{
	/* XXX timeout */
	while (BASE_RU8(channel, ATA_BASE_STATUS) & ATA_SR_BSY)
	{
		if (!(channel->flags & ATA_CHANNEL_INT))
			continue;
		/* XXX timeout */
		int ret = waitq_wait_head(&channel->waitq, &channel->waitq_sl, NULL);
		if (ret)
			return ret;
	}
	return check_state(channel);
}

static void setup_dma_prdt(struct ata_channel *channel, uint8_t numsect)
{
	BMIDE_WU32(channel, ATA_BMIDE_PRDT, channel->prdt_page->offset * PAGE_SIZE);
	*channel->prdt = ((channel->buf_page->offset * PAGE_SIZE) & 0xFFFFFFFF)
	               | ((uint64_t)(numsect * BLOCK_SIZE) << 32)
	               | (1ULL << 63);
}

static int ata_read_dma(struct ata_device *device, uint8_t numsect)
{
	struct ata_channel *channel = device->channel;
	int ret;

	spinlock_lock(&channel->waitq_sl);
	setup_dma_prdt(channel, numsect);
	BMIDE_WU8(channel, ATA_BMIDE_COMMAND, 8);
	BMIDE_WU8(channel, ATA_BMIDE_STATUS, 0);
	BASE_WU8(channel, ATA_BASE_COMMAND,
	         device->identify.command_set_support.big_lba
	       ? ATA_CMD_READ_DMA_EXT
	       : ATA_CMD_READ_DMA);
	BMIDE_WU8(channel, ATA_BMIDE_COMMAND, 9);
	ret = ata_wait_dma(channel);
	spinlock_unlock(&channel->waitq_sl);
	return ret;
}

static int ata_read_pio(struct ata_device *device, uint8_t numsect)
{
	struct ata_channel *channel = device->channel;
	uint32_t words = BLOCK_SIZE / 2;
	uint16_t *buf = channel->buf;
	int ret;

	spinlock_lock(&channel->waitq_sl);
	BASE_WU8(channel, ATA_BASE_COMMAND,
	         device->identify.command_set_support.big_lba
	       ? ATA_CMD_READ_SECTORS_EXT
	       : ATA_CMD_READ_SECTORS);
	for (size_t i = 0; i < numsect; i++)
	{
		ret = ata_wait_pio(channel);
		if (ret)
			goto end;
		pci_ru16v(&channel->base, ATA_BASE_DATA, buf, words);
		buf += words;
	}
	ret = 0;

end:
	spinlock_unlock(&channel->waitq_sl);
	return ret;
}

static int ata_read_batch(struct ata_device *device, struct uio *uio,
                          uint64_t lba, uint8_t numsect)
{
	struct ata_channel *channel = device->channel;
	ssize_t ret;

	ret = ata_setup_addr(device, lba, numsect);
	if (ret)
		return ret;
	if (channel->flags & ATA_CHANNEL_BMIDE)
		ret = ata_read_dma(device, numsect);
	else
		ret = ata_read_pio(device, numsect);
	if (ret)
		return ret;
	return uio_copyin(uio, channel->buf, numsect * BLOCK_SIZE);
}

static ssize_t ata_read(struct ata_device *device, struct uio *uio)
{
	struct ata_channel *channel = device->channel;
	ssize_t ret;
	size_t rd = 0;

	while (uio->count >= BLOCK_SIZE)
	{
		size_t numsect = uio->count / BLOCK_SIZE;
		if (numsect > PAGE_SIZE / BLOCK_SIZE)
			numsect = PAGE_SIZE / BLOCK_SIZE;
		uint64_t lba = uio->off / BLOCK_SIZE;
		mutex_lock(&channel->mutex);
		ret = ata_read_batch(device, uio, lba, numsect);
		mutex_unlock(&channel->mutex);
		if (ret < 0)
			return ret;
		rd += ret;
	}
	return rd;
}

static int ata_write_dma(struct ata_device *device, uint8_t numsect)
{
	struct ata_channel *channel = device->channel;
	int ret;

	spinlock_lock(&channel->waitq_sl);
	setup_dma_prdt(channel, numsect);
	BMIDE_WU8(channel, ATA_BMIDE_COMMAND, 0);
	BMIDE_WU8(channel, ATA_BMIDE_STATUS, 0);
	BASE_WU8(channel, ATA_BASE_COMMAND,
	         device->identify.command_set_support.big_lba
	       ? ATA_CMD_WRITE_DMA_EXT
	       : ATA_CMD_WRITE_DMA);
	BMIDE_WU8(channel, ATA_BMIDE_COMMAND, 1);
	ret = ata_wait_dma(channel);
	spinlock_unlock(&channel->waitq_sl);
	return ret;
}

static int ata_write_pio(struct ata_device *device, uint8_t numsect)
{
	struct ata_channel *channel = device->channel;
	uint32_t words = BLOCK_SIZE / 2;
	uint16_t *buf = channel->buf;
	struct timespec delay;
	int ret;

	spinlock_lock(&channel->waitq_sl);
	BASE_WU8(channel, ATA_BASE_COMMAND,
	         device->identify.command_set_support.big_lba
	       ? ATA_CMD_WRITE_SECTORS_EXT
	       : ATA_CMD_WRITE_SECTORS);
	for (size_t i = 0; i < numsect; i++)
	{
		ret = ata_wait_pio(channel);
		if (ret)
			goto end;
		pci_wu16v(&channel->base, ATA_BASE_DATA, buf, words);
		buf += words;
	}
	BASE_WU8(channel, ATA_BASE_COMMAND, ATA_CMD_FLUSH_CACHE_EXT);
	delay.tv_sec = 0;
	delay.tv_nsec = 400;
	spinsleep(&delay);
	wait_busy(channel);

end:
	spinlock_unlock(&channel->waitq_sl);
	return ret;
}

static int ata_write_batch(struct ata_device *device, struct uio *uio,
                           uint64_t lba, uint8_t numsect)
{
	struct ata_channel *channel = device->channel;
	ssize_t ret;

	ret = uio_copyout(channel->buf, uio, numsect * BLOCK_SIZE);
	if (ret < 0)
		return ret;
	ret = ata_setup_addr(device, lba, numsect);
	if (ret)
		return ret;
	if (channel->flags & ATA_CHANNEL_BMIDE)
		return ata_write_dma(device, numsect);
	return ata_write_pio(device, numsect);
}

static ssize_t ata_write(struct ata_device *device, struct uio *uio)
{
	struct ata_channel *channel = device->channel;
	ssize_t ret;
	size_t wr = 0;

	while (uio->count >= BLOCK_SIZE)
	{
		size_t numsect = uio->count / BLOCK_SIZE;
		if (numsect > PAGE_SIZE / BLOCK_SIZE)
			numsect = PAGE_SIZE / BLOCK_SIZE;
		uint64_t lba = uio->off / BLOCK_SIZE;
		mutex_lock(&channel->mutex);
		ret = ata_write_batch(device, uio, lba, numsect);
		mutex_unlock(&channel->mutex);
		if (ret)
			return ret;
		wr += numsect * BLOCK_SIZE;
	}
	return wr;
}

static ssize_t dread(struct disk *disk, struct uio *uio)
{
	struct ata_device *device = disk->userdata;
	switch (device->type)
	{
		case IDE_ATA:
			return ata_read(device, uio);
		case IDE_ATAPI:
			return -EINVAL;
		default:
			return -EINVAL;
	}
}

static ssize_t dwrite(struct disk *disk, struct uio *uio)
{
	struct ata_device *device = disk->userdata;
	switch (device->type)
	{
		case IDE_ATA:
			return ata_write(device, uio);
		case IDE_ATAPI:
			return -EINVAL;
		default:
			return -EINVAL;
	}
}

static int sysnode_open(struct file *file, struct node *node)
{
	file->userdata = node->userdata;
	return 0;
}

static ssize_t sysnode_read(struct file *file, struct uio *uio)
{
	struct ata_device *device = file->userdata;
	size_t count = uio->count;
	off_t off = uio->off;
	uprintf(uio, "serial: %.*s\n",
	        (int)sizeof(device->identify.serial_number),
	       device->identify.serial_number);
	uprintf(uio, "firmware: %.*s\n",
	        (int)sizeof(device->identify.firmware_revision),
	        device->identify.firmware_revision);
	uprintf(uio, "model: %.*s\n",
	        (int)sizeof(device->identify.model_number),
	        device->identify.model_number);
	uio->off = off + count - uio->count;
	return count - uio->count;
}

int init_channel_maps(struct ata_channel *channel, uint32_t base,
                      uint32_t ctrl, uint32_t bmide, int secondary)
{
	int ret = pci_map(&channel->base, base, 8, 0);
	if (ret)
	{
		printf("ata: base map failed\n");
		return ret;
	}
	ret = pci_map(&channel->ctrl, ctrl, 4, 0);
	if (ret)
	{
		printf("ata: control map failed\n");
		return ret;
	}
	channel->flags |= ATA_CHANNEL_EXISTS;
	if (bmide)
	{
		ret = pci_map(&channel->bmide, bmide, 8, secondary * 8);
		if (ret)
		{
			printf("ata: bmide map failed\n");
			return ret;
		}
		channel->flags |= ATA_CHANNEL_BMIDE;
	}
	return 0;
}

int init_device(struct ata *ata, struct ata_device *device,
                uint8_t id, struct ata_channel *channel)
{
	uint8_t err = 0;
	uint8_t type = IDE_ATA;
	uint8_t status;

	BASE_WU8(channel, ATA_BASE_HDSEL, 0xA0 | (id << 4));
	io_wait();
	BASE_WU8(channel, ATA_BASE_COMMAND, ATA_CMD_IDENTIFY_DEVICE);
	io_wait();
	if (!BASE_RU8(channel, ATA_BASE_STATUS))
		return -EAGAIN;
	while (1)
	{
		status = BASE_RU8(channel, ATA_BASE_STATUS);
		if ((status & ATA_SR_ERR))
		{
			err = 1;
			break;
		}
		if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
			break;
	}
	if (err)
	{
		uint8_t cl = BASE_RU8(channel, ATA_BASE_LBALO);
		uint8_t ch = BASE_RU8(channel, ATA_BASE_LBAMID);
		if (cl == 0x14 && ch == 0xEB)
			type = IDE_ATAPI;
		else if (cl == 0x69 && ch == 0x96)
			type = IDE_ATAPI;
		else
			return -EAGAIN;

		BASE_WU8(channel, ATA_BASE_COMMAND, ATA_CMD_IDENTIFY_PACKET_DEVICE);
		io_wait();
	}
	pci_ru32v(&channel->base, ATA_BASE_DATA, (uint32_t*)&device->identify, 128);
	reorder_string(device->identify.serial_number, sizeof(device->identify.serial_number));
	reorder_string(device->identify.firmware_revision, sizeof(device->identify.firmware_revision));
	reorder_string(device->identify.model_number, sizeof(device->identify.model_number));
	device->id = ata->devices_count;
	device->type = type;
	device->channel = channel;
	device->drive = id;
	if (!device->identify.capabilities.lba_supported)
		panic("ata: lba not supported\n");
	if (device->identify.command_set_support.big_lba)
		device->size = device->identify.max_48bit_lba[0];
	else
		device->size = device->identify.user_addressable_sectors;
	ata->devices_count++;
	return 0;
}

int init_channel(struct ata *ata, struct ata_channel *channel,
                 uint8_t id, uint8_t pci_native)
{
	int ret;

#if 1
	channel->flags |= ATA_CHANNEL_INT;
#endif
	mutex_init(&channel->mutex, 0);
	waitq_init(&channel->waitq);
	spinlock_init(&channel->waitq_sl);
	ret = pm_alloc_page(&channel->buf_page);
	if (ret)
	{
		printf("ata: failed to allocate buffer page\n");
		return ret;
	}
	channel->buf = vm_map(channel->buf_page, PAGE_SIZE, VM_PROT_RW);
	if (!channel->buf)
	{
		printf("ata: failed to map buffer page\n");
		return -ENOMEM;
	}
	ret = pm_alloc_page(&channel->prdt_page);
	if (ret)
	{
		printf("ata: failed to allocate prdt page\n");
		return ret;
	}
	channel->prdt = vm_map(channel->prdt_page, PAGE_SIZE, VM_PROT_RW);
	if (!channel->prdt)
	{
		printf("ata: failed to map prdt page\n");
		return -ENOMEM;
	}
	CTRL_WU8(channel, ATA_CTRL_CONTROL, 2);
	for (uint8_t i = 0; i < 2; ++i)
	{
		struct ata_device *device = &ata->devices[ata->devices_count];
		ret = init_device(ata, device, i, channel);
		if (ret == -EAGAIN)
			continue;
		if (ret)
			return ret;
	}
	if (channel->flags & ATA_CHANNEL_INT)
	{
		if (pci_native & (1 << id))
		{
			panic("ata: unhandled pci native int\n");
		}
		else
		{
			ret = register_isa_irq(id ? ISA_IRQ_ATA2 : ISA_IRQ_ATA1,
			                       int_handler, channel,
			                       &channel->irq_handle);
			if (ret)
			{
				printf("ata: isa IRQ setup failed\n");
				return ret;
			}
		}
		CTRL_WU8(channel, ATA_CTRL_CONTROL, 0);
	}
	return 0;
}

int init_dev(const uint32_t *bars, uint8_t pci_native)
{
	struct ata *ata;
	int ret;

	ata = malloc(sizeof(*ata), M_ZERO);
	if (!ata)
	{
		printf("ata: failed to allocate ata\n");
		return -ENOMEM;
	}
	if (bars[0] && bars[1])
	{
		ret = init_channel_maps(&ata->channels[ATA_PRIMARY],
		                        bars[0], bars[1], bars[4], 0);
		if (ret)
			return ret;
	}
	if (bars[2] && bars[3])
	{
		ret = init_channel_maps(&ata->channels[ATA_SECONDARY],
		                        bars[2], bars[3], bars[4], 1);
		if (ret)
			return ret;
	}
	for (uint8_t i = 0; i < 2; ++i)
	{
		struct ata_channel *channel = &ata->channels[i];
		if (!(channel->flags & ATA_CHANNEL_EXISTS))
			continue;
		init_channel(ata, channel, i, pci_native);
	}
	for (size_t i = 0; i < ata->devices_count; ++i)
	{
		struct ata_device *device = &ata->devices[i];
		ret = disk_new("ata", makedev(3, device->id * 64),
		               device->size * BLOCK_SIZE, &dop,
		               &device->disk);
		if (ret)
			panic("ata: failed to create disk: %s\n", strerror(ret));
		device->disk->userdata = device;
		ret = disk_load(device->disk);
		if (ret)
			panic("ata: failed to load disk: %s\n", strerror(ret));
		ret = sysfs_mknode(device->disk->name, 0, 0, 0400, &fop,
		                   &device->sysfs_node);
		if (ret)
			printf("ata: failed to create sysfs node: %s\n", strerror(ret));
		else
			device->sysfs_node->userdata = device;
	}
	return 0;
}

int init_isa(void)
{
	static const uint32_t bars[] =
	{
		ATA_PRIMARY_PORT_BASE,
		ATA_PRIMARY_PORT_CTRL,
		ATA_SECONDARY_PORT_BASE,
		ATA_SECONDARY_PORT_CTRL,
		0,
	};
	return init_dev(bars, 0);
}

int init_pci(struct pci_device *device, void *userdata)
{
	int *found = userdata;
	uint32_t bars[5];
	uint8_t pci_native = 0;
	if (device->header.progif & 0x80)
	{
		pci_enable_bus_mastering(device);
		bars[4] = device->header0.bar4;
	}
	else
	{
		bars[4] = 0;
	}
	if (device->header.progif & 0x01)
	{
		bars[0] = device->header0.bar0;
		bars[1] = device->header0.bar1;
		pci_native |= 0x1;
	}
	else
	{
		bars[0] = ATA_PRIMARY_PORT_BASE;
		bars[1] = ATA_PRIMARY_PORT_CTRL;
	}
	if (device->header.progif & 0x04)
	{
		bars[2] = device->header0.bar2;
		bars[3] = device->header0.bar3;
		pci_native |= 0x2;
	}
	else
	{
		bars[2] = ATA_SECONDARY_PORT_BASE;
		bars[3] = ATA_SECONDARY_PORT_CTRL;
	}
	int ret = init_dev(bars, pci_native);
	if (ret)
		return ret;
	*found = 1;
	return 0;
}

int init(void)
{
	int found = 0;
	pci_probe(0x8086, 0x7010, init_pci, &found);
	pci_probe(0x8086, 0x7111, init_pci, &found);
	if (found)
		return 0;
	return init_isa();
}

void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "ata",
	.init = init,
	.fini = fini,
};
