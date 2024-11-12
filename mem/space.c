#include <random.h>
#include <errno.h>
#include <file.h>
#include <cpu.h>
#include <std.h>
#include <sma.h>
#include <uio.h>
#include <vfs.h>
#include <mem.h>

static struct sma vm_zone_sma;
static struct sma vm_shm_sma;
static struct sma vm_space_sma;

struct vm_region g_vm_heap; /* kernel heap */
struct mutex g_vm_mutex;

void vm_zone_init(void)
{
	sma_init(&vm_zone_sma, sizeof(struct vm_zone), NULL, NULL, "vm_zone");
	sma_init(&vm_shm_sma, sizeof(struct vm_shm), NULL, NULL, "vm_shm");
	sma_init(&vm_space_sma, sizeof(struct vm_space), NULL, NULL, "vm_space");
}

static int is_range_user(struct vm_space *vm_space, uintptr_t addr, size_t size)
{
	if (!addr)
		return 0;
	if (addr < vm_space->region.addr)
		return 0;
	uintptr_t end;
	if (__builtin_add_overflow(addr, size, &end))
		return 0;
	if (end > vm_space->region.addr + vm_space->region.size)
		return 0;
	return 1;
}

static int is_range_aligned(uintptr_t addr, size_t size)
{
	if (addr & PAGE_MASK)
		return 0;
	if (size & PAGE_MASK)
		return 0;
	return 1;
}

static int is_range_overflowing(uintptr_t addr, size_t size)
{
	uintptr_t end;
	if (__builtin_add_overflow(addr, size, &end))
		return 1;
	return 0;
}

static inline void print_zones(struct vm_space *space)
{
	printf("zones %p:\n", space);
	struct vm_zone *zone;
	TAILQ_FOREACH(zone, &space->zones, chain)
	{
		printf("\t0x%0*zx - 0x%0*zx %c%c%c %p\n",
		       (int)(sizeof(uintptr_t) * 2), zone->addr,
		       (int)(sizeof(uintptr_t) * 2), zone->addr + zone->size,
		       (zone->prot & VM_PROT_R) ? 'R' : '-',
		       (zone->prot & VM_PROT_W) ? 'W' : '-',
		       (zone->prot & VM_PROT_X) ? 'X' : '-',
		       zone->file);
	}
}

static struct vm_zone *zone_dup(const struct vm_zone *zone, uintptr_t addr,
                                size_t size, off_t off)
{
	struct vm_zone *dup = sma_alloc(&vm_zone_sma, 0);
	if (!dup)
		return NULL;
	dup->op = zone->op;
	dup->addr = addr;
	dup->size = size;
	dup->off = off;
	dup->flags = zone->flags;
	dup->prot = zone->prot;
	dup->file = zone->file;
	dup->userdata = zone->userdata;
	if (dup->file)
		file_ref(dup->file);
	return dup;
}

static void zone_free(struct vm_zone *zone)
{
	if (zone->file)
		file_free(zone->file);
	sma_free(&vm_zone_sma, zone);
}

int vm_free(struct vm_space *space, uintptr_t addr, size_t size)
{
	if (!is_range_aligned(addr, size))
		return -EINVAL;
	uintptr_t end;
	if (__builtin_add_overflow(addr, size, &end))
		return -EOVERFLOW;
	struct vm_zone *zone, *nxt;
	TAILQ_FOREACH_SAFE(zone, &space->zones, chain, nxt)
	{
		if (end <= zone->addr)
			break;
		if (addr >= zone->addr + zone->size)
			continue;
		if (addr <= zone->addr)
		{
			if (end >= zone->addr + zone->size)
			{
				/* remove full */
				TAILQ_REMOVE(&space->zones, zone, chain);
				vfree_user(space, zone->addr, zone->size);
				if (zone->op && zone->op->close)
					zone->op->close(zone);
				zone_free(zone);
			}
			else
			{
				/* truncate head */
				size_t delta = end - zone->addr;
				vfree_user(space, addr, delta);
				zone->addr += delta;
				zone->off += delta;
				zone->size -= size;
			}
		}
		else
		{
			if (end >= zone->addr + zone->size)
			{
				/* truncate tail */
				size_t delta = zone->addr + zone->size - addr;
				vfree_user(space, addr, delta);
				zone->size -= delta;
			}
			else
			{
				/* split */
				struct vm_zone *newz = zone_dup(zone, end,
				                                zone->addr + zone->size - end,
				                                zone->off);
				if (!newz)
					return -ENOMEM;
				TAILQ_INSERT_AFTER(&space->zones, zone, newz, chain);
				zone->size = addr - zone->addr;
				if (newz->op && newz->op->open)
					newz->op->open(newz);
				vfree_user(space, addr, size);
			}
		}
	}
	return 0;
}

