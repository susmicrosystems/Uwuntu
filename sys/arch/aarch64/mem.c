#define ENABLE_TRACE

#include <arch/asm.h>

#include <multiboot.h>
#include <errno.h>
#include <proc.h>
#include <mem.h>

#define DIR3_MASK  0x0000FF8000000000
#define DIR2_MASK  0x0000007FC0000000
#define DIR1_MASK  0x000000003FE00000
#define DIR0_MASK  0x00000000001FF000

#define DIR3_SHIFT 39
#define DIR2_SHIFT 30
#define DIR1_SHIFT 21
#define DIR0_SHIFT 12

#define DIR3_ID(addr) ((((uint64_t)(addr)) & DIR3_MASK) >> DIR3_SHIFT)
#define DIR2_ID(addr) ((((uint64_t)(addr)) & DIR2_MASK) >> DIR2_SHIFT)
#define DIR1_ID(addr) ((((uint64_t)(addr)) & DIR1_MASK) >> DIR1_SHIFT)
#define DIR0_ID(addr) ((((uint64_t)(addr)) & DIR0_MASK) >> DIR0_SHIFT)
#define PAGE_ID(addr) ((((uint64_t)(addr)) & PAGE_MASK))

#define ADDR_ALIGN(addr, mask) (((uint64_t)(addr) + (mask)) & ~(mask))
#define DIR2_ALIGN(addr) ADDR_ALIGN(addr, (1 << DIR3_SHIFT) - 1)
#define DIR1_ALIGN(addr) ADDR_ALIGN(addr, (1 << DIR2_SHIFT) - 1)
#define DIR0_ALIGN(addr) ADDR_ALIGN(addr, (1 << DIR1_SHIFT) - 1)
#define PAGE_ALIGN(addr) ADDR_ALIGN(addr, (1 << DIR0_SHIFT) - 1)

#define DIR_FLAG_UXN     (1ULL << 54) /* user no execute */
#define DIR_FLAG_PXN     (1ULL << 53) /* priviledged no execute */
#define DIR_FLAG_CNT     (1ULL << 52) /* contiguous */
#define DIR_FLAG_DBM     (1ULL << 51) /* dirty */
#define DIR_FLAG_NG      (1ULL << 11) /* non-global */
#define DIR_FLAG_AF      (1ULL << 10) /* accessed */
#define DIR_FLAG_SH(x)   (((uint64_t)(x)) << 8) /* shareability level */
#define DIR_FLAG_RO      (1ULL << 7) /* read-only */
#define DIR_FLAG_US      (1ULL << 6) /* user access */
#define DIR_FLAG_NS      (1ULL << 5) /* non-secure */
#define DIR_FLAG_ATTR(x) (((uint64_t)(x)) << 2) /* MAIR attribute index */
#define DIR_FLAG_P       (3ULL << 0) /* not really a present flag, but fair enough */
#define DIR_FLAG_MASK    0xFFF0000000000FFFULL

#define DIR_POFF(val)  (DIR_PADDR(val) >> DIR0_SHIFT)
#define DIR_PADDR(val) ((uint64_t)(val) & ~DIR_FLAG_MASK)

extern uint8_t _kernel_end;
extern uint64_t ttbr1_page[];

static uint64_t *early_vmap_pge = (uint64_t*)0xFFFFFFFFFFE00000;
static uint64_t *early_vmap_tbl = (uint64_t*)0xFFFFFFFFFFFFF000;

static inline void invalidate(uintptr_t addr)
{
	dsb_ishst();
	tlbi_vaale1is(addr >> DIR0_SHIFT);
	dsb_ish();
	isb();
}

static inline uint64_t mkentry(uint64_t poff, uint64_t flags)
{
	return (poff << DIR0_SHIFT) | flags;
}

static void set_dir0(struct vm_space *space, uint64_t addr, uint64_t *dir0_ptr,
                     uintptr_t poff, uint32_t prot)
{
	uint64_t f = poff ? (DIR_FLAG_P | DIR_FLAG_AF) : 0;
	if (space)
		f |= DIR_FLAG_US;
	if (!(prot & VM_PROT_W))
		f |= DIR_FLAG_RO;
	if (!(prot & VM_PROT_X))
		f |= DIR_FLAG_UXN | DIR_FLAG_PXN;
	if (prot & VM_UC)
		f |= DIR_FLAG_ATTR(2);
	else if (prot & VM_WB)
		f |= DIR_FLAG_ATTR(0);
	else if (prot & VM_WC)
		f |= DIR_FLAG_ATTR(5);
	else if (prot & VM_MMIO)
		f |= DIR_FLAG_ATTR(4);
	else
		f |= DIR_FLAG_ATTR(1);
	*dir0_ptr = mkentry(poff, f);
	if (!space)
	{
		invalidate(addr);
		return;
	}
	struct thread *thread = curcpu()->thread;
	if (thread && space == thread->proc->vm_space)
		invalidate(addr);
}

