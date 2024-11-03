#include "virtio.h"

#include <errno.h>
#include <kmod.h>
#include <file.h>
#include <stat.h>
#include <pci.h>
#include <vfs.h>
#include <uio.h>
#include <tty.h>
#include <fb.h>

#define VIRTIO_GPU_F_VIRGL         0
#define VIRTIO_GPU_F_EDID          1
#define VIRTIO_GPU_F_RESOURCE_UUID 2
#define VIRTIO_GPU_F_RESOURCE_BLOB 3
#define VIRTIO_GPU_F_CONTEXT_INIT  4

#define VIRTIO_GPU_C_EVENTS_READ  0x00
#define VIRTIO_GPU_C_EVENTS_WRITE 0x04
#define VIRTIO_GPU_C_NUM_SCANOUTS 0x08
#define VIRTIO_GPU_C_NUM_CAPSETS  0x0C

#define VIRTIO_GPU_FLAG_FENCE         (1 << 0)
#define VIRTIO_GPU_FLAG_INFO_RING_IDX (1 << 1)

#define VIRTIO_GPU_MAX_SCANOUTS 16

#define VIRTIO_GPU_CAPSET_VIRGL        1
#define VIRTIO_GPU_CAPSET_VIRGL2       2
#define VIRTIO_GPU_CAPSET_GFXSTREAM    3
#define VIRTIO_GPU_CAPSET_VENUS        4
#define VIRTIO_GPU_CAPSET_CROSS_DOMAIN 5

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

enum virtio_gpu_ctrl_type
{
	/* 2d commands */
	VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
	VIRTIO_GPU_CMD_RESOURCE_UNREF,
	VIRTIO_GPU_CMD_SET_SCANOUT,
	VIRTIO_GPU_CMD_RESOURCE_FLUSH,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
	VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
	VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
	VIRTIO_GPU_CMD_GET_CAPSET_INFO,
	VIRTIO_GPU_CMD_GET_CAPSET,
	VIRTIO_GPU_CMD_GET_EDID,
	VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
	VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

	/* 3d commands */
	VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
	VIRTIO_GPU_CMD_CTX_DESTROY,
	VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
	VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
	VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
	VIRTIO_GPU_CMD_SUBMIT_3D,
	VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
	VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

	/* cursor commands */
	VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
	VIRTIO_GPU_CMD_MOVE_CURSOR,

	/* success responses */
	VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
	VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET,
	VIRTIO_GPU_RESP_OK_EDID,
	VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
	VIRTIO_GPU_RESP_OK_MAP_INFO,

	/* error responses */
	VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
	VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
	VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

enum virtio_gpu_formats
{
	VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,
	VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
	VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
	VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,

	VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
	VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68,

	VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM = 121,
	VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM = 134,
};

struct virtio_gpu_rect
{
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

struct virtio_gpu_ctrl_hdr
{
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	uint32_t ctx_id;
	uint8_t ring_idx;
	uint8_t padding[3];
};

struct virtio_gpu_resp_display_info
{
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_display_one
	{
		struct virtio_gpu_rect r;
		uint32_t enabled;
		uint32_t flags;
	} pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};

struct virtio_gpu_get_edid
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t scanout;
	uint32_t padding;
};

struct virtio_gpu_resp_edid
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
	uint8_t edid[1024];
};

struct virtio_gpu_resource_create_2d
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

struct virtio_gpu_resource_unref
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

struct virtio_gpu_set_scanout
{
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint32_t scanout_id;
	uint32_t resource_id;
};

struct virtio_gpu_resource_flush
{
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint32_t resource_id;
	uint32_t padding;
};

struct virtio_gpu_transfer_to_host_2d
{
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t padding;
};

struct virtio_gpu_resource_attach_backing
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t nr_entries;
};

struct virtio_gpu_mem_entry
{
	uint64_t addr;
	uint32_t length;
	uint32_t padding;
};

struct virtio_gpu_resource_detach_backing
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
};

