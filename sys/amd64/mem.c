#define ENABLE_TRACE

#include "arch/x86/x86.h"
#include "arch/x86/asm.h"
#include "arch/x86/msr.h"
#include "arch/x86/cr.h"

#include <errno.h>
#include <proc.h>
#include <std.h>
#include <cpu.h>
#include <mem.h>

#define PML_MASK  0x0000FF8000000000
#define PDP_MASK  0x0000007FC0000000
#define DIR_MASK  0x000000003FE00000
#define TBL_MASK  0x00000000001FF000
#define PGE_MASK  0x0000000000000FFF

#define PML_SHIFT 39
#define PDP_SHIFT 30
#define DIR_SHIFT 21
#define TBL_SHIFT 12
#define PGE_SHIFT 0

#define PML_ID(addr) ((((uint64_t)(addr)) & PML_MASK) >> PML_SHIFT)
#define PDP_ID(addr) ((((uint64_t)(addr)) & PDP_MASK) >> PDP_SHIFT)
#define DIR_ID(addr) ((((uint64_t)(addr)) & DIR_MASK) >> DIR_SHIFT)
#define TBL_ID(addr) ((((uint64_t)(addr)) & TBL_MASK) >> TBL_SHIFT)
#define PGE_ID(addr) ((((uint64_t)(addr)) & PGE_MASK) >> PGE_SHIFT)

#define ADDR_ALIGN(addr, mask) (((uint64_t)(addr) + (mask)) & ~(mask))
#define PDP_ALIGN(addr) ADDR_ALIGN(addr, (1 << PML_SHIFT) - 1)
#define DIR_ALIGN(addr) ADDR_ALIGN(addr, (1 << PDP_SHIFT) - 1)
#define TBL_ALIGN(addr) ADDR_ALIGN(addr, (1 << DIR_SHIFT) - 1)
#define PGE_ALIGN(addr) ADDR_ALIGN(addr, (1 << TBL_SHIFT) - 1)

#define DIR_FLAG_P     (1ULL << 0) /* is present */
#define DIR_FLAG_RW    (1ULL << 1) /* enable write */
#define DIR_FLAG_US    (1ULL << 2) /* available for userspace */
#define DIR_FLAG_PWT   (1ULL << 3) /* write through / write back */
#define DIR_FLAG_PCD   (1ULL << 4) /* must not be cached */
#define DIR_FLAG_A     (1ULL << 5) /* has been accessed */
#define DIR_FLAG_XD    (1ULL << 63) /* execute disable */
#define DIR_FLAG_MASK  0xFFF0000000000FFFULL

#define DIR_POFF(val)  (DIR_PADDR(val) >> TBL_SHIFT)
#define DIR_PADDR(val) ((uint64_t)(val) & ~DIR_FLAG_MASK)

#define TBL_FLAG_P     (1ULL << 0) /* is present */
#define TBL_FLAG_RW    (1ULL << 1) /* enable write */
#define TBL_FLAG_US    (1ULL << 2) /* available for userspace */
#define TBL_FLAG_PWT   (1ULL << 3) /* write through / write back */
#define TBL_FLAG_PCD   (1ULL << 4) /* must not be cached */
#define TBL_FLAG_A     (1ULL << 5) /* has been accessed */
#define TBL_FLAG_D     (1ULL << 6) /* has been written to */
#define TBL_FLAG_PAT   (1ULL << 7) /* PAT bit */
#define TBL_FLAG_G     (1ULL << 8) /* global page */
#define TBL_FLAG_XD    (1ULL << 63) /* execute disable */
#define TBL_FLAG_MASK  0xFFF0000000000FFFULL

#define TBL_POFF(val)  (TBL_PADDR(val) >> TBL_SHIFT)
#define TBL_PADDR(val) ((uint64_t)(val) & ~TBL_FLAG_MASK)

extern uint8_t _kernel_end;
extern uint64_t kern_pml_page;

static uint64_t *early_vmap_pge = (uint64_t*)0xFFFFFFFFFFE00000;
static uint64_t *early_vmap_tbl = (uint64_t*)0xFFFFFFFFFFFFF000;

static inline uint64_t mkentry(uint64_t poff, uint64_t flags)
{
	return (poff << 12) | flags;
}

