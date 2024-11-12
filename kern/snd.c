#include <random.h>
#include <errno.h>
#include <file.h>
#include <snd.h>
#include <sma.h>
#include <vfs.h>
#include <uio.h>
#include <std.h>
#include <mem.h>

struct sma snd_sma;

static const struct file_op fop;

void snd_init(void)
{
	sma_init(&snd_sma, sizeof(struct snd), NULL, NULL, "snd");
}

int snd_alloc(struct snd **sndp)
{
	struct snd *snd = sma_alloc(&snd_sma, M_ZERO);
	if (!snd)
		return -ENOMEM;
	refcount_init(&snd->refcount, 1);
	snd->nbufs = SND_NBUFS;
	mutex_init(&snd->mutex, 0);
	waitq_init(&snd->waitq);
	int ret = pipebuf_init(&snd->pipebuf, PAGE_SIZE * 2, &snd->mutex,
	                       &snd->waitq, &snd->waitq);
	if (ret)
	{
		mutex_destroy(&snd->mutex);
		waitq_destroy(&snd->waitq);
		sma_free(&snd_sma, snd);
		return ret;
	}
	snd->pipebuf.nreaders++;
	snd->pipebuf.nwriters++;
	for (size_t i = 0; ; ++i)
	{
		if (i == 128)
		{
			snd_free(snd);
			return -ENOMEM;
		}
		char name[MAXPATHLEN];
		snprintf(name, sizeof(name), "snd%zu", i);
		ret = cdev_alloc(name, 0, 0, 0600, makedev(5, i), &fop, &snd->cdev);
		if (!ret)
			break;
		if (ret != -EEXIST)
		{
			snd_free(snd);
			return ret;
		}
	}
	snd->cdev->userdata = snd;
	for (size_t i = 0; i < snd->nbufs; ++i)
	{
		ret = pm_alloc_page(&snd->bufs[i].page);
		if (ret)
		{
			snd_free(snd);
			return ret;
		}
		snd->bufs[i].data = vm_map(snd->bufs[i].page, PAGE_SIZE,
		                           VM_PROT_RW);
		if (!snd->bufs[i].data)
		{
			snd_free(snd);
			return -ENOMEM;
		}
#if 1
		memset(snd->bufs[i].data, 0, PAGE_SIZE);
#endif
#if 0
		random_get(snd->bufs[i].data, PAGE_SIZE);
#endif
#if 0
		int16_t *dst = (int16_t*)snd->bufs[i].data;
		for (size_t n = 0; n < PAGE_SIZE / 2; ++n)
		{
			if (n & 1)
			{
				if (n % 96 < 48)
					dst[n] = INT16_MIN;
				else
					dst[n] = INT16_MAX;
			}
			else
			{
				if (n % 192 < 96)
					dst[n] = INT16_MIN;
				else
					dst[n] = INT16_MAX;
			}
		}
#endif
	}
	*sndp = snd;
	return 0;
}

void snd_free(struct snd *snd)
{
	if (!snd)
		return;
	if (refcount_dec(&snd->refcount))
		return;
	for (size_t i = 0; i < snd->nbufs; ++i)
	{
		if (snd->bufs[i].data)
			vm_unmap(snd->bufs[i].data, PAGE_SIZE);
		if (snd->bufs[i].page)
			pm_free_page(snd->bufs[i].page);
	}
	pipebuf_destroy(&snd->pipebuf);
	mutex_destroy(&snd->mutex);
	waitq_destroy(&snd->waitq);
	sma_free(&snd_sma, snd);
}

void snd_fill_buf(struct snd *snd, struct snd_buf *buf)
{
	struct uio uio;
	struct iovec iov;
	uio_fromkbuf(&uio, &iov, buf->data, PAGE_SIZE, 0);
	ssize_t rd = pipebuf_read(&snd->pipebuf, &uio, 0, NULL);
	if (rd <= 0) /* XXX ? */
		memset(&buf->data[rd], 0, PAGE_SIZE);
	else
		memset(&buf->data[rd], 0, PAGE_SIZE - rd);
}

static int snd_cdev_open(struct file *file, struct node *node)
{
	struct cdev *cdev = node->cdev;
	if (!cdev)
		return -EINVAL;
	file->userdata = cdev->userdata;
	return 0;
}

static ssize_t snd_cdev_write(struct file *file, struct uio *uio)
{
	struct snd *snd = file->userdata;
	return pipebuf_write(&snd->pipebuf, uio, uio->count, NULL);
}

static const struct file_op fop =
{
	.open = snd_cdev_open,
	.write = snd_cdev_write,
};