struct virtio_gpu_get_capset_info
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_index;
	uint32_t padding;
};

struct virtio_gpu_resp_capset_info
{
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t capset_id;
	uint32_t capset_max_version;
	uint32_t capset_max_size;
	uint32_t padding;
};

struct framebuffer
{
	struct page **pages;
	size_t pages_count;
	size_t size;
	uint32_t id;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

struct virtio_gpu
{
	struct virtio_dev dev;
	struct pci_map gpu_cfg;
	struct mutex mutex;
	struct page *buf_pages[10]; /* XXX less arbitrary */
	uint8_t *buf;
	struct virtio_gpu_resp_display_info display_info;
	struct framebuffer framebuffer;
	uint32_t resource_id;
	struct fb *fb;
	struct tty *tty;
};

static int synchronous_request(struct virtio_gpu *gpu, struct virtq *queue,
                               struct virtq_buf *bufs,
                               int nrequest, int nreply)
{
	(void)gpu;
	int ret = virtq_send(queue, bufs, nrequest, nreply);
	if (ret < 0)
	{
		printf("virtio_gpu: failed to send request: %s\n",
		       strerror(ret));
		return ret;
	}
	virtq_notify(queue);
	while (1)
	{
		/* XXX timeout ? */
		uint16_t id;
		uint32_t len;
		ret = virtq_poll(queue, &id, &len);
		if (ret != -EAGAIN)
			break;
	}
	return ret;
}

int cmd_get_display_info(struct virtio_gpu *gpu,
                         struct virtio_gpu_resp_display_info *display_info)
{
	int ret;
	struct virtq_buf bufs[2];
	struct virtio_gpu_ctrl_hdr *request;
	struct virtio_gpu_resp_display_info *reply;

	mutex_lock(&gpu->mutex);
	request = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[0];
	reply = (struct virtio_gpu_resp_display_info*)&gpu->buf[sizeof(*request)];
	memset(request, 0, sizeof(*request));
	request->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		goto end;
	if (reply->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_GET_DISPLAY_INFO response: 0x%" PRIx32 "\n",
		       display_info->hdr.type);
		ret = -EXDEV;
		goto end;
	}
	memcpy(display_info, reply, sizeof(*display_info));
	ret = 0;

end:
	mutex_unlock(&gpu->mutex);
	return ret;
}

int cmd_resource_create_2d(struct virtio_gpu *gpu, uint32_t id, uint32_t format,
                           uint32_t width, uint32_t height)
{
	int ret;
	struct virtq_buf bufs[2];
	struct virtio_gpu_resource_create_2d *request;
	struct virtio_gpu_ctrl_hdr *reply;

	mutex_lock(&gpu->mutex);
	request = (struct virtio_gpu_resource_create_2d*)&gpu->buf[0];;
	reply = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[sizeof(*request)];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	request->resource_id = id;
	request->format = format;
	request->width = width;
	request->height = height;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		goto end;
	if (reply->type != VIRTIO_GPU_RESP_OK_NODATA)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_RESOURCE_CREATE_2D response: 0x%" PRIx32 "\n",
		       reply->type);
		ret = -EXDEV;
		goto end;
	}
	ret = 0;

end:
	mutex_unlock(&gpu->mutex);
	return ret;
}

int cmd_resource_unref(struct virtio_gpu *gpu, uint32_t id)
{
	struct virtq_buf bufs[2];
	struct virtio_gpu_resource_unref *request;
	struct virtio_gpu_ctrl_hdr *reply;
	int ret;

	mutex_lock(&gpu->mutex);
	request = (struct virtio_gpu_resource_unref*)&gpu->buf[0];
	reply = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[sizeof(*request)];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
	request->resource_id = id;
	request->padding = 0;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		return ret;
	if (reply->type != VIRTIO_GPU_RESP_OK_NODATA)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_RESOURCE_UNREF response: 0x%" PRIx32 "\n",
		       reply->type);
		ret = -EXDEV;
		goto end;
	}
	ret = 0;

