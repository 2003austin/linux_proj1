#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>

static void get_pgtable_macro(void)
{
    pr_info("PAGE_OFFSET = 0x%lx\n", PAGE_OFFSET);
    pr_info("PGDIR_SHIFT = %d\n", PGDIR_SHIFT);
    pr_info("P4D_SHIFT = %d\n", P4D_SHIFT);
    pr_info("PUD_SHIFT = %d\n", PUD_SHIFT);
    pr_info("PMD_SHIFT = %d\n", PMD_SHIFT);
    pr_info("PAGE_SHIFT = %d\n", PAGE_SHIFT);
    pr_info("PTRS_PER_PGD = %d\n", PTRS_PER_PGD);
    pr_info("PTRS_PER_PUD = %d\n", PTRS_PER_PUD);
    pr_info("PTRS_PER_PMD = %d\n", PTRS_PER_PMD);
    pr_info("PTRS_PER_PTE = %d\n", PTRS_PER_PTE);
    pr_info("PAGE_MASK = 0x%lx\n", PAGE_MASK);
}

static unsigned long vaddr2paddr(unsigned long vaddr)
{
    struct mm_struct *mm = current->mm;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    pud_t pudval;
    pmd_t pmdval;
    pte_t pteval;
    spinlock_t *ptl;
    unsigned long pfn;
    phys_addr_t pa = 0;

    if (!mm || vaddr >= TASK_SIZE)
        return 0;

    if (mmap_read_lock_killable(mm))
        return 0;

    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        goto out;

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d))
        goto out;

    pud = pud_offset(p4d, vaddr);
    pudval = READ_ONCE(*pud);
    if (pud_none(pudval))
        goto out;

    if (pud_leaf(pudval)) {
        ptl = pud_lock(mm, pud);
        pudval = READ_ONCE(*pud);
        if (pud_present(pudval) && pud_leaf(pudval)) {
            pfn = pud_pfn(pudval) + ((vaddr & ~PUD_MASK) >> PAGE_SHIFT);
            if (pfn_valid(pfn))
                pa = ((phys_addr_t)pfn << PAGE_SHIFT) |
                     (vaddr & ~PAGE_MASK);
            spin_unlock(ptl);
            goto out;
        }
        spin_unlock(ptl);
        
        pudval = READ_ONCE(*pud);
        if (pud_none(pudval) || pud_leaf(pudval))
            goto out;
    }
    if (pud_bad(pudval))
        goto out;

    pmd = pmd_offset(pud, vaddr);
    pmdval = READ_ONCE(*pmd);
    if (pmd_none(pmdval))
        goto out;
    
    if (pmd_leaf(pmdval)) {
        ptl = pmd_lock(mm, pmd);
        pmdval = READ_ONCE(*pmd);
        if (pmd_present(pmdval) && pmd_leaf(pmdval)) {
            pfn = pmd_pfn(pmdval) + ((vaddr & ~PMD_MASK) >> PAGE_SHIFT);
            if (pfn_valid(pfn))
                pa = ((phys_addr_t)pfn << PAGE_SHIFT) |
                     (vaddr & ~PAGE_MASK);
            spin_unlock(ptl);
            goto out;
        }
        spin_unlock(ptl);
        pmdval = READ_ONCE(*pmd);
        if (pmd_none(pmdval) || pmd_leaf(pmdval))
            goto out;
    }
    if (pmd_bad(pmdval))
        goto out;


    pte = pte_offset_map_lock(mm, pmd, vaddr, &ptl);
    if (!pte)
        goto out;

    pteval = READ_ONCE(*pte);
    if (pte_present(pteval)) {
        pfn = pte_pfn(pteval);
        if (pfn_valid(pfn))
            pa = ((phys_addr_t)pfn << PAGE_SHIFT) |
                 (vaddr & ~PAGE_MASK);
    }
    pte_unmap_unlock(pte, ptl);

out:
    mmap_read_unlock(mm);
    return (unsigned long)pa;
}


SYSCALL_DEFINE1(my_get_physical_addresses, unsigned long __user *, usr_ptr)
{
    get_pgtable_macro();
    unsigned long paddr;
    
    paddr = vaddr2paddr((unsigned long)usr_ptr);
    return paddr;
}
