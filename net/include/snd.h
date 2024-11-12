#ifndef SND_H
#define SND_H

#include <refcount.h>
#include <pipebuf.h>
#include <types.h>

#define SND_NBUFS 32

struct page;

struct snd_buf
{
	struct page *page;
	uint8_t *data;
};

struct snd
{
	refcount_t refcount;
	struct snd_buf bufs[SND_NBUFS];
	size_t nbufs;
	struct pipebuf pipebuf;
	struct mutex mutex;
	struct waitq waitq;
	struct cdev *cdev;
};

int snd_alloc(struct snd **snd);
void snd_free(struct snd *snd);
void snd_fill_buf(struct snd *snd, struct snd_buf *buf);

#endif