end:
	mutex_unlock(&gpu->mutex);
	return ret;
}

int cmd_resource_attach_backing(struct virtio_gpu *gpu, uint32_t id,
                                struct page **pages, size_t pages_count)
{
	int ret;
	struct virtq_buf bufs[2];
	struct virtio_gpu_resource_attach_backing *request;
	struct virtio_gpu_ctrl_hdr *reply;
	size_t reply_offset = sizeof(*request) + sizeof(struct virtio_gpu_mem_entry) * pages_count;

	if (reply_offset + sizeof(*reply) >= PAGE_SIZE * sizeof(gpu->buf_pages) / sizeof(*gpu->buf_pages))
	{
		printf("virtio_gpu: request too big\n");
		return -ENOMEM;
	}
	mutex_lock(&gpu->mutex);
	request = (struct virtio_gpu_resource_attach_backing*)&gpu->buf[0];
	reply = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[reply_offset];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	request->resource_id = id;
	request->nr_entries = pages_count;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	for (size_t i = 0; i < pages_count; ++i)
	{
		struct virtio_gpu_mem_entry *mem_entry = (struct virtio_gpu_mem_entry*)&gpu->buf[sizeof(*request) + i * sizeof(*mem_entry)];
		mem_entry->addr = pm_page_addr(pages[i]);
		mem_entry->length = PAGE_SIZE;
		mem_entry->padding = 0;
		bufs[0].size += sizeof(*mem_entry);
	}
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		goto end;
	if (reply->type != VIRTIO_GPU_RESP_OK_NODATA)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING response: 0x%" PRIx32 "\n",
		       reply->type);
		ret = -EXDEV;
		goto end;
	}
	ret = 0;

end:
	mutex_unlock(&gpu->mutex);
	return ret;
}

int cmd_set_scanout(struct virtio_gpu *gpu,
                    uint32_t scanout, uint32_t id,
                    uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height)
{
	struct virtq_buf bufs[2];
	struct virtio_gpu_set_scanout *request;
	struct virtio_gpu_ctrl_hdr *reply;
	int ret;

	mutex_lock(&gpu->mutex);
	request = (struct virtio_gpu_set_scanout*)&gpu->buf[0];
	reply = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[sizeof(*request)];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	request->r.x = x;
	request->r.y = y;
	request->r.width = width;
	request->r.height = height;
	request->scanout_id = scanout;
	request->resource_id = id;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		goto end;
	if (reply->type != VIRTIO_GPU_RESP_OK_NODATA)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_SET_SCANOUT response: 0x%" PRIx32 "\n",
		       reply->type);
		ret = -EXDEV;
		goto end;
	}
	ret = 0;

end:
	mutex_unlock(&gpu->mutex);
	return ret;
}

int cmd_transfer_to_host_2d(struct virtio_gpu *gpu, uint32_t id, uint64_t offset,
                            uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height)
{
	struct virtq_buf bufs[2];
	struct virtio_gpu_transfer_to_host_2d *request;
	struct virtio_gpu_ctrl_hdr *reply;
	int ret;

	mutex_lock(&gpu->mutex);
	request = (struct virtio_gpu_transfer_to_host_2d*)&gpu->buf[0];
	reply = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[sizeof(*request)];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	request->r.x = x;
	request->r.y = y;
	request->r.width = width;
	request->r.height = height;
	request->offset = offset;
	request->resource_id = id;
	request->padding = 0;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		goto end;
	if (reply->type != VIRTIO_GPU_RESP_OK_NODATA)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D response: 0x%" PRIx32 "\n",
		       reply->type);
		ret = -EXDEV;
		goto end;
	}
	ret = 0;

end:
	mutex_unlock(&gpu->mutex);
	return ret;
}

