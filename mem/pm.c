#include <multiboot.h>
#include <errno.h>
#include <mem.h>

struct pm_pool_head g_pm_pools = TAILQ_HEAD_INITIALIZER(g_pm_pools);

void pm_init_page(struct page *page, uintptr_t poff)
{
	page->offset = poff;
	page->flags = 0;
	refcount_init(&page->refcount, 1);
}

static void update_pm_bitmap_first_free(struct pm_pool *pm_pool, size_t start)
{
	for (size_t i = start / (sizeof(size_t) * 8); i < pm_pool->bitmap_size; ++i)
	{
		if (pm_pool->bitmap[i] == (size_t)-1)
			continue;
		for (size_t j = 0; j < sizeof(size_t) * 8; ++j)
		{
			if (pm_pool->bitmap[i] & ((size_t)1 << j))
				continue;
			pm_pool->bitmap_first_free = i * sizeof(size_t) * 8 + j;
			return;
		}
		panic("no empty bits\n");
	}
	panic("no more pages\n");
}

int pm_alloc_page(struct page **page)
{
	struct pm_pool *pm_pool;
	TAILQ_FOREACH(pm_pool, &g_pm_pools, chain)
	{
		mutex_spinlock(&pm_pool->mutex);
		if (pm_pool->bitmap_first_free >= pm_pool->bitmap_size * sizeof(size_t) * 8)
		{
			mutex_unlock(&pm_pool->mutex);
			continue;
		}
		size_t i = pm_pool->bitmap_first_free / (sizeof(size_t) * 8);
		size_t j = pm_pool->bitmap_first_free % (sizeof(size_t) * 8);
		if (pm_pool->bitmap[i] & ((size_t)1 << j))
			panic("invalid first page 0x%0*zu: 0x%0*zu\n",
			      (int)(sizeof(size_t) * 2), pm_pool->bitmap_first_free,
			      (int)(sizeof(size_t) * 2), pm_pool->bitmap[i]);
		pm_pool->bitmap[i] |= ((size_t)1 << j);
		*page = &pm_pool->pages[pm_pool->bitmap_first_free];
		assert(!refcount_get(&(*page)->refcount), "allocating referenced page (%p, %" PRIu32 " references)\n", (void*)(*page)->offset, refcount_get(&(*page)->refcount));
		pm_ref_page(*page);
		update_pm_bitmap_first_free(pm_pool, pm_pool->bitmap_first_free);
		pm_pool->used++;
		mutex_unlock(&pm_pool->mutex);
		return 0;
	}
	return -ENOMEM;
}

int pm_alloc_pages(struct page **page, size_t nb)
{
	if (!nb)
		return -EINVAL;
	struct pm_pool *pm_pool;
	TAILQ_FOREACH(pm_pool, &g_pm_pools, chain)
	{
		mutex_spinlock(&pm_pool->mutex);
		if (pm_pool->bitmap_first_free >= pm_pool->bitmap_size * sizeof(size_t) * 8)
		{
			mutex_unlock(&pm_pool->mutex);
			continue;
		}
		for (size_t i = pm_pool->bitmap_first_free / (sizeof(size_t) * 8); i < pm_pool->bitmap_size; ++i)
		{
			if (pm_pool->bitmap[i] == (size_t)-1)
				continue;
			for (size_t j = 0; j < sizeof(size_t) * 8; ++j)
			{
				if (pm_pool->bitmap[i] & ((size_t)1 << j))
					continue;
				size_t off = i * sizeof(size_t) * 8 + j;
				for (size_t k = 0; k < nb; ++k)
				{
					size_t v = off + k;
					if (pm_pool->bitmap[v / (sizeof(size_t) * 8)] & ((size_t)1 << (v % (sizeof(size_t) * 8))))
						goto next_page;
				}
				for (size_t k = 0; k < nb; ++k)
				{
					size_t v = off + k;
					assert(!refcount_get(&pm_pool->pages[v].refcount), "allocating referenced page\n");
					pm_ref_page(&pm_pool->pages[v]);
				}
				for (size_t k = 0; k < nb; ++k)
				{
					size_t v = off + k;
					pm_pool->bitmap[v / (sizeof(size_t) * 8)] |= ((size_t)1 << (v % (sizeof(size_t) * 8)));
				}
				if (pm_pool->bitmap_first_free == off)
					update_pm_bitmap_first_free(pm_pool, off + nb);
				*page = &pm_pool->pages[off];
				pm_pool->used += nb;
				mutex_unlock(&pm_pool->mutex);
				return 0;
next_page:
				continue;
			}
		}
		mutex_unlock(&pm_pool->mutex);
	}
	return -ENOMEM;
}