static void set_pte(struct vm_space *space, uint64_t addr, uint64_t *tbl_ptr,
                    uintptr_t poff, uint32_t prot)
{
	uint64_t f = poff ? TBL_FLAG_P : 0;
	if (space)
		f |= TBL_FLAG_US;
	if (prot & VM_PROT_W)
		f |= TBL_FLAG_RW;
	if (prot & VM_UC)
		f |= TBL_FLAG_PCD | TBL_FLAG_PWT; /* PAT 3 */
	else if (prot & VM_WC)
		f |= TBL_FLAG_PAT; /* PAT 4 */
	else if (prot & VM_WB)
		f |= 0; /* PAT 0 */
	else
		f |= TBL_FLAG_PWT; /* PAT 1 */
	if (!(prot & VM_PROT_X))
		f |= TBL_FLAG_XD;
	*tbl_ptr = mkentry(poff, f);
	if (!space)
	{
		invlpg(addr);
		return;
	}
	struct thread *thread = curcpu()->thread;
	if (thread && space == thread->proc->vm_space)
		invlpg(addr);
}

static int get_tbl_ptr(struct vm_space *space, uintptr_t addr, int create,
                       uint64_t **tbl_ptr)
{
	uint64_t pte_id = PML_ID(addr);
	uint64_t *pte = space ? PMAP(pm_page_addr(space->arch.dir_page)) : PMAP(kern_pml_page);
	uint64_t f = DIR_FLAG_P | DIR_FLAG_RW;
	if (space)
		f |= DIR_FLAG_US;
	uint8_t shift = PDP_SHIFT;
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
			pte[pte_id] = mkentry(page->offset, f);
			memset(PMAP(page->offset * PAGE_SIZE), 0, PAGE_SIZE);
		}
		pte = PMAP(DIR_PADDR(pte[pte_id]));
		pte_id = (addr >> shift) & 0x1FF;
		shift -= 9;
	}
	*tbl_ptr = &pte[pte_id];
	return 0;
}

void arch_vm_setspace(const struct vm_space *space)
{
	if (space)
		setcr3(pm_page_addr(space->arch.dir_page));
	else
		setcr3(kern_pml_page);
}

int arch_vm_space_init(struct vm_space *space)
{
	int ret = pm_alloc_page(&space->arch.dir_page);
	if (ret)
		return ret;
	uint64_t *dir = PMAP(pm_page_addr(space->arch.dir_page));
	memset(dir, 0, 256 * sizeof(uint64_t));
	memcpy(&dir[256],
	       &((uint64_t*)PMAP(kern_pml_page))[256],
	       256 * sizeof(uint64_t));
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
		pm_free_pt(TBL_POFF(entry));
	}
}

void arch_vm_space_cleanup(struct vm_space *space)
{
	if (!space->arch.dir_page)
		return;
	cleanup_level(PMAP(pm_page_addr(space->arch.dir_page)), 0, 256, 3);
	pm_free_page(space->arch.dir_page);
	space->arch.dir_page = NULL;
}

static int dup_table(uint64_t *tbl_dst, uint64_t *tbl_src, uint64_t tbl_id)
{
	uint64_t src_poff = TBL_POFF(tbl_src[tbl_id]);
	struct page *page = pm_get_page(src_poff);
	if (!page)
	{
		/* XXX update refcount somehow */
		tbl_dst[tbl_id] = mkentry(src_poff,
		                          tbl_src[tbl_id] & TBL_FLAG_MASK);
		return 0;
	}
	int ret = pm_alloc_page(&page);
	if (ret)
		return ret;
	tbl_dst[tbl_id] = mkentry(page->offset, tbl_src[tbl_id] & TBL_FLAG_MASK);
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
	                  0, 256, 3);
}