int vm_alloc(struct vm_space *space, uintptr_t addr, off_t off,
             size_t size, uint32_t prot, uint32_t flags,
             struct file *file, struct vm_zone **zonep)
{
	if (!is_range_aligned(addr, size))
		return -EINVAL;
	if (!file)
	{
		/* XXX if the block near the next one is also a anonymous one,
		 * merge them to avoid useless zone allocation
		 */
	}
	struct vm_zone *zone = sma_alloc(&vm_zone_sma, 0);
	if (!zone)
		return -ENOMEM;
	int ret = vm_region_alloc(&space->region, addr, size, &zone->addr);
	if (ret)
	{
		sma_free(&vm_zone_sma, zone);
		return ret;
	}
	zone->off = off;
	zone->size = size;
	zone->op = NULL;
	zone->file = file;
	if (file)
		file_ref(file);
	zone->prot = prot;
	zone->flags = flags;
	zone->userdata = NULL;
	*zonep = zone;
	struct vm_zone *it;
	TAILQ_FOREACH(it, &space->zones, chain)
	{
		if (zone->addr < it->addr)
		{
			TAILQ_INSERT_BEFORE(it, zone, chain);
			return 0;
		}
	}
	TAILQ_INSERT_TAIL(&space->zones, zone, chain);
	return 0;
}

struct vm_space *vm_space_alloc(void)
{
	int ret;
	struct vm_space *space = sma_alloc(&vm_space_sma, 0);
	if (!space)
	{
		TRACE("failed to allocate vm space");
		return NULL;
	}
	uintptr_t addr = VADDR_USER_BEGIN;
	uintptr_t size = VADDR_USER_END - addr;
#ifdef WITH_ASLR
	uintptr_t aslr[2];
	ret = random_get(aslr, sizeof(aslr));
	if (ret != sizeof(aslr))
	{
		TRACE("failed to get ASLR random");
		sma_free(&vm_space_sma, space);
		return NULL;
	}
#if __SIZE_WIDTH__ == 32
	/* 24-bits, page-aligned */
	aslr[0] &= 0xFFF000;
	aslr[1] &= 0xFFF000;
#elif defined(__aarch64__)
	/* 40-bits, 16 page-aligned */
	aslr[0] &= 0xFFFFFF0000;
	aslr[1] &= 0xFFFFFF0000;
#else
	/* 40-bits, page-aligned */
	aslr[0] &= 0xFFFFFFF000;
	aslr[1] &= 0xFFFFFFF000;
#endif
	addr += aslr[0];
	size -= aslr[0] + aslr[1];
#endif
	vm_region_init(&space->region, addr, size);
	mutex_init(&space->mutex, MUTEX_RECURSIVE);
	refcount_init(&space->refcount, 1);
	TAILQ_INIT(&space->zones);
	TAILQ_INIT(&space->shms);
	ret = arch_vm_space_init(space);
	if (ret)
	{
		sma_free(&vm_space_sma, space);
		return NULL;
	}
	return space;
}

void vm_space_free(struct vm_space *space)
{
	if (!space)
		return;
	if (refcount_dec(&space->refcount))
		return;
	arch_vm_space_cleanup(space);
	mutex_destroy(&space->mutex);
	sma_free(&vm_space_sma, space);
}

static int dup_zones(struct vm_space *dst, struct vm_space *src)
{
	struct vm_zone *zone;
	TAILQ_FOREACH(zone, &src->zones, chain)
	{
		struct vm_zone *dup = zone_dup(zone, zone->addr, zone->size,
		                               zone->off);
		assert(dup, "failed to duplicate vm zone\n");
		TAILQ_INSERT_TAIL(&dst->zones, dup, chain);
		if (dup->op && dup->op->open)
			dup->op->open(dup);
	}
	return 0;
}