void pm_free_page(struct page *page)
{
	if (!page)
		return;
	struct pm_pool *pm_pool;
	TAILQ_FOREACH(pm_pool, &g_pm_pools, chain)
	{
		if (page < pm_pool->pages
		 || page >= pm_pool->pages + pm_pool->count)
			continue;
		mutex_spinlock(&pm_pool->mutex);
		if (!refcount_get(&page->refcount))
			panic("page double free %p\n", page);
		if (refcount_dec(&page->refcount))
		{
			mutex_unlock(&pm_pool->mutex);
			return;
		}
		size_t delta = page - pm_pool->pages;
		size_t *bitmap = &pm_pool->bitmap[delta / (sizeof(size_t) * 8)];
		size_t mask = ((size_t)1 << (delta % (sizeof(size_t) * 8)));
		assert(*bitmap & mask, "free_page of unallocated page: %p\n", page);
		*bitmap &= ~mask;
		if (delta < pm_pool->bitmap_first_free)
			pm_pool->bitmap_first_free = delta;
		pm_pool->used--;
		mutex_unlock(&pm_pool->mutex);
		return;
	}
	panic("free_page of invalid address: %p\n", page);
}

void pm_free_pages(struct page *pages, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		pm_free_page(&pages[i]);
}

void pm_ref_page(struct page *page)
{
	refcount_inc(&page->refcount);
}

struct page *pm_get_page(size_t off)
{
	struct pm_pool *pm_pool;
	TAILQ_FOREACH(pm_pool, &g_pm_pools, chain)
	{
		if (off >= pm_pool->offset
		 && off < pm_pool->offset + pm_pool->count)
			return &pm_pool->pages[off - pm_pool->offset];
	}
	return NULL;
}

static struct page *pm_fetch_page(size_t off)
{
	struct pm_pool *pm_pool;
	TAILQ_FOREACH(pm_pool, &g_pm_pools, chain)
	{
		if (off < pm_pool->offset
		 || off >= pm_pool->offset + pm_pool->count)
			continue;
		size_t pool_off = off - pm_pool->offset;
		size_t i = pool_off / (sizeof(size_t) * 8);
		size_t j = pool_off % (sizeof(size_t) * 8);
		if (!(pm_pool->bitmap[i] & ((size_t)1 << j)))
		{
			pm_pool->bitmap[i] |= ((size_t)1 << j);
			pm_pool->used++;
		}
		struct page *page = &pm_pool->pages[pool_off];
		pm_ref_page(page);
		if (pool_off == pm_pool->bitmap_first_free)
			update_pm_bitmap_first_free(pm_pool, pm_pool->bitmap_first_free);
		mutex_unlock(&pm_pool->mutex);
		return page;
	}
	return NULL;
}

int pm_fetch_pages(size_t off, size_t n, struct page **pages)
{
	for (size_t i = 0; i < n; ++i)
	{
		pages[i] = pm_fetch_page(off + i);
		if (!pages[i])
			return -ENOMEM; /* XXX */
	}
	return 0;
}

void pm_free_pt(uintptr_t off)
{
	struct page *page = pm_get_page(off);
	if (page)
		pm_free_page(page);
}

#if __SIZE_WIDTH__ == 64
static void init_bitmap(struct pm_pool *pm_pool)
{
	pm_pool->bitmap = PMAP(PAGE_SIZE * (pm_pool->offset + pm_pool->used));
	pm_pool->bitmap_size = (pm_pool->count + 63) / 64;
	uint64_t bitmap_bytes = pm_pool->bitmap_size * sizeof(uint64_t);
	uint64_t bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t first_free = bitmap_pages + pm_pool->used;
	memset(pm_pool->bitmap, 0, bitmap_bytes);
	for (size_t i = 0; i <= first_free; ++i)
		pm_pool->bitmap[i / 64] |= ((size_t)1 << (i % 64));
	pm_pool->bitmap_first_free += bitmap_pages;
	pm_pool->used += bitmap_pages;
}

