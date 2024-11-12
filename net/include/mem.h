#ifndef MEM_H
#define MEM_H

/*
 * virtual memory layouts
 * (may vary a bit for each arch, see arch.h for effective addresses):
 *
 * 32bit:
 * 0x00000000 - 0x000FFFFF (1.0 MB): unused
 * 0x00100000 - 0xBFFFFFFF (3.2 GB): userland
 * 0xC0000000 - 0xCXXXXXXX (X.X MB): kernel binary + pm_pool
 * 0xCXXXXXXX - 0xFFBFFFFF (1.0 GB): kern heap
 * 0xFFC00000 - 0xFFFFFFFF (4.0 MB): recursive mapping
 *
 * 64bit;
 * 0x0000000000000000 - 0x00000000000FFFFF (1.0 MB): unused
 * 0x0000000000100000 - 0x00007FFFFFFFFFFF (140 TB): userland
 * 0xFFFF800000000000 - 0xFFFFBFFFFFFFFFFF ( 70 TB): direct physical mapping
 * 0xFFFFC00000000000 - 0xFFFFFFFF80000000 ( 70 TB): kern heap
 * 0xFFFFFFFF80000000 - 0xFFFFFFFFFF000000 (2.0 GB): kernel binary
 * 0xFFFFFFFFFF000000 - 0xFFFFFFFFFFFFFFFF ( 16 MB): recursive mapping
 */

#include <arch/mem.h>

#include <refcount.h>
#include <mutex.h>
#include <queue.h>
#include <types.h>

#define VM_SPACE_ISOLATION 0

#define VM_PROT_R (1 << 0)
#define VM_PROT_W (1 << 1)
#define VM_PROT_X (1 << 2)
#define VM_PROT_RW (VM_PROT_R | VM_PROT_W)
#define VM_PROT_RX (VM_PROT_R | VM_PROT_X)
#define VM_PROT_WX (VM_PROT_W | VM_PROT_X)
#define VM_PROT_RWX (VM_PROT_R | VM_PROT_W | VM_PROT_X)

#define VM_WC   (1 << 4) /* write-combining */
#define VM_UC   (1 << 5) /* uncacheable */
#define VM_WB   (1 << 6) /* write-back */
#define VM_MMIO (1 << 7) /* MMIO (device memory) */

#define MAP_ANONYMOUS (1 << 0)
#define MAP_SHARED    (1 << 1)
#define MAP_PRIVATE   (1 << 2)
#define MAP_FIXED     (1 << 3)
#define MAP_EXCL      (1 << 4)
#define MAP_POPULATE  (1 << 5)
#define MAP_NORESERVE (1 << 6)

#define PROT_NONE  0
#define PROT_EXEC  (1 << 0)
#define PROT_READ  (1 << 1)
#define PROT_WRITE (1 << 2)

#define MS_ASYNC      (1 << 0)
#define MS_SYNC       (1 << 1)
#define MS_INVALIDATE (1 << 2)

#define MADV_NORMAL   0
#define MADV_DONTNEED 1

struct page
{
	uintptr_t offset;
	refcount_t refcount;
	uint32_t flags;
};

struct pm_pool
{
	size_t *bitmap;
	size_t bitmap_size; /* number of size_t */
	size_t bitmap_first_free;
	size_t offset; /* page offset */
	size_t count; /* pages count */
	size_t used; /* used pages */
	size_t admin; /* administrative pages (bitmap, struct page) */
	struct page *pages;
	struct mutex mutex;
	TAILQ_ENTRY(pm_pool) chain;
};

TAILQ_HEAD(pm_pool_head, pm_pool);

struct vm_range
{
	uintptr_t addr;
	size_t size;
	TAILQ_ENTRY(vm_range) chain;
};

TAILQ_HEAD(vm_range_head, vm_range);

struct vm_region
{
	uintptr_t addr;
	size_t size;
	struct vm_range range_0; /* always have an available item */
	struct vm_range_head ranges; /* address-ordered */
};

struct vm_zone;

struct vm_zone_op
{
	void (*open)(struct vm_zone *zone);
	void (*close)(struct vm_zone *zone);
	int (*fault)(struct vm_zone *zone, off_t off, struct page **page);
};

struct file;

struct vm_zone
{
	const struct vm_zone_op *op;
	uintptr_t addr;
	size_t size;
	off_t off;
	uint32_t flags;
	uint32_t prot;
	struct file *file;
	void *userdata;
	TAILQ_ENTRY(vm_zone) chain;
};

struct vm_shm
{
	uintptr_t addr;
	size_t size;
	int shm;
	TAILQ_ENTRY(vm_shm) chain;
};