static int dup_shms(struct vm_space *dst, struct vm_space *src)
{
	struct vm_shm *shm;
	TAILQ_FOREACH(shm, &src->shms, chain)
	{
		struct vm_shm *dup = sma_alloc(&vm_shm_sma, 0);
		assert(dup, "failed to duplicate vm shm\n");
		dup->addr = shm->addr;
		dup->size = shm->size;
		dup->shm = shm->shm;
		TAILQ_INSERT_TAIL(&dst->shms, dup, chain);
	}
	return 0;
}

struct vm_space *vm_space_dup(struct vm_space *space)
{
	struct vm_space *dup;
	int ret;

	dup = sma_alloc(&vm_space_sma, 0);
	if (!dup)
	{
		TRACE("failed to allocate vm space");
		return NULL;
	}
	mutex_init(&dup->mutex, MUTEX_RECURSIVE);
	refcount_init(&dup->refcount, 1);
	TAILQ_INIT(&dup->zones);
	TAILQ_INIT(&dup->shms);
	ret = arch_vm_space_init(dup);
	if (ret)
		panic("failed to allocate vm space\n"); /* XXX */
	mutex_lock(&space->mutex);
	ret = vm_region_dup(&dup->region, &space->region);
	if (ret)
		panic("failed to duplicate region\n"); /* XXX */
	ret = dup_zones(dup, space);
	if (ret)
		panic("failed to duplicate zones\n"); /* XXX */
	ret = dup_shms(dup, space);
	if (ret)
		panic("failed to duplicate shms\n"); /* XXX */
	ret = arch_vm_space_copy(dup, space);
	if (ret)
		panic("failed to copy vm space\n"); /* XXX */
	mutex_unlock(&space->mutex);
	return dup;
}

int vm_space_protect(struct vm_space *space, uintptr_t addr, size_t size,
                     uint32_t prot)
{
	if (!is_range_aligned(addr, size))
		return -EINVAL;
	uintptr_t end;
	if (__builtin_add_overflow(addr, size, &end))
		return -EOVERFLOW;
	struct vm_zone *zone, *nxt;
	TAILQ_FOREACH_SAFE(zone, &space->zones, chain, nxt)
	{
		if (end <= zone->addr)
			break;
		if (addr >= zone->addr + zone->size)
			continue;
		if (addr <= zone->addr)
		{
			if (end >= zone->addr + zone->size)
			{
				/* protect full */
				if (prot != zone->prot)
				{
					int ret = vm_protect(space, zone->addr,
					                     zone->size, prot);
					if (ret)
						return ret;
					zone->prot = prot;
				}
			}
			else
			{
				/* protect head */
				if (prot != zone->prot)
				{
					size_t delta = end - zone->addr;
					struct vm_zone *newz = zone_dup(zone,
					                                zone->addr + delta,
					                                zone->size - delta,
					                                zone->off + delta);
					if (!newz)
						return -ENOMEM;
					zone->size = delta;
					TAILQ_INSERT_AFTER(&space->zones, zone, newz, chain);
					if (newz->op && newz->op->open)
						newz->op->open(newz);
					int ret = vm_protect(space, zone->addr,
					                     zone->size, prot);
					if (ret)
						return ret;
					zone->prot = prot;
				}
			}
		}
		else
		{
			if (end >= zone->addr + zone->size)
			{
				/* truncate tail */
				if (prot != zone->prot)
				{
					size_t delta = zone->addr + zone->size - addr;
					struct vm_zone *newz = zone_dup(zone, addr,
					                                delta,
					                                zone->off + addr - zone->addr);
					if (!newz)
						return -ENOMEM;
					zone->size -= delta;
					TAILQ_INSERT_AFTER(&space->zones, zone, newz, chain);
					if (newz->op && newz->op->open)
						newz->op->open(newz);
					int ret = vm_protect(space, newz->addr,
					                     newz->size, prot);
					if (ret)
						return ret;
					newz->prot = prot;
				}
			}
			else
			{
				/* split */
				if (prot != zone->prot)
				{
					size_t deltal = addr - zone->addr;
					size_t deltah = zone->addr + zone->size - end;
					struct vm_zone *newl = zone_dup(zone, zone->addr,
					                                deltal, zone->off);
					if (!newl)
						return -ENOMEM;
					struct vm_zone *newh = zone_dup(zone, end,
					                                deltah,
					                                zone->off + zone->size - deltah);
					if (!newh)
					{
						zone_free(newl);
						return -ENOMEM;
					}
					TAILQ_INSERT_BEFORE(zone, newl, chain);
					TAILQ_INSERT_AFTER(&space->zones, zone, newh, chain);
					zone->addr += deltal;
					zone->size -= deltal + deltah;
					if (newl->op && newl->op->open)
						newl->op->open(newl);
					if (newh->op && newh->op->open)
						newh->op->open(newl);
					int ret = vm_protect(space, zone->addr,
					                     zone->size, prot);
					if (ret)
						return ret;
					zone->prot = prot;
				}
			}
		}
	}
	return vm_protect(space, addr, size, prot);
}

