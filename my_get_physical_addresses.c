#include <linux/syscalls.h>
#include <linux/uaccess.h>   

/*
 * 系統調用：獲取虛擬地址對應的物理地址
 * @user_vaddr_ptr: 用戶空間傳入的虛擬地址指針
 * @paddr_out: 用於存儲物理地址的用戶空間指針
 * 返回值：成功返回0，失敗返回負的錯誤碼
 */
SYSCALL_DEFINE2(my_get_physical_addresses, unsigned long __user *, user_vaddr_ptr, unsigned long __user *, paddr_out)
{
    /* 用於存儲從用戶空間複製來的虛擬地址 */
    unsigned long vaddr;
    
    /* 定義頁表項指針 */
    pgd_t *pgd;    /* 頁全局目錄 Page Global Directory */
    p4d_t *p4d;    /* 四級頁目錄 Level 4 Page Directory */
    pud_t *pud;    /* 上級頁目錄 Page Upper Directory */
    pmd_t *pmd;    /* 中級頁目錄 Page Middle Directory */
    pte_t *pte;    /* 頁表項 Page Table Entry */
    
    /* 用於存儲計算結果的變量 */
    unsigned long paddr = 0;        /* 最終的物理地址 */
    unsigned long page_addr = 0;    /* 頁的物理地址 */
    unsigned long page_offset = 0;  /* 頁內偏移 */

    /* 從用戶空間複製虛擬地址 */
    if (copy_from_user(&vaddr, user_vaddr_ptr, sizeof(vaddr)))
        return -EFAULT;

    /* 獲取頁全局目錄項
     * current->mm 表示當前進程的內存描述符
     */
    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd)) {
        return -EINVAL;  /* 該虛擬地址在PGD中未映射 */
    }

    /* 獲取四級頁目錄項 */
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d)) {
        return -EINVAL;  /* 該虛擬地址在P4D中未映射 */
    }

    /* 獲取上級頁目錄項 */
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) {
        return -EINVAL;  /* 該虛擬地址在PUD中未映射 */
    }

    /* 獲取中級頁目錄項 */
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) {
        return -EINVAL;  /* 該虛擬地址在PMD中未映射 */
    }

    /* 獲取頁表項 */
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) {
        return -EINVAL;  /* 該虛擬地址在PTE中未映射 */
    }

    /* 計算物理地址
     * 物理地址由兩部分組成：
     * 1. 頁框號（page frame number）：通過清除PTE中的標誌位得到
     * 2. 頁內偏移：通過虛擬地址的低位得到
     */
    page_addr = pte_val(*pte) & PAGE_MASK & PTE_PFN_MASK;/* 獲取頁框的物理地址 */
    page_offset = vaddr & ~PAGE_MASK;             /* 獲取頁內偏移 */
    paddr = page_addr | page_offset;              /* 組合得到完整的物理地址 */

    /* 將結果複製回用戶空間 */
    if (copy_to_user(paddr_out, &paddr, sizeof(paddr)))
        return -EFAULT;  /* 複製到用戶空間失敗 */

    return 0;  /* 成功返回 */
}