int cmd_resource_flush(struct virtio_gpu *gpu, uint32_t id,
                       uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height)
{
	struct virtq_buf bufs[2];
	struct virtio_gpu_resource_flush *request;
	struct virtio_gpu_ctrl_hdr *reply;
	int ret;

	request = (struct virtio_gpu_resource_flush*)&gpu->buf[0];
	reply = (struct virtio_gpu_ctrl_hdr*)&gpu->buf[sizeof(*request)];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	request->r.x = x;
	request->r.y = y;
	request->r.width = width;
	request->r.height = height;
	request->resource_id = id;
	request->padding = 0;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		return ret;
	if (reply->type != VIRTIO_GPU_RESP_OK_NODATA)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_RESOURCE_FLUSH response: 0x%" PRIx32 "\n",
		       reply->type);
		return -EXDEV;
	}
	return 0;
}

int cmd_get_capset_info(struct virtio_gpu *gpu, uint32_t id,
                        struct virtio_gpu_resp_capset_info *info)
{
	struct virtq_buf bufs[2];
	struct virtio_gpu_get_capset_info *request;
	struct virtio_gpu_resp_capset_info *reply;
	int ret;

	request = (struct virtio_gpu_get_capset_info*)&gpu->buf[0];
	reply = (struct virtio_gpu_resp_capset_info*)&gpu->buf[sizeof(*request)];
	memset(&request->hdr, 0, sizeof(request->hdr));
	request->hdr.type = VIRTIO_GPU_CMD_GET_CAPSET_INFO;
	request->capset_index = id;
	request->padding = 0;
	bufs[0].addr = pm_page_addr(gpu->buf_pages[0]);
	bufs[0].size = sizeof(*request);
	bufs[1].addr = bufs[0].addr + bufs[0].size;
	bufs[1].size = sizeof(*reply);
	ret = synchronous_request(gpu, &gpu->dev.queues[0], bufs, 1, 1);
	if (ret < 0)
		return ret;
	if (reply->hdr.type != VIRTIO_GPU_RESP_OK_CAPSET_INFO)
	{
		printf("virtio_gpu: invalid VIRTIO_GPU_CMD_GET_CAPSET_INFO response: 0x%" PRIx32 "\n",
		       reply->hdr.type);
		return -EXDEV;
	}
	memcpy(info, reply, sizeof(*info));
	return 0;
}

static int gpu_fb_flush(struct fb *fb, uint32_t x, uint32_t y,
                        uint32_t width, uint32_t height)
{
	struct virtio_gpu *gpu = fb->userdata;
	uint32_t right;
	uint32_t bottom;
	int ret;

	if (x >= gpu->fb->width
	 || y >= gpu->fb->height
	 || __builtin_add_overflow(x, width, &right)
	 || right > gpu->fb->width
	 || __builtin_add_overflow(y, height, &bottom)
	 || bottom > gpu->fb->height)
		return -EINVAL;
	ret = cmd_transfer_to_host_2d(gpu, gpu->framebuffer.id,
	                              gpu->fb->pitch * y + gpu->fb->bpp / 8 * x,
	                              x, y, width, height);
	if (ret)
	{
		printf("virtio_gpu: failed to transfer framebuffer memory\n");
		return ret;
	}
	ret = cmd_resource_flush(gpu, gpu->framebuffer.id,
	                         x, y, width, height);
	if (ret)
	{
		printf("virtio_gpu: failed to flush framebuffer\n");
		return ret;
	}
	return 0;
}

const struct fb_op fb_op =
{
	.flush = gpu_fb_flush,
};

int get_resource_id(struct virtio_gpu *gpu, uint32_t *id)
{
	/* XXX bitmap? */
	*id = __atomic_add_fetch(&gpu->resource_id, 1, __ATOMIC_SEQ_CST);
	return 0;
}

void framebuffer_free(struct virtio_gpu *gpu, struct framebuffer *fb)
{
	int ret;

	if (fb->id)
	{
		ret = cmd_resource_unref(gpu, fb->id);
		if (ret)
			printf("virtio_gpu: failed to unref resource\n");
	}
	if (fb->pages)
	{
		for (size_t i = 0; i < fb->pages_count; ++i)
			pm_free_page(fb->pages[i]);
		free(fb->pages);
	}
}