int vm_space_find(struct vm_space *space, uintptr_t addr,
                  struct vm_zone **zonep)
{
	struct vm_zone *zone;
	TAILQ_FOREACH(zone, &space->zones, chain)
	{
		if (addr < zone->addr)
			break;
		if (addr < zone->addr + zone->size)
		{
			*zonep = zone;
			return 0;
		}
	}
	return -EFAULT;
}

void vm_space_cleanup(struct vm_space *space)
{
	struct vm_zone *zone;
	while ((zone = TAILQ_FIRST(&space->zones)))
	{
		TAILQ_REMOVE(&space->zones, zone, chain);
		vfree_user(space, zone->addr, zone->size);
		if (zone->op && zone->op->close)
			zone->op->close(zone);
		zone_free(zone);
	}
	vm_region_destroy(&space->region);
	struct vm_shm *shm;
	while ((shm = TAILQ_FIRST(&space->shms)))
	{
		TAILQ_REMOVE(&space->shms, shm, chain);
		sma_free(&vm_shm_sma, shm);
	}
	arch_vm_space_cleanup(space);
}

int vm_shm_alloc(uintptr_t addr, size_t size, int id, struct vm_shm **shmp)
{
	struct vm_shm *shm = sma_alloc(&vm_shm_sma, 0);
	if (!shm)
		return -ENOMEM;
	shm->addr = addr;
	shm->size = size;
	shm->shm = id;
	*shmp = shm;
	return 0;
}

int vm_shm_find(struct vm_space *space, uintptr_t addr, struct vm_shm **shmp)
{
	struct vm_shm *vm_shm;
	mutex_lock(&space->mutex);
	TAILQ_FOREACH(vm_shm, &space->shms, chain)
	{
		if (vm_shm->addr != (uintptr_t)addr)
			continue;
		break;
	}
	mutex_unlock(&space->mutex);
	*shmp = vm_shm;
	return vm_shm ? 0 : -EINVAL;
}

int vm_fault_page(struct vm_space *space, uintptr_t addr,
                  struct page **page, struct vm_zone **zonep)
{
	struct vm_zone *zone;
	int ret = vm_space_find(space, addr, &zone);
	if (ret)
		return ret;
	if (zone->op)
	{
		ret = zone->op->fault(zone, addr - zone->addr, page);
		if (ret)
			return ret;
	}
	else
	{
		ret = pm_alloc_page(page);
		if (ret)
			return ret;
		struct arch_copy_zone *copy_zone = &curcpu()->copy_dst_page;
		arch_set_copy_zone(copy_zone, (*page)->offset);
		memset(copy_zone->ptr, 0, PAGE_SIZE);
	}
	*zonep = zone;
	return 0;
}

int vm_fault(struct vm_space *space, uintptr_t addr, uint32_t prot)
{
	if (!space)
	{
		TRACE("page fault on user address %p without running thread",
		      (void*)addr);
		return -EINVAL;
	}
	addr &= ~PAGE_MASK;
	if (addr < space->region.addr
	 || addr >= space->region.addr + space->region.size)
	{
		TRACE("page fault on non-user address %p from userspace",
		      (void*)addr);
		return -EINVAL;
	}
	mutex_lock(&space->mutex);
	int ret = arch_vm_populate_page(space, addr, prot, NULL);
	mutex_unlock(&space->mutex);
	return ret;
}