static void init_pages(struct pm_pool *pm_pool)
{
	pm_pool->pages = PMAP(PAGE_SIZE * (pm_pool->offset + pm_pool->used));
	uint64_t pages_size = sizeof(struct page) * pm_pool->count;
	uint64_t pages_pages = (pages_size + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t pm_off = pm_pool->offset + pm_pool->bitmap_first_free;
	for (size_t i = 0; i < pm_pool->count; ++i)
	{
		struct page *page = &pm_pool->pages[i];
		page->offset = pm_pool->offset + i;
		page->flags = 0;
		refcount_init(&page->refcount, (i < (pm_off - pm_pool->offset)) ? 1 : 0);
	}
	for (size_t i = pm_pool->bitmap_first_free; i < pm_off - pm_pool->offset; ++i)
		pm_pool->bitmap[i / 64] |= ((size_t)1 << (i % 64));
	pm_pool->bitmap_first_free += pages_pages;
	pm_pool->used += pages_pages;
}

static void memory_iterator(uintptr_t addr, size_t size, void *userdata)
{
	(void)userdata;
	if (size < 1024 * 1024 * 16) /* at least 16 MiB */
		return;
	_Static_assert(sizeof(struct pm_pool) <= PAGE_SIZE);
	size_t used = 1;
	arch_pm_init_pmap(addr / PAGE_SIZE, size / PAGE_SIZE, &used);
	struct pm_pool *pm_pool = PMAP(addr);
	mutex_init(&pm_pool->mutex, 0);
	pm_pool->offset = addr / PAGE_SIZE;
	pm_pool->count = size / PAGE_SIZE;
	pm_pool->used = used;
	pm_pool->bitmap_first_free = used;
	init_bitmap(pm_pool);
	init_pages(pm_pool);
	pm_pool->admin = pm_pool->used;
	TAILQ_INSERT_TAIL(&g_pm_pools, pm_pool, chain);
}

void pm_init(uintptr_t kernel_reserved)
{
	mutex_init(&g_vm_mutex, MUTEX_RECURSIVE);
	multiboot_iterate_memory(kernel_reserved, UINT64_MAX, memory_iterator, NULL);
	if (TAILQ_EMPTY(&g_pm_pools))
		panic("no pm ranges found\n");
	g_vm_heap.addr = VADDR_HEAP_BEGIN;
	g_vm_heap.size = VADDR_HEAP_END - VADDR_HEAP_BEGIN;
	TAILQ_INIT(&g_vm_heap.ranges);
}

#else

static void *init_pm_vaddr;
extern uint8_t _kernel_end;

static void init_bitmap(struct pm_pool *pm_pool)
{
	pm_pool->bitmap = init_pm_vaddr;
	pm_pool->bitmap_size = (pm_pool->count + 31) / 32;
	uint32_t bitmap_bytes = pm_pool->bitmap_size * sizeof(uint32_t);
	uint32_t bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
	init_pm_vaddr = (uint8_t*)init_pm_vaddr + PAGE_SIZE * bitmap_pages;

	uint32_t pm_off = pm_pool->offset + pm_pool->bitmap_first_free;
	arch_pm_init_map(pm_pool->bitmap, &pm_off, bitmap_pages);

	memset(pm_pool->bitmap, 0, bitmap_bytes);
	for (size_t i = 0; i <= bitmap_pages; ++i)
		pm_pool->bitmap[i / 32] |= (1 << (i % 32));
	pm_pool->bitmap_first_free = pm_off - pm_pool->offset;
}

static void init_pages(struct pm_pool *pm_pool)
{
	pm_pool->pages = init_pm_vaddr;
	uint32_t pages_size = sizeof(struct page) * pm_pool->count;
	uint32_t pages_pages = (pages_size + PAGE_SIZE - 1) / PAGE_SIZE;
	init_pm_vaddr = (uint8_t*)init_pm_vaddr + PAGE_SIZE * pages_pages;

	uint32_t pm_off = pm_pool->offset + pm_pool->bitmap_first_free;
	arch_pm_init_map(pm_pool->pages, &pm_off, pages_pages);

	for (size_t i = 0; i < pm_pool->count; ++i)
	{
		struct page *page = &pm_pool->pages[i];
		page->offset = pm_pool->offset + i;
		page->flags = 0;
		refcount_init(&page->refcount, (i < (pm_off - pm_pool->offset)) ? 1 : 0);
	}
	for (size_t i = pm_pool->bitmap_first_free; i < pm_off - pm_pool->offset; ++i)
		pm_pool->bitmap[i / 32] |= (1 << (i % 32));
	pm_pool->bitmap_first_free = pm_off - pm_pool->offset;
}

static void memory_iterator(uintptr_t addr, size_t size, void *userdata)
{
	(void)userdata;
	if (size < 1024 * 1024 * 16) /* at least 16 MiB */
		return;
	_Static_assert(sizeof(struct pm_pool) <= PAGE_SIZE);
	struct pm_pool *pm_pool = init_pm_vaddr;
	init_pm_vaddr = (uint8_t*)init_pm_vaddr + PAGE_SIZE;
	uint32_t pm_off = addr / PAGE_SIZE;
	arch_pm_init_map(pm_pool, &pm_off, 1);
	mutex_init(&pm_pool->mutex, 0);
	pm_pool->offset = addr / PAGE_SIZE;
	pm_pool->count = size / PAGE_SIZE;
	pm_pool->used = pm_off - pm_pool->offset;
	pm_pool->bitmap_first_free = pm_off - pm_pool->offset;
	init_bitmap(pm_pool);
	init_pages(pm_pool);
	pm_pool->used = pm_pool->bitmap_first_free;
	pm_pool->admin = pm_pool->used;
	TAILQ_INSERT_TAIL(&g_pm_pools, pm_pool, chain);
}

void pm_init(uintptr_t kernel_reserved)
{
	mutex_init(&g_vm_mutex, MUTEX_RECURSIVE);
	init_pm_vaddr = (void*)(((uintptr_t)&_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
	multiboot_iterate_memory(kernel_reserved, UINT32_MAX, memory_iterator, NULL);
	if (TAILQ_EMPTY(&g_pm_pools))
		panic("no pm ranges found\n");
	g_vm_heap.addr = (uintptr_t)init_pm_vaddr;
	g_vm_heap.size = VADDR_HEAP_END - g_vm_heap.addr;
	TAILQ_INIT(&g_vm_heap.ranges);
}
#endif