static int get_dir0_ptr(struct vm_space *space, uintptr_t addr, int create,
                        uint64_t **dir0_ptr)
{
	uint64_t pte_id = DIR3_ID(addr);
	uint64_t *pte = space ? PMAP(pm_page_addr(space->arch.dir_page)) : (uint64_t*)(VADDR_CODE_BEGIN + (uint64_t)ttbr1_page - 0x40000000UL);
	uint8_t shift = DIR2_SHIFT;
	for (size_t i = 0; i < 3; ++i)
	{
		if (!(pte[pte_id] & DIR_FLAG_P))
		{
			if (!create)
				return -EINVAL;
			struct page *page;
			int ret = pm_alloc_page(&page);
			if (ret)
				return ret;
			pte[pte_id] = mkentry(page->offset, DIR_FLAG_AF | DIR_FLAG_SH(0) | DIR_FLAG_P);
			memset(PMAP(page->offset * PAGE_SIZE), 0, PAGE_SIZE);
		}
		pte = PMAP(DIR_PADDR(pte[pte_id]));
		pte_id = (addr >> shift) & 0x1FF;
		shift -= 9;
	}
	*dir0_ptr = &pte[pte_id];
	return 0;
}

void arch_vm_setspace(const struct vm_space *space)
{
	if (space)
		set_ttbr0_el1(pm_page_addr(space->arch.dir_page));
	else
		set_ttbr0_el1(0); /* XXX does it even make sense at all ? */
	isb();
	tlbi_vmalle1();
	dsb_ish();
	isb();
}

int arch_vm_space_init(struct vm_space *space)
{
	int ret = pm_alloc_page(&space->arch.dir_page);
	if (ret)
		return ret;
	memset(PMAP(pm_page_addr(space->arch.dir_page)), 0, PAGE_SIZE);
	return 0;
}

static void cleanup_level(uint64_t *pte, size_t min, size_t max, uint8_t level)
{
	for (size_t i = min; i < max; ++i)
	{
		uint64_t entry = pte[i];
		if (!(entry & DIR_FLAG_P))
			continue;
		if (level)
		{
			uint64_t *nxt = PMAP(DIR_PADDR(entry));
			cleanup_level(nxt, 0, 512, level - 1);
		}
		pm_free_pt(DIR_POFF(entry));
	}
}

void arch_vm_space_cleanup(struct vm_space *space)
{
	if (!space->arch.dir_page)
		return;
	cleanup_level(PMAP(pm_page_addr(space->arch.dir_page)), 0, 512, 3);
	pm_free_page(space->arch.dir_page);
	space->arch.dir_page = NULL;
}

static int dup_table(uint64_t *dir0_dst, uint64_t *dir0_src, uint64_t dir0_id)
{
	uint64_t src_poff = DIR_POFF(dir0_src[dir0_id]);
	struct page *page = pm_get_page(src_poff);
	if (!page)
	{
		/* XXX update refcount somehow */
		dir0_dst[dir0_id] = mkentry(src_poff,
		                            dir0_src[dir0_id] & DIR_FLAG_MASK);
		return 0;
	}
	int ret = pm_alloc_page(&page);
	if (ret)
		return ret;
	dir0_dst[dir0_id] = mkentry(page->offset, dir0_src[dir0_id] & DIR_FLAG_MASK);
	struct arch_copy_zone *src_zone = &curcpu()->copy_src_page;
	struct arch_copy_zone *dst_zone = &curcpu()->copy_dst_page;
	arch_set_copy_zone(src_zone, src_poff);
	arch_set_copy_zone(dst_zone, page->offset);
	memcpy(__builtin_assume_aligned(dst_zone->ptr, PAGE_SIZE),
	       __builtin_assume_aligned(src_zone->ptr, PAGE_SIZE),
	       PAGE_SIZE);
	return 0;
}