static int map_page(struct vm_space *space, uintptr_t addr, uintptr_t poff,
                    uint32_t prot)
{
	assert(poff, "vmap null page\n");
	assert(!(addr & PAGE_MASK), "unaligned vmap %#lx\n", addr);
	uint64_t *tbl_ptr;
	int ret = get_tbl_ptr(space, addr, 1, &tbl_ptr);
	if (ret)
	{
		TRACE("failed to get vmap ptr");
		return ret;
	}
	if (*tbl_ptr & TBL_FLAG_P)
	{
		TRACE("vmap already created page %p: 0x%016" PRIx64,
		      (void*)addr, *tbl_ptr);
		return -EINVAL;
	}
	set_pte(space, addr, tbl_ptr, poff, prot);
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
	uint64_t *tbl_ptr;
	if (get_tbl_ptr(space, addr, 0, &tbl_ptr))
		return 0;
	if (*tbl_ptr & TBL_FLAG_P)
		pm_free_pt(TBL_POFF(*tbl_ptr));
	*tbl_ptr = mkentry(0, 0);
	invlpg(addr);
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
	uint64_t *tbl_ptr;
	int ret = get_tbl_ptr(space, addr, 0, &tbl_ptr);
	if (ret)
		return 0;
	set_pte(space, addr, tbl_ptr, TBL_POFF(*tbl_ptr), prot);
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
	uint64_t *tbl_ptr;
	int ret = get_tbl_ptr(space, addr, 1, &tbl_ptr);
	if (ret)
	{
		TRACE("failed to get tbl ptr");
		return ret;
	}
	uint64_t poff;
	if (*tbl_ptr & TBL_FLAG_P)
	{
		if (prot & VM_PROT_X)
		{
			if (*tbl_ptr & TBL_FLAG_XD)
				return -EFAULT;
		}
		else if (prot & VM_PROT_W)
		{
			if (!(*tbl_ptr & TBL_FLAG_RW))
				return -EFAULT;
		}
		poff = TBL_POFF(*tbl_ptr);
	}
	else
	{
		struct vm_zone *zone;
		struct page *page;
		ret = vm_fault_page(space, addr, &page, &zone);
		if (ret)
			return ret;
		poff = page->offset;
		set_pte(space, addr, tbl_ptr, poff, zone->prot);
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
	ret = get_tbl_ptr(NULL, (uintptr_t)zone->ptr, 1, &zone->tbl_ptr);
	if (ret)
		panic("failed to get zero page tbl ptr\n");
}

void arch_set_copy_zone(struct arch_copy_zone *zone, uintptr_t poff)
{
	*zone->tbl_ptr = mkentry(poff, TBL_FLAG_P | TBL_FLAG_RW | TBL_FLAG_XD);
	invlpg((uintptr_t)zone->ptr);
}

static void map_early(uintptr_t addr, uint64_t poff,
                      uintptr_t base, size_t *used)
{
	static uint64_t prev_pte[3] = {0, 0, 0};
	uint64_t pte_id = PML_ID(addr);
	uint64_t *pte = (uint64_t*)kern_pml_page;
	uint8_t shift = PDP_SHIFT;
	for (size_t j = 0; j < 3; ++j)
	{
		if (!(pte[pte_id] & DIR_FLAG_P))
		{
			uint64_t pte_poff = base + (*used)++;
			uint64_t pte_val = mkentry(pte_poff,
			                           DIR_FLAG_P | DIR_FLAG_RW);
			pte[pte_id] = pte_val;
			prev_pte[j] = pte_val;
			early_vmap_tbl[j + 1] = pte_val;
			pte = &early_vmap_pge[512 * (j + 1)];
			invlpg((uintptr_t)pte);
			memset(pte, 0, PAGE_SIZE);
		}
		else if (pte[pte_id] != prev_pte[j])
		{
			prev_pte[j] = pte[pte_id];
			early_vmap_tbl[j + 1] = pte[pte_id];
			pte = &early_vmap_pge[512 * (j + 1)];
			invlpg((uintptr_t)pte);
		}
		else
		{
			pte = &early_vmap_pge[512 * (j + 1)];
		}
		pte_id = (addr >> shift) & 0x1FF;
		shift -= 9;
	}
	pte[pte_id] = mkentry(poff, TBL_FLAG_P | TBL_FLAG_RW | TBL_FLAG_XD);
	invlpg(addr);
}

void arch_pm_init_pmap(uintptr_t base, size_t count, size_t *used)
{
	for (size_t i = 0; i < count; ++i)
		map_early((uintptr_t)PMAP(PAGE_SIZE * (base + i)),
		          base + i, base, used);
}

void arch_paging_init(void)
{
	uint64_t kernel_reserved = (uint64_t)&_kernel_end - VADDR_CODE_BEGIN;
	kernel_reserved = PGE_ALIGN(kernel_reserved);
	pm_init(kernel_reserved);
	uint64_t *pml = PMAP(kern_pml_page);
	for (size_t i = 256; i < 512; ++i)
	{
		if (pml[i])
			continue;
		struct page *page;
		if (pm_alloc_page(&page))
			panic("failed to allocate pdp page\n");
		uint64_t pdp_val = mkentry(page->offset, DIR_FLAG_P | DIR_FLAG_RW);
		pml[i] = pdp_val;
		memset(PMAP(page->offset * PAGE_SIZE), 0, PAGE_SIZE);
	}
	/* remove identity paging */
	pml[0] = 0;
	//setcr4(getcr4() & ~CR4_PGE);
	setcr3(getcr3());
	//setcr4(getcr4() | CR4_PGE);
}
