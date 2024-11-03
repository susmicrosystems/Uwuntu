#include "virtio.h"

#include <random.h>
#include <errno.h>
#include <kmod.h>
#include <std.h>

struct virtio_rng
{
	struct virtio_dev dev;
	struct page *buf_page;
	uint8_t *buf;
	struct waitq waitq;
	struct spinlock waitq_sl;
	uint32_t collect_len;
};

static void on_msg(struct virtq *queue, uint16_t id, uint32_t len)
{
	(void)id;
	struct virtio_rng *rng = (struct virtio_rng*)queue->dev;
	spinlock_lock(&rng->waitq_sl);
	rng->collect_len = len;
	waitq_signal(&rng->waitq, 0);
	spinlock_unlock(&rng->waitq_sl);
}

static ssize_t random_collect(void *buf, size_t size, void *userdata)
{
	struct virtio_rng *rng = userdata;
	if (size > PAGE_SIZE)
		size = PAGE_SIZE;
	struct virtq_buf vq_buf;
	vq_buf.addr = pm_page_addr(rng->buf_page);
	vq_buf.size = size;
	int ret = virtq_send(&rng->dev.queues[0], &vq_buf, 0, 1);
	if (ret < 0)
		return ret;
	virtq_notify(&rng->dev.queues[0]);
	/* XXX should not sleep */
	spinlock_lock(&rng->waitq_sl);
	ret = waitq_wait_head(&rng->waitq, &rng->waitq_sl, NULL);
	spinlock_unlock(&rng->waitq_sl);
	if (ret)
		return ret;
	memcpy(buf, rng->buf, size);
	return size;
}

static void virtio_rng_delete(struct virtio_rng *rng)
{
	if (!rng)
		return;
	if (rng->buf)
		vm_unmap(rng->buf, PAGE_SIZE);
	if (rng->buf_page)
		pm_free_page(rng->buf_page);
	virtio_dev_destroy(&rng->dev);
	waitq_destroy(&rng->waitq);
	spinlock_destroy(&rng->waitq_sl);
	free(rng);
}

int init_pci(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct virtio_rng *rng = malloc(sizeof(*rng), M_ZERO);
	if (!rng)
	{
		printf("virtio_rng: allocation failed\n");
		return -ENOMEM;
	}
	waitq_init(&rng->waitq);
	spinlock_init(&rng->waitq_sl);
	uint8_t features[1];
	int ret = virtio_dev_init(&rng->dev, device, features, 0);
	if (ret)
	{
		virtio_rng_delete(rng);
		return ret;
	}
	if (rng->dev.queues_nb < 1)
	{
		printf("virtio_rng: no queues\n");
		virtio_rng_delete(rng);
		return -EINVAL;
	}
	ret = pm_alloc_page(&rng->buf_page);
	if (ret)
	{
		printf("virtio_rng: failed to allocate page\n");
		virtio_rng_delete(rng);
		return ret;
	}
	rng->buf = vm_map(rng->buf_page, PAGE_SIZE, VM_PROT_RW);
	if (!rng->buf)
	{
		printf("virtio_rng: failed to map page\n");
		virtio_rng_delete(rng);
		return -ENOMEM;
	}
	rng->dev.queues[0].on_msg = on_msg;
	ret = virtq_setup_irq(&rng->dev.queues[0]);
	if (ret)
	{
		printf("virtio_rng: failed to setup irq\n");
		virtio_rng_delete(rng);
		return ret;
	}
	virtio_dev_init_end(&rng->dev);
	random_register(random_collect, rng);
	return 0;
}

static int init(void)
{
	pci_probe(0x1AF4, 0x1005, init_pci, NULL);
	return 0;
}

static void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "virtio_rng",
	.init = init,
	.fini = fini,
};