static int copy_level(uint64_t *dst, uint64_t *src, size_t min, size_t max,
                      uint8_t level)
{
	for (size_t i = min; i < max; ++i)
	{
		if (!(src[i] & DIR_FLAG_P))
		{
			dst[i] = src[i];
			continue;
		}
		if (level)
		{
			struct page *pte_page;
			int ret = pm_alloc_page(&pte_page);
			if (ret)
				return ret;
			dst[i] = mkentry(pte_page->offset,
			                 src[i] & DIR_FLAG_MASK);
			uint64_t *nxt_src = PMAP(DIR_PADDR(src[i]));
			uint64_t *nxt_dst = PMAP(DIR_PADDR(dst[i]));
			ret = copy_level(nxt_dst, nxt_src, 0, 512, level - 1);
			if (ret)
				return ret;
		}
		else
		{
			int ret = dup_table(dst, src, i);
			if (ret)
				return ret;
		}
	}
	return 0;
}

int arch_vm_space_copy(struct vm_space *dst, struct vm_space *src)
{
	return copy_level(PMAP(pm_page_addr(dst->arch.dir_page)),
	                  PMAP(pm_page_addr(src->arch.dir_page)),
	                  0, 512, 3);
}

static int map_page(struct vm_space *space, uintptr_t addr, uintptr_t poff,
                    uint32_t prot)
{
	assert(poff, "vmap null page\n");
	assert(!(addr & PAGE_MASK), "unaligned vmap %#lx\n", addr);
	uint64_t *dir0_ptr;
	int ret = get_dir0_ptr(space, addr, 1, &dir0_ptr);
	if (ret)
	{
		TRACE("failed to get vmap ptr");
		return ret;
	}
	if (*dir0_ptr & DIR_FLAG_P)
	{
		TRACE("vmap already created page %p: 0x%016" PRIx64,
		      (void*)addr, *dir0_ptr);
		return -EINVAL;
	}
	set_dir0(space, addr, dir0_ptr, poff, prot);
	if (poff)
	{
		struct page *page = pm_get_page(poff);
		if (page)
			pm_ref_page(page);
	}
	return 0;
}

int arch_vm_map(struct vm_space *space, uintptr_t addr, uintptr_t poff,
                size_t size, uint32_t prot)
{
	/* XXX optimize pte tree */
	for (size_t i = 0; i < size; i += PAGE_SIZE)
	{
		int ret = map_page(space, addr + i, poff + i / PAGE_SIZE, prot);
		if (ret)
		{
			arch_vm_unmap(NULL, addr, i);
			TRACE("failed to map page");
			return ret;
		}
	}
	return 0;
}

static int unmap_page(struct vm_space *space, uintptr_t addr)
{
	uint64_t *dir0_ptr;
	if (get_dir0_ptr(space, addr, 0, &dir0_ptr))
		return 0;
	if (*dir0_ptr & DIR_FLAG_P)
		pm_free_pt(DIR_POFF(*dir0_ptr));
	*dir0_ptr = mkentry(0, 0);
	invalidate(addr);
	return 0;
}

int arch_vm_unmap(struct vm_space *space, uintptr_t addr, size_t size)
{
	/* XXX optimize pte tree */
	for (size_t i = 0; i < size; i += PAGE_SIZE)
		unmap_page(space, addr + i);
	return 0;
}

static int protect_page(struct vm_space *space, uintptr_t addr, uint32_t prot)
{
	uint64_t *dir0_ptr;
	int ret = get_dir0_ptr(space, addr, 0, &dir0_ptr);
	if (ret)
		return 0;
	set_dir0(space, addr, dir0_ptr, DIR_POFF(*dir0_ptr), prot);
	return 0;
}

int arch_vm_protect(struct vm_space *space, uintptr_t addr, size_t size,
                    uint32_t prot)
{
	/* XXX optimize pte tree */
	for (size_t i = 0; i < size; i += PAGE_SIZE)
		protect_page(space, addr + i, prot);
	return 0;
}

