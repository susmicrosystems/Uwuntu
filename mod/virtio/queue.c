#include "virtio.h"

#include <errno.h>
#include <time.h>
#include <std.h>

#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2
#define VIRTQ_DESC_F_INDIRECT 3

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

#define VIRTQ_USED_F_NO_NOTIFY 1

struct virtq_desc
{
	uint64_t addr;
	uint32_t size;
	uint16_t flags;
	uint16_t next;
};

struct virtq_avail
{
	uint16_t flags;
	uint16_t index;
	uint16_t ring[];
};

struct virtq_used_elem
{
	uint32_t id;
	uint32_t len;
};

struct virtq_used
{
	uint16_t flags;
	uint16_t index;
	struct virtq_used_elem ring[];
};

static inline void print(struct uio *uio, struct pci_map *cmn_cfg)
{
	uprintf(uio, "queue_size: 0x%" PRIx16 "\n",
	        pci_ru16(cmn_cfg, VIRTIO_C_QUEUE_SIZE));
	uprintf(uio, "queue_msix_vector: 0x%" PRIx16 "\n",
	        pci_ru16(cmn_cfg, VIRTIO_C_QUEUE_MSIX_VECTOR));
	uprintf(uio, "queue_enable: 0x%" PRIx16 "\n",
	        pci_ru16(cmn_cfg, VIRTIO_C_QUEUE_ENABLE));
	uprintf(uio, "queue_notify_off: 0x%" PRIx16 "\n",
	        pci_ru16(cmn_cfg, VIRTIO_C_QUEUE_NOTIFY_OFF));
	uprintf(uio, "queue_desc: 0x%" PRIx64 "\n",
	        pci_ru64(cmn_cfg, VIRTIO_C_QUEUE_DESC));
	uprintf(uio, "queue_driver: 0x%" PRIx64 "\n",
	        pci_ru64(cmn_cfg, VIRTIO_C_QUEUE_DRIVER));
	uprintf(uio, "queue_device: 0x%" PRIx64 "\n",
	        pci_ru64(cmn_cfg, VIRTIO_C_QUEUE_DEVICE));
}

int virtq_poll(struct virtq *queue, uint16_t *id, uint32_t *len)
{
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	uint16_t index = queue->used->index % queue->size;
	if (queue->used_tail == index)
		return -EAGAIN;
	struct virtq_used_elem *elem = &queue->used->ring[queue->used_tail];
	*id = elem->id;
	*len = elem->len;
	queue->used_tail = (queue->used_tail + 1) % queue->size;
	return 0;
}

void virtq_on_irq(struct virtq *queue)
{
	if (!queue->on_msg)
		return;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	uint16_t index = queue->used->index % queue->size;
	while (queue->used_tail != index)
	{
		struct virtq_used_elem *elem = &queue->used->ring[queue->used_tail];
		queue->on_msg(queue, elem->id, elem->len);
		queue->used_tail = (queue->used_tail + 1) % queue->size;
	}
}

static void int_handler(void *userptr)
{
	struct virtq *queue = userptr;
	virtq_on_irq(queue);
}

int virtq_setup_irq(struct virtq *queue)
{
	uint16_t vector;
	if (queue->dev->irq_handle.type == IRQ_MSIX)
	{
		int ret = register_pci_irq(queue->dev->device, int_handler,
		                           queue, &queue->irq_handle);
		if (ret)
			return ret;
		vector = queue->irq_handle.msix.vector;
	}
	else
	{
		vector = 0xFFFF;
	}
	pci_wu16(&queue->dev->common_cfg, VIRTIO_C_QUEUE_SELECT, queue->id);
	pci_wu16(&queue->dev->common_cfg, VIRTIO_C_QUEUE_MSIX_VECTOR, vector);
	return 0;
}

void virtq_notify(struct virtq *queue)
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
	pci_wu32(&queue->dev->notify_cfg,
	         queue->dev->notify_multiplier * queue->id,
	         queue->id);
}