static int vfree_zone(struct vm_space *space, uintptr_t addr, size_t size)
{
	if (!is_range_aligned(addr, size)
	 || is_range_overflowing(addr, size))
		return -EINVAL;
	if (space)
	{
		if (!is_range_user(space, addr, size))
		{
			TRACE("vfree user zone in non-user space");
			return -EINVAL;
		}
	}
	else
	{
		if (addr < g_vm_heap.addr
		 || addr + size > g_vm_heap.addr + g_vm_heap.size)
		{
			TRACE("vfree kern zone in non-kern space");
			return -EINVAL;
		}
	}
	struct mutex *mutex = space ? &space->mutex : &g_vm_mutex;
	mutex_lock(mutex);
	arch_vm_unmap(space, addr, size);
	int ret = vm_region_free(space ? &space->region : &g_vm_heap, addr, size);
	mutex_unlock(mutex);
	return ret;
}

void *vmalloc(size_t size)
{
	if (!is_range_aligned(0, size))
		return NULL;
	uintptr_t addr = 0;
	struct mutex *mutex = &g_vm_mutex;
	mutex_lock(mutex);
	if (vm_region_alloc(&g_vm_heap, addr, size, &addr))
	{
		mutex_unlock(mutex);
		return NULL;
	}
	assert(!(addr & PAGE_MASK), "vmalloc unaligned data 0x%zx\n", addr);
	for (size_t i = 0; i < size; i += PAGE_SIZE)
	{
		struct page *page;
		int ret = pm_alloc_page(&page);
		if (ret)
		{
			arch_vm_unmap(NULL, addr, i);
			mutex_unlock(mutex);
			TRACE("failed to allocate page");
			return NULL;
		}
		ret = arch_vm_map(NULL, addr + i, page->offset, PAGE_SIZE,
		                  VM_PROT_RW);
		pm_free_page(page);
		if (ret)
		{
			arch_vm_unmap(NULL, addr, i);
			mutex_unlock(mutex);
			TRACE("failed to map page");
			return NULL;
		}
	}
	mutex_unlock(mutex);
	return (void*)addr;
}

void vfree(void *ptr, size_t size)
{
	int ret = vfree_zone(NULL, (uintptr_t)ptr, size);
	if (ret)
		panic("vfree failed\n");
}

int vfree_user(struct vm_space *space, uintptr_t addr, size_t size)
{
	return vfree_zone(space, addr, size);
}

void *vm_map(struct page *page, size_t size, uint32_t prot)
{
	assert(!(size & PAGE_MASK), "vmap unaligned size 0x%zx\n", size);
	uintptr_t addr;
	mutex_spinlock(&g_vm_mutex);
	if (vm_region_alloc(&g_vm_heap, 0, size, &addr))
	{
		mutex_unlock(&g_vm_mutex);
		return NULL;
	}
	assert(!(addr & PAGE_MASK), "vmap unaligned data 0x%zx\n", addr);
	if (arch_vm_map(NULL, addr, page->offset, size, prot))
	{
		mutex_unlock(&g_vm_mutex);
		return NULL;
	}
	mutex_unlock(&g_vm_mutex);
	return (void*)addr;
}

void *vm_map_pages(struct page **pages, size_t npages, uint32_t prot)
{
	uintptr_t addr;
	mutex_spinlock(&g_vm_mutex);
	size_t size = npages * PAGE_SIZE;
	if (vm_region_alloc(&g_vm_heap, 0, size, &addr))
	{
		mutex_unlock(&g_vm_mutex);
		return NULL;
	}
	assert(!(addr & PAGE_MASK), "vmap unaligned data 0x%zx\n", addr);
	for (size_t i = 0; i < npages; ++i)
	{
		if (arch_vm_map(NULL, addr + i * PAGE_SIZE, pages[i]->offset,
		                PAGE_SIZE, prot))
		{
			arch_vm_unmap(NULL, addr, size);
			mutex_unlock(&g_vm_mutex);
			TRACE("failed to map page");
			return NULL;
		}
	}
	mutex_unlock(&g_vm_mutex);
	return (void*)addr;
}