int arch_vm_populate_page(struct vm_space *space, uintptr_t addr,
                          uint32_t prot, uintptr_t *poffp)
{
	uint64_t *dir0_ptr;
	int ret = get_dir0_ptr(space, addr, 1, &dir0_ptr);
	if (ret)
	{
		TRACE("failed to get dir0 ptr");
		return ret;
	}
	uint64_t poff;
	if (*dir0_ptr & DIR_FLAG_P)
	{
		if (prot & VM_PROT_X)
		{
			if (*dir0_ptr & (DIR_FLAG_PXN | DIR_FLAG_UXN))
				return -EFAULT;
		}
		else if (prot & VM_PROT_W)
		{
			if (*dir0_ptr & DIR_FLAG_RO)
				return -EFAULT;
		}
		poff = DIR_POFF(*dir0_ptr);
	}
	else
	{
		struct vm_zone *zone;
		struct page *page;
		ret = vm_fault_page(space, addr, &page, &zone);
		if (ret)
			return ret;
		poff = page->offset;
		set_dir0(space, addr, dir0_ptr, poff, zone->prot);
	}
	if (poffp)
		*poffp = poff;
	return 0;
}

void arch_init_copy_zone(struct arch_copy_zone *zone)
{
	mutex_spinlock(&g_vm_mutex);
	int ret = vm_region_alloc(&g_vm_heap, 0, PAGE_SIZE, (uintptr_t*)&zone->ptr);
	if (ret)
		panic("failed to alloc zero page\n");
	mutex_unlock(&g_vm_mutex);
	ret = get_dir0_ptr(NULL, (uintptr_t)zone->ptr, 1, &zone->dir0_ptr);
	if (ret)
		panic("failed to get zero page tbl ptr\n");
}

void arch_set_copy_zone(struct arch_copy_zone *zone, uintptr_t poff)
{
	*zone->dir0_ptr = mkentry(poff, DIR_FLAG_AF | DIR_FLAG_SH(0) | DIR_FLAG_P);
	invalidate((uintptr_t)zone->ptr);
}

static void map_early(uintptr_t addr, uint64_t poff,
                      uintptr_t base, size_t *used)
{
	static uint64_t prev_pte[3] = {0, 0, 0};
	uint64_t pte_id = DIR3_ID(addr);
	uint64_t *pte = ttbr1_page;
	uint8_t shift = DIR2_SHIFT;
	for (size_t j = 0; j < 3; ++j)
	{
		if (!(pte[pte_id] & DIR_FLAG_P))
		{
			uint64_t pte_poff = base + (*used)++;
			uint64_t pte_val = mkentry(pte_poff,
			                           DIR_FLAG_AF | DIR_FLAG_SH(0) | DIR_FLAG_P);
			prev_pte[j] = pte_val;
			pte[pte_id] = pte_val;
			early_vmap_tbl[j + 1] = pte_val;
			pte = &early_vmap_pge[512 * (j + 1)];
			invalidate((uintptr_t)pte);
			memset(pte, 0, PAGE_SIZE);
		}
		else if (pte[pte_id] != prev_pte[j])
		{
			prev_pte[j]  = pte[pte_id];
			early_vmap_tbl[j + 1] = pte[pte_id];
			pte = &early_vmap_pge[512 * (j + 1)];
			invalidate((uintptr_t)pte);
		}
		else
		{
			pte = &early_vmap_pge[512 * (j + 1)];
		}
		pte_id = (addr >> shift) & 0x1FF;
		shift -= 9;
	}
	pte[pte_id] = mkentry(poff, DIR_FLAG_AF | DIR_FLAG_SH(0) | DIR_FLAG_P);
	invalidate(addr);
}

void arch_pm_init_pmap(uintptr_t base, size_t count, size_t *used)
{
	for (size_t i = 0; i < count; ++i)
		map_early((uintptr_t)PMAP(PAGE_SIZE * (base + i)),
		          base + i, base, used);
}

void arch_paging_init(void)
{
	uint64_t kernel_reserved = (uint64_t)&_kernel_end - VADDR_CODE_BEGIN + 0x40000000;
	kernel_reserved = PAGE_ALIGN(kernel_reserved);
	pm_init(kernel_reserved);
	uint64_t *dir3 = ttbr1_page;
	for (size_t i = 0; i < 512; ++i)
	{
		if (dir3[i] & DIR_FLAG_P)
			continue;
		struct page *page;
		if (pm_alloc_page(&page))
			panic("failed to allocate dir2 page\n");
		uint64_t dir2_val = mkentry(page->offset, DIR_FLAG_AF | DIR_FLAG_SH(0) | DIR_FLAG_P);
		dir3[i] = dir2_val;
		memset(PMAP(page->offset * PAGE_SIZE), 0, PAGE_SIZE);
	}
	/* XXX remove identity pages */
}