struct vm_space
{
	struct arch_vm_space arch;
	struct mutex mutex;
	struct vm_region region;
	refcount_t refcount;
	TAILQ_HEAD(, vm_zone) zones;
	TAILQ_HEAD(, vm_shm) shms;
} __attribute__ ((aligned(PAGE_SIZE)));

void vm_register_sysfs(void);
size_t vm_available_size(void);

int arch_vm_space_init(struct vm_space *space);
void arch_vm_space_cleanup(struct vm_space *space);
int arch_vm_space_copy(struct vm_space *dst, struct vm_space *src);
void arch_vm_setspace(const struct vm_space *space);

int arch_vm_map(struct vm_space *space, uintptr_t addr, uintptr_t poff,
                size_t size, uint32_t prot);
int arch_vm_unmap(struct vm_space *space, uintptr_t addr, size_t size);
int arch_vm_protect(struct vm_space *space, uintptr_t addr, size_t size,
                    uint32_t prot);
int arch_vm_populate_page(struct vm_space *space, uintptr_t addr,
                          uint32_t prot, uintptr_t *poffp);
#if __SIZE_WIDTH__ == 64
void arch_pm_init_pmap(uintptr_t base, size_t count, size_t *used);
#else
void arch_pm_init_map(void *addr, uint32_t *pm_off, size_t pages);
#endif

struct vm_space *vm_space_alloc(void);
void vm_space_free(struct vm_space *space);
struct vm_space *vm_space_dup(struct vm_space *space);
int vm_fault(struct vm_space *space, uintptr_t addr, uint32_t prot);
int vm_fault_page(struct vm_space *space, uintptr_t addr,
                  struct page **page, struct vm_zone **zonep);

int vm_region_alloc(struct vm_region *region, uintptr_t addr, size_t size,
                    uintptr_t *ret);
int vm_region_free(struct vm_region *region, uintptr_t addr, size_t size);
int vm_region_test(struct vm_region *region, uintptr_t addr,
                   size_t size);
int vm_region_dup(struct vm_region *dst, const struct vm_region *src);
void vm_region_init(struct vm_region *region, uintptr_t addr, uintptr_t size);
void vm_region_destroy(struct vm_region *region);

int vm_shm_alloc(uintptr_t addr, size_t size, int id, struct vm_shm **shmp);
int vm_shm_find(struct vm_space *space, uintptr_t addr, struct vm_shm **shmp);

int vm_free(struct vm_space *space, uintptr_t addr, size_t size);
int vm_alloc(struct vm_space *space, uintptr_t addr, off_t off,
             size_t size, uint32_t prot, uint32_t flags,
             struct file *file, struct vm_zone **zonep);
int vm_space_protect(struct vm_space *space, uintptr_t addr, size_t size,
                     uint32_t prot);
int vm_space_find(struct vm_space *space, uintptr_t addr,
                  struct vm_zone **zonep);

void *vmalloc(size_t bytes);
void vfree(void *ptr, size_t bytes);
int vfree_user(struct vm_space *space, uintptr_t addr, size_t bytes);

void *vm_map(struct page *page, size_t bytes, uint32_t prot);
void vm_zero_page(struct page *page);
int vm_map_user(struct vm_space *space, uintptr_t addr, size_t bytes,
                uint32_t prot, void **ptr);
void *vm_map_pages(struct page **pages, size_t npages, uint32_t prot);
void vm_unmap(void *ptr, size_t bytes);
int vm_protect(struct vm_space *space, uintptr_t addr, size_t bytes,
               uint32_t prot);
int vm_populate(struct vm_space *space, uintptr_t addr, size_t bytes);
int vm_paddr(struct vm_space *space, uintptr_t addr, uintptr_t *paddr);

int vm_verifystr(struct vm_space *space, const char *str);
int vm_verifystra(struct vm_space *space, const char * const *array);

int vm_copyin(struct vm_space *space, void *kaddr, const void *uaddr,
              size_t n);
int vm_copyout(struct vm_space *space, void *uaddr, const void *kaddr,
               size_t n);
int vm_copystr(struct vm_space *space, char *kstr, const char *ustr,
               size_t n);

int pm_alloc_page(struct page **page);
int pm_alloc_pages(struct page **pages, size_t n);
void pm_free_page(struct page *page);
void pm_free_pages(struct page *page, size_t n);
void pm_ref_page(struct page *page);
struct page *pm_get_page(size_t off);
void pm_free_pt(uintptr_t off);
int pm_fetch_pages(size_t off, size_t n, struct page **pages);
void pm_init_page(struct page *page, uintptr_t poff);
void pm_init(uintptr_t kernel_reserved);

static inline uintptr_t pm_page_addr(const struct page *page)
{
	return page->offset * PAGE_SIZE;
}

extern struct pm_pool_head g_pm_pools;

extern struct vm_region g_vm_heap;
extern struct mutex g_vm_mutex;

#endif