static int vm_map_user_page(struct vm_space *space, uintptr_t addr,
                            uintptr_t uaddr, uint32_t prot)
{
	uintptr_t poff;
	int ret = arch_vm_populate_page(space, uaddr, VM_PROT_R, &poff);
	if (ret)
		return ret;
	return arch_vm_map(NULL, addr, poff, PAGE_SIZE, prot);
}

int vm_map_user(struct vm_space *space, uintptr_t uaddr, size_t size,
                uint32_t prot, void **ptr)
{
	if (!space)
	{
		TRACE("no vm space given");
		return -EINVAL;
	}
	if (!is_range_aligned(uaddr, size)
	 || !is_range_user(space, uaddr, size))
		return -EINVAL;
	uintptr_t addr;
	mutex_spinlock(&g_vm_mutex);
	int ret = vm_region_alloc(&g_vm_heap, 0, size, &addr);
	mutex_unlock(&g_vm_mutex);
	if (ret)
		return ret;
	mutex_lock(&space->mutex);
	for (size_t i = 0; i < size; i += PAGE_SIZE)
	{
		ret = vm_map_user_page(space, addr + i, uaddr + i, prot);
		if (!ret)
			continue;
		if (i)
			vm_unmap((void*)addr, i);
		mutex_spinlock(&g_vm_mutex);
		if (vm_region_free(&g_vm_heap, addr + i, size - i))
			panic("failed to free just allocated region\n");
		mutex_unlock(&g_vm_mutex);
		mutex_unlock(&space->mutex);
		return ret;
	}
	mutex_unlock(&space->mutex);
	*ptr = (void*)addr;
	return 0;
}

void vm_unmap(void *ptr, size_t size)
{
	if (vfree_zone(NULL, (uintptr_t)ptr, size))
		panic("vunmap failed\n");
}

int vm_populate(struct vm_space *space, uintptr_t addr, size_t size)
{
	if (!is_range_aligned(addr, size)
	 || is_range_overflowing(addr, size))
		return -EINVAL;
	for (size_t i = 0; i < size; i += PAGE_SIZE)
	{
		int ret = arch_vm_populate_page(space, addr + i, VM_PROT_R,
		                                NULL);
		if (ret)
			return ret;
	}
	return 0;
}

int vm_paddr(struct vm_space *space, uintptr_t addr, uintptr_t *paddr)
{
	uintptr_t poff;
	int ret = arch_vm_populate_page(space, addr, VM_PROT_R, &poff);
	if (ret)
		return ret;
	*paddr = poff * PAGE_SIZE + (addr & PAGE_MASK);
	return 0;
}

int vm_protect(struct vm_space *space, uintptr_t addr, size_t size,
               uint32_t prot)
{
	if (!is_range_aligned(addr, size)
	 || is_range_overflowing(addr, size))
		return -EINVAL;
	mutex_lock(space ? &space->mutex : &g_vm_mutex);
	if (!vm_region_test(space ? &space->region : &g_vm_heap, addr, size))
	{
		TRACE("invalid region");
		mutex_unlock(space ? &space->mutex : &g_vm_mutex);
		return -ENOMEM;
	}
	int ret = arch_vm_protect(space, addr, size, prot);
	mutex_unlock(space ? &space->mutex : &g_vm_mutex);
	return ret;
}