int framebuffer_alloc(struct virtio_gpu *gpu, struct framebuffer *fb,
                      uint32_t format, uint32_t width, uint32_t height)
{
	uint32_t id;
	int ret;

	ret = get_resource_id(gpu, &id);
	if (ret)
	{
		printf("virtio_gpu: resource id allocation failed\n");
		return ret;
	}
	ret = cmd_resource_create_2d(gpu, id, format, width, height);
	if (ret)
	{
		printf("virtio_gpu: resource creation failed\n");
		return ret;
	}
	fb->id = id;
	fb->format = format;
	fb->width = width;
	fb->height = height;
	fb->size = width * height * 4; /* XXX bpp */
	fb->size += PAGE_SIZE - 1;
	fb->size -= fb->size % PAGE_SIZE;
	fb->pages_count = fb->size / PAGE_SIZE;
	fb->pages = malloc(sizeof(*fb->pages) * fb->pages_count, M_ZERO);
	if (!fb->pages)
	{
		printf("virtio_gpu: framebuffer pages allocation failed\n");
		framebuffer_free(gpu, fb);
		return ret;
	}
	for (size_t i = 0; i < fb->pages_count; ++i)
	{
		ret = pm_alloc_page(&fb->pages[i]);
		if (ret)
		{
			printf("virtio_gpu: framebuffer page allocation failed\n");
			framebuffer_free(gpu, fb);
			return ret;
		}
	}
	ret = cmd_resource_attach_backing(gpu, fb->id, fb->pages, fb->pages_count);
	if (ret)
	{
		printf("virtio_gpu: failed to attach framebuffer resources\n");
		framebuffer_free(gpu, fb);
		return ret;
	}
	return 0;
}

void print_gpu_cfg(struct uio *uio, struct pci_map *gpu_cfg)
{
	uprintf(uio, "num scanouts: %" PRIu32 "\n",
	        pci_ru32(gpu_cfg, VIRTIO_GPU_C_NUM_SCANOUTS));
	uprintf(uio, "num capsets: %" PRIu32 "\n",
	        pci_ru32(gpu_cfg, VIRTIO_GPU_C_NUM_CAPSETS));
}

void print_display_info(struct uio *uio,
                        struct virtio_gpu_resp_display_info *display_info)
{
	uprintf(uio, "x: %" PRIu32 "\n", display_info->pmodes[0].r.x);
	uprintf(uio, "y: %" PRIu32 "\n", display_info->pmodes[0].r.y);
	uprintf(uio, "width: %" PRIu32 "\n", display_info->pmodes[0].r.width);
	uprintf(uio, "height: %" PRIu32 "\n", display_info->pmodes[0].r.height);
	uprintf(uio, "enabled: %" PRIu32 "\n", display_info->pmodes[0].enabled);
	uprintf(uio, "flags: %" PRIx32 "\n", display_info->pmodes[0].flags);
}

void virtio_gpu_delete(struct virtio_gpu *gpu)
{
	if (!gpu)
		return;
	fb_free(gpu->fb);
	framebuffer_free(gpu, &gpu->framebuffer);
	pci_unmap(&gpu->gpu_cfg);
	virtio_dev_destroy(&gpu->dev);
	mutex_destroy(&gpu->mutex);
	free(gpu);
}