int virtq_init(struct virtq *queue, struct virtio_dev *dev, uint16_t id)
{
	queue->id = id;
	queue->dev = dev;
	pci_wu16(&dev->common_cfg, VIRTIO_C_QUEUE_SELECT, id);
#if 0
	print(NULL, &dev->common_cfg);
#endif
	queue->size = pci_ru16(&dev->common_cfg, VIRTIO_C_QUEUE_SIZE);
	if (!queue->size)
	{
		printf("virtq: empty queue\n");
		return -EINVAL;
	}
	if (queue->size > 0x100)
	{
		pci_wu16(&dev->common_cfg, VIRTIO_C_QUEUE_SIZE, 0x100);
		queue->size = 0x100;
	}
	int ret = pm_alloc_page(&queue->desc_page);
	if (ret)
	{
		printf("virtq: failed to alloc desc page\n");
		return ret;
	}
	queue->desc = vm_map(queue->desc_page, PAGE_SIZE, VM_PROT_RW);
	if (!queue->desc)
	{
		printf("virtq: failed to map desc page\n");
		return -ENOMEM;
	}
	memset(queue->desc, 0, PAGE_SIZE);
	pci_wu64(&dev->common_cfg, VIRTIO_C_QUEUE_DESC,
	         pm_page_addr(queue->desc_page));
	ret = pm_alloc_page(&queue->avail_page);
	if (ret)
	{
		printf("virtq: failed to alloc avail page\n");
		return ret;
	}
	queue->avail = vm_map(queue->avail_page, PAGE_SIZE, VM_PROT_RW);
	if (!queue->avail)
	{
		printf("virtq: failed to map avail page\n");
		return -ENOMEM;
	}
	memset(queue->avail, 0, PAGE_SIZE);
	pci_wu64(&dev->common_cfg, VIRTIO_C_QUEUE_DRIVER,
	         pm_page_addr(queue->avail_page));
	ret = pm_alloc_page(&queue->used_page);
	if (ret)
	{
		printf("virtq: failed to alloc used page\n");
		return ret;
	}
	queue->used = vm_map(queue->used_page, PAGE_SIZE, VM_PROT_RW);
	if (!queue->used)
	{
		printf("virtq: failed to map used page\n");
		return -ENOMEM;
	}
	memset(queue->used, 0, PAGE_SIZE);
	pci_wu64(&dev->common_cfg, VIRTIO_C_QUEUE_DEVICE,
	         pm_page_addr(queue->used_page));
	pci_wu16(&dev->common_cfg, VIRTIO_C_QUEUE_ENABLE, 1);
	queue->avail->flags = 0;
	queue->used->flags = VIRTQ_USED_F_NO_NOTIFY;
#if 0
	print(NULL, &dev->common_cfg);
#endif
	return 0;
}

void virtq_destroy(struct virtq *queue)
{
	if (!queue->size)
		return;
	vm_unmap(queue->desc, PAGE_SIZE);
	vm_unmap(queue->avail, PAGE_SIZE);
	vm_unmap(queue->used, PAGE_SIZE);
	pm_free_page(queue->desc_page);
	pm_free_page(queue->avail_page);
	pm_free_page(queue->used_page);
	if (queue->dev->irq_handle.type != IRQ_MSIX)
		unregister_irq(&queue->irq_handle);
}

int virtq_send(struct virtq *queue, const struct virtq_buf *bufs,
               size_t nread, size_t nwrite)
{
	size_t total = nread + nwrite;
	uint16_t base = queue->desc_head;
	for (size_t i = 0; i < total; ++i)
	{
		struct virtq_desc *desc = &queue->desc[queue->desc_head];
		desc->addr = bufs[i].addr;
		desc->size = bufs[i].size;
		desc->flags = i < nread ? 0 : VIRTQ_DESC_F_WRITE;
		uint16_t next = (queue->desc_head + 1) % queue->size;
		if (i != total - 1)
		{
			desc->next = next;
			desc->flags |= VIRTQ_DESC_F_NEXT;
		}
		else
		{
			desc->next = 0;
		}
		queue->desc_head = next;
	}
	queue->avail->ring[queue->avail->index % queue->size] = base;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	queue->avail->index++;
	return 0;
}