/* XXX shame.... */
static const char *mem_fmt(char *buf, size_t size, size_t n)
{
#if __SIZE_WIDTH__ >= 64
	if (n >= 100000000000000000)
		snprintf(buf, size, "%zu PB", n / 1000000000000000);
	else if (n >= 10000000000000000)
		snprintf(buf, size, "%zu.%01zu PB", n / 1000000000000000, (n / 100000000000000) % 10);
	else if (n >= 1000000000000000)
		snprintf(buf, size, "%zu.%02zu PB", n / 1000000000000000, (n / 10000000000000) % 100);
	else if (n >= 100000000000000)
		snprintf(buf, size, "%zu TB", n / 1000000000000);
	else if (n >= 10000000000000)
		snprintf(buf, size, "%zu.%01zu TB", n / 1000000000000, (n / 100000000000) % 10);
	else if (n >= 1000000000000)
		snprintf(buf, size, "%zu.%02zu TB", n / 1000000000000, (n / 10000000000) % 100);
	else if (n >= 100000000000)
		snprintf(buf, size, "%zu GB", n / 1000000000);
	else if (n >= 10000000000)
		snprintf(buf, size, "%zu.%01zu GB", n / 1000000000, (n / 100000000) % 10);
	else
#endif
	if (n >= 1000000000)
		snprintf(buf, size, "%zu.%02zu GB", n / 1000000000, (n / 10000000) % 100);
	else if (n >= 100000000)
		snprintf(buf, size, "%zu MB", n / 1000000);
	else if (n >= 10000000)
		snprintf(buf, size, "%zu.%01zu MB", n / 1000000, (n / 100000) % 10);
	else if (n >= 1000000)
		snprintf(buf, size, "%zu.%02zu MB", n / 1000000, (n / 10000) % 100);
	else if (n >= 100000)
		snprintf(buf, size, "%zu KB", n / 1000);
	else if (n >= 10000)
		snprintf(buf, size, "%zu.%01zu kB", n / 1000, (n / 100) % 10);
	else if (n >= 1000)
		snprintf(buf, size, "%zu.%02zu kB", n / 1000, (n / 10) % 100);
	else
		snprintf(buf, size, "%zu B", n);
	return buf;
}

static void pm_dumpinfo(struct uio *uio)
{
	size_t size = 0;
	size_t used = 0;
	size_t admin = 0;
	struct pm_pool *pm_pool;
	TAILQ_FOREACH(pm_pool, &g_pm_pools, chain)
	{
		mutex_spinlock(&pm_pool->mutex);
		size += pm_pool->count;
		used += pm_pool->used;
		admin += pm_pool->admin;
		mutex_unlock(&pm_pool->mutex);
	}
	char buf[16];
	uprintf(uio, "PhysicalUsed:      0x%0*zx (%s)\n",
	        (int)sizeof(size_t) * 2, used * PAGE_SIZE,
	        mem_fmt(buf, sizeof(buf), used * PAGE_SIZE));
	uprintf(uio, "PhysicalSize:      0x%0*zx (%s)\n",
	        (int)sizeof(size_t) * 2, size * PAGE_SIZE,
	        mem_fmt(buf, sizeof(buf), size * PAGE_SIZE));
	uprintf(uio, "PhysicalReserved : 0x%0*zx (%s)\n",
	        (int)sizeof(size_t) * 2, admin * PAGE_SIZE,
	        mem_fmt(buf, sizeof(buf), admin * PAGE_SIZE));
}

static void vm_dumpinfo(struct uio *uio)
{
	size_t available = 0;
	struct vm_range *item;
	if (TAILQ_EMPTY(&g_vm_heap.ranges))
	{
		available = g_vm_heap.size;
	}
	else
	{
		TAILQ_FOREACH(item, &g_vm_heap.ranges, chain)
			available += item->size;
	}
	size_t size = g_vm_heap.size;
	size_t used = size - available;
	char buf[16];
	uprintf(uio, "KernelVirtualUsed: 0x%0*zx (%s)\n",
	        (int)sizeof(size_t) * 2, used,
	        mem_fmt(buf, sizeof(buf), used));
	uprintf(uio, "KernelVirtualSize: 0x%0*zx (%s)\n",
	        (int)sizeof(size_t) * 2, size,
	        mem_fmt(buf, sizeof(buf), size));
}

static void paging_dumpinfo(struct uio *uio)
{
	pm_dumpinfo(uio);
	vm_dumpinfo(uio);
}

static ssize_t meminfo_read(struct file *file, struct uio *uio)
{
	(void)file;
	size_t count = uio->count;
	off_t off = uio->off;
	paging_dumpinfo(uio);
	uio->off = off + count - uio->count;
	return count - uio->count;
}

static const struct file_op meminfo_fop =
{
	.read = meminfo_read,
};

void vm_register_sysfs(void)
{
	if (sysfs_mknode("meminfo", 0, 0, 0444, &meminfo_fop, NULL))
		panic("failed to create meminfo\n");
}

size_t vm_available_size(void)
{
	size_t sum = 0;
	struct vm_range *item;
	TAILQ_FOREACH(item, &g_vm_heap.ranges, chain)
		sum += item->size;
	return sum;
}