int init_pci(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct virtio_gpu *gpu = malloc(sizeof(*gpu), M_ZERO);
	if (!gpu)
	{
		printf("virtio_gpu: allocation failed\n");
		return -ENOMEM;
	}
	mutex_init(&gpu->mutex, 0);
	uint8_t features[(VIRTIO_F_RING_RESET + 7) / 8];
	memset(features, 0, sizeof(features));
	int ret = virtio_dev_init(&gpu->dev, device, features, VIRTIO_F_RING_RESET);
	if (ret)
		goto err;
	if (gpu->dev.queues_nb < 2)
	{
		printf("virtio_gpu: no queues\n");
		ret = -EINVAL;
		goto err;
	}
	ret = virtio_get_cfg(device, VIRTIO_PCI_CAP_DEVICE_CFG,
	                     &gpu->gpu_cfg, 0x40, NULL);
	if (ret)
		goto err;
	if (pci_ru32(&gpu->gpu_cfg, VIRTIO_GPU_C_NUM_SCANOUTS) < 1)
	{
		printf("virtio_gpu: no scanouts\n");
		ret = -EXDEV;
		goto err;
	}
#if 0
	print_gpu_cfg(NULL, &gpu->gpu_cfg);
#endif
	ret = pm_alloc_pages(gpu->buf_pages, sizeof(gpu->buf_pages) / sizeof(*gpu->buf_pages));
	if (ret)
	{
		printf("virtio_gpu: failed to allocate page\n");
		goto err;
	}
	gpu->buf = vm_map(gpu->buf_pages[0], PAGE_SIZE * sizeof(gpu->buf_pages) / sizeof(*gpu->buf_pages), VM_PROT_RW);
	if (!gpu->buf)
	{
		printf("virtio_gpu: failed to map page\n");
		ret = -ENOMEM;
		goto err;
	}
	virtio_dev_init_end(&gpu->dev);
	ret = cmd_get_display_info(gpu, &gpu->display_info);
	if (ret)
	{
		printf("virtio_gpu: failed to get display info\n");
		goto err;
	}
	ret = framebuffer_alloc(gpu, &gpu->framebuffer,
	                        VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
	                        gpu->display_info.pmodes[0].r.width,
	                        gpu->display_info.pmodes[0].r.height);
	if (ret)
	{
		printf("virtio_gpu: failed to create framebuffer\n");
		goto err;
	}
	ret = cmd_set_scanout(gpu, 0, gpu->framebuffer.id, 0, 0,
	                      gpu->framebuffer.width,
	                      gpu->framebuffer.height);
	if (ret)
	{
		printf("virtio_gpu: failed to set scanout\n");
		goto err;
	}
	ret = fb_alloc(&fb_op, &gpu->fb);
	if (ret)
	{
		printf("virtio_gpu: failed to create fb\n");
		goto err;
	}
	ret = fb_update(gpu->fb,
	                gpu->framebuffer.width,
	                gpu->framebuffer.height,
	                FB_FMT_B8G8R8A8,
	                gpu->framebuffer.width * 4,
	                32, gpu->framebuffer.pages,
	                gpu->framebuffer.pages_count,
	                0);
	if (ret)
	{
		printf("virtio_gpu: failed to update fb\n");
		goto err;
	}
	size_t num_capsets = pci_ru32(&gpu->gpu_cfg, VIRTIO_GPU_C_NUM_CAPSETS);
	printf("capsets: %zu\n", num_capsets);
	for (size_t i = 0; i < num_capsets; ++i)
	{
		struct virtio_gpu_resp_capset_info info;
		ret = cmd_get_capset_info(gpu, i, &info);
		if (ret)
		{
			printf("virtio_gpu: failed to get capset info\n");
			goto err;
		}
		printf("capset_id: %" PRIu32 "\n", info.capset_id);
	}
	gpu->fb->userdata = gpu;
	ret = vtty_alloc("tty0", 0, gpu->fb, &gpu->tty);
	if (!ret)
		curtty = gpu->tty;
	else
		printf("virtio_gpu: failed to create tty: %s\n", strerror(ret));
	return 0;

err:
	virtio_gpu_delete(gpu);
	return ret;
}

int init(void)
{
	pci_probe(0x1AF4, 0x1050, init_pci, NULL);
	return 0;
}

void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "virtio_gpu",
	.init = init,
	.fini = fini,
};
