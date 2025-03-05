```
組別:第39組
113525008 沈育安
113522049 鄭鼎立
113522059 陳奕昕
113522128 林芷筠
```
<h1><font color="#F7A004">Goal</font></h1>

* 實作一個 Vitual Address to Physical Address 的 System Call
* 使用 CoW 和 Demand Paging 兩個測試程式來確認 System Call 正確性

<h1><font color="#F7A004">System info</font></h1>

```
OS: Ubuntu-22.04.5-desktop-amd64
Kernel Version: 5.15.137
Vitual Machine: VMware
VM Memory: 4GB
```
<h1><font color="#F7A004">Linux 中的 Page Table</font></h1>

### 1. Multi-level Page
x86 的架構使用 2-level 的 Page Table (10-10-12)，而 x86-64 的架構則使用 4-level (9-9-9-9-12) 或 5-level (`pgd_t` 和 `pud_t` 間多了一層 `p4d_t`) 的 Page Table，這可以透過 config 檔內的 `CONFIG_PGTABLE_LEVELS` 設定，基本上是基於處理器架構在設定的。

以老師上課所述的Four-level Page Table Hierarchy 為例：
* PGD (Page Global Directory)
* P4D (Page 4 Directory) -> 只有 5-level 下有此層
* PUD (Page Upper Directory)
* PMD (Page Middle Directory)
* PTE（page table entry）

![IMG_0961](https://hackmd.io/_uploads/r1vPxLSZyl.png)

首先 CR3（或稱 PDBR，Page Directory Base Register）存放的是 PGD 的實體位址。在開啟 Paging 功能之前，一定要先設定好這個暫存器的值。但程式應該要看到虛擬位址，所以使用
```task_struct->mm->pgd```來取得 PGD 的虛擬位址。
> [Chapter 3 Page Table Management](https://www.kernel.org/doc/gorman/html/understand/understand006.html)
Each process a pointer (mm_struct→pgd) to its own Page Global Directory (PGD) which is a physical page frame. This frame contains an array of type pgd_t which is an architecture specific type defined in <asm/page.h>. The page tables are loaded differently depending on the architecture. On the x86, the process page table is loaded by copying mm_struct→pgd into the cr3 register which has the side effect of flushing the TLB. In fact this is how the function __flush_tlb() is implemented in the architecture dependent code.

除了 CR3 之外，在控制暫存器（Control Registers）中還有另外三個和[設定分頁功能](https://www.csie.ntu.edu.tw/~wcchen/asm98/asm/proj/b85506061/chap2/paging.html)有關的flag：
1. PG (paging)：CR0 的 bit 31
2. PSE (page size extensions)：CR4 的 bit 4。在 Pentium 和以後的處理器才有
5. PAE (physical address extension)：CR4 的 bit 5。Pentium Pro 和 Pentium II 以後的處理器才有

以下為各 flag 功能說明：
* PG flag 設為 1 時會開啟分頁功能。
* PSE flag 設為 1 時才可以使用 4MB 的分頁大小，否則就只能使用 4KB 的分頁大小。
* PAE 為 P6 家族新增的功能，可以支援到 64GB 的實體記憶體。

### 2. 資料結構
在 Linux 核心的記憶體管理中，pgd_t、pmd_t、pud_t 和 pte_t 分別代表 Page Global Directory (PGD)、Page Upper Directory (PUD)、Page Middle Directory (PMD) 和 Page Table Entries (PTE)。這四種資料結構在本質上都是unsigned long，但是 Linux 為了減少開發過程中因為勿傳入錯誤的資料結構，所以將它們分別封裝成四種不同的頁表項。
以下為四種頁面的資料結構，定義如下:
```c=
//arch/arm64/include/asm/pgtable-types.h/ 

typedef struct { pgdval_t pgd; } pgd_t;
typedef struct { pudval_t pud; } pud_t ; 
typedef struct { pmdval_t pmd; } pmd_t ; 
typedef struct { pteval_t pte; } ;
```
其中```pgdval_t```、```pudval_t```、```pmdval_t```、```pteval_t```的型別皆為 unsigned long。

<h1><font color="#F7A004">Page Table 查表的大原則</font></h1>


由上圖可知，要查找下一層的目錄的 Base Address，就把目前這一層的 Base Address 加上 Offset 所對應的 Address 的內容取出。

Logical Address 轉換為 Physical Address 一層一層查表的整個流程為：```pgd_t -> p4d_t -> pud_t -> pmd_t -> pte_t```，最後取得```pte_t```再加上 Virtual Address 的 Offset 即得 Physical Address。

<h1><font color="#F7A004">Trace 查表的 Function</font></h1>


可以在 Bootlin 中查找，其中要查找 Kernel Version: 5.15.137。
```c=
//include/linux/pgtable.h 
#ifndef pte_offset_kernel
static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}
#define pte_offset_kernel pte_offset_kernel
#endif



/* Find an entry in the second-level page table.. */
#ifndef pmd_offset
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
	return pud_pgtable(*pud) + pmd_index(address);
}
#define pmd_offset pmd_offset
#endif


#ifndef pud_offset
static inline pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
	return p4d_pgtable(*p4d) + pud_index(address);
}
#define pud_offset pud_offset
#endif

#ifndef pgd_offset
#define pgd_offset(mm, address)		pgd_offset_pgd((mm)->pgd, (address))
#endif

static inline pgd_t *pgd_offset_pgd(pgd_t *pgd, unsigned long address)
{
	return (pgd + pgd_index(address));
};
```
其中關於 p4d 的邏輯在
```c=
// arch/x86/include/asm/pgtable.h 

/* to find an entry in a page-table-directory. */
static inline p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
        if (!pgtable_l5_enabled())
                return (p4d_t *)pgd;
        return (p4d_t *)pgd_page_vaddr(*pgd) + p4d_index(address);
}
```
可以看到若不是 5-level paging，則直接回傳```(p4d_t *)pgd```，所以 4-level ```pgd``` = ```p4d```，3-level ```pgd``` = ```p4d``` = ```pud```


```c=
#define pgd_offset(mm, address)		pgd_offset_pgd((mm)->pgd, (address))
```
注意到這行 Code 會把```pgd_offset(mm, address)``` 轉換成右邊的 ```pgd_offset_pgd((mm)->pgd, (address))```


<h1><font color="#F7A004">copy_from_user & copy_to_use</font></h1>

<h2>
    Copy from User
</h2>

```c
unsigned long copy_from_user(void *to, const void __user *from, unsigned long n);
```
這個函數的功能是將 User Space 的資料複製到 Kernel Space。其中：
```to```：目標位址，是 Kernel Space 中的一個指標，用來存放從 User Space 複製過來的資料。
```from```：來源位址，是 User Space 中的一個指標，指向需要被複製的資料（e.g., Point to Virtual Address）。
```n```：要傳送資料的長度。
回傳值：0 on success, or the number of bytes that could not be copied.

<h2>
    Copy to User
</h2>

```c
unsigned long copy_to_user(void __user *to, const void *from, unsigned long n);
```
這個函數的功能是將 Kernel Space 的資料複製到 User Space Variable，其中:
```to```：目標地址（User Space）。
```from```：複製地址（Kernel Space）。
```n```：要傳送資料的長度。
回傳值：0 on success, or the number of bytes that could not be copied.

參考：https://blog.csdn.net/qq_30624591/article/details/88544739

<h1><font color="#F7A004">Kernal Get Addr Code</font></h1>


:::spoiler kernel code
``` C=1
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

#define PTE_PFN_MASK    ((pteval_t)PHYSICAL_PAGE_MASK)
#define PTE_FLAGS_MASK  (~PTE_PFN_MASK)

/*
 * 系統調用：獲取虛擬地址對應的物理地址
 * @user_vaddr_ptr: 用戶空間傳入的虛擬地址指針
 * @paddr_out: 用於存儲物理地址的用戶空間指針
 * 返回值：成功返回0，失敗返回負的錯誤碼
 */
SYSCALL_DEFINE2(my_get_physical_addresses, unsigned long __user *, user_vaddr_ptr, unsigned long __user *, paddr_out)
{
    unsigned long vaddr;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long paddr = 0;

    if (copy_from_user(&vaddr, user_vaddr_ptr, sizeof(vaddr)))
        return -EFAULT;

    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        return -EFAULT;

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d))
        return -EFAULT;

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud))
        return -EFAULT;

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd))
        return -EFAULT;

    pte = pte_offset_kernel(pmd, vaddr);
    if (!pte_present(*pte))
        return -EFAULT;

    paddr = (pte_val(*pte) & PTE_PFN_MASK) | (vaddr & ~PAGE_MASK);

    if (copy_to_user(paddr_out, &paddr, sizeof(paddr)))
        return -EFAULT;

    return 0;
}
```
:::
其中 line 47 為取得實體地址的計算方式。

```pte_val(*pte) & PTE_PFN_MASK```
* ```pte_val(*pte)```取得 Page Table Entry的值，如下圖（64bit PTE）。
    * 由```PFN```以及 Permission Bits（12bit 組成）。

```PTE_PFN_MASK = 0x000FFFFFFFFFF000```，主要用於取得```PFN```的區段```12~51bits```，將此與```PTE```進行```and```運算就可以取得```PFN```。
![image](https://hackmd.io/_uploads/HkcFrcsW1x.png)

```vaddr & ~PAGE_MASK``` 通過```and```運算將```vaddr```保留最後```12bits```，這```12bits```就是```Offset```。
```paddr```就是將前面```PFN```與```Offset```進行```or```運算後得出來的結果。

:::info
```
Virtual addr. of arg a = 0x7ffc80b5f244
Physical addr. of arg a = 0x1b5f2244
```
上面是我寫了一個測試檔宣告了
```c=1
a=10
```
然後試著取得並將其 vaddr & paddr 都印出來後的結果

以下是我自己修改了先前附上的 Kernel Code 並輸出的結果
```=1
[   93.929059] Virtual Address: 0x7ffc80b5f244
[   93.929063] PGD Base: 0xffff88c6449627f8
[   93.929064] PGD Value: 0x800000000795b067
[   93.929065] P4D Base: 0xffff88c6449627f8
[   93.929066] P4D Value: 0x800000000795b067
[   93.929066] PUD Base: 0xffff88c64795bf90
[   93.929067] PUD Value: 0x70fa067
[   93.929067] PMD Base: 0xffff88c6470fa028
[   93.929068] PMD Value: 0x20c1067
[   93.929069] PTE Base: 0xffff88c6420c1af8
[   93.929069] PTE Value: 0x800000001b5f2867
[   93.929070] PFN_MASK: 0x000ffffffffff000
[   93.929071] Page Frame Number: 0x1b5f2000
[   93.929071] Page Offset: 0x244
[   93.929072] Final Physical Address: 0x1b5f2244
```
可以看到其中的第 12 行的 PFN_MASK 跟先前 Kernel Code 解析的內容相同，詳細的計算結果如下
```
PFN_MASK:        0x000ffffffffff000
PTE Value:       0x800000001b5f2867
                 &
Page Frame Num:  0x0000000001b5f2000 
```

```
Virtual Address: 0x7ffc80b5f244
~PAGE_MASK:      0x0000000000000fff
                 &
Page Offset:     0x0000000000000244
```

```
Page Frame Num:  0x0000000001b5f2000
Page Offset:     0x0000000000000244
                 |
Physical Addr:   0x0000000001b5f2244
```

:::


<h1><font color="#F7A004">Compile</font></h1>

在 /usr/src/linux-5.15.137 以下建立一個目錄名為 addr，並創建 my_get_physical_addresses.c 檔。
```
mkdir addr
cd    addr
vim   my_get_physical_addresses.c
```
在同個目錄下再建個 Makefile。
```
vim Makefile
```
```Makefile```功能：告訴 make 如何編譯這些檔案。
```Makefile``` 的主要目的是讓 make 工具知道如何根據 Source Code 的變化來更新和編譯目標檔案，以及確保這些操作按照正確的順序執行。

其中 Makefile 內容。
```
obj-y := my_get_physical_addresses.o
```
接著回上個目錄打開 Makefile 修改，編譯時才會編譯到 addr 目錄。
```
core-y += kernel/ certs/ mm/ fs/ ipc/ security/ crypto/ addr/
```
接著修改 syscall_64.tbl 檔。
```
vim arch/x86/entry/syscalls/syscall_64.tbl
```
加上此行。
```
450	common	my_get_physical_addresses		sys_my_get_physical_addresses
```


編輯 syscalls.h 檔。
```
vim include/linux/syscalls.h
```
在 #endif 前加上此行。
![image](https://hackmd.io/_uploads/B1XV7P9bke.png)

編譯 Kernel。
```
make localmodconfig (建議使用)
make
sudo make modules_install
sudo make install
```
以下介紹幾個常見的configuration commands：
* "make localmodconfig"：會根據系統中當前加載的模組來保留所需的模組，並且也會禁用不必要的模組，所以能夠生成比較精簡版的核心配置。
* "make menuconfig" : 系統會顯示一個文字介面的選單，在選單中可以開啟或關閉各種核心功能，像是：處理器架構支援、檔案系統、網路協定、驅動程式等等。
* "make oldconfig"：系統會自動根據現有的 .config 檔案來設定所有現有的選項，然後只針對那些新增加的配置選項詢問要如何設定，適合在升級核心原始碼的時候使用它。

接著更新Bootloader並重新開機。
```
sudo update-grub
reboot
```
GRUB（Grand Unified Bootloader）是常見的引導程式(boot loader)，它會讓我們選擇要啟動的作業系統或kernel版本然後將對應的Linux kernel載入記憶體，並且將控制權轉交給kernel。

* Linux開機程序:
打開電腦電源後，BIOS會先初始化硬體，進行硬體檢測(POST，Power-On Self-Test)，接著根據預設設置或用戶選擇，從光碟、硬碟等存儲設備中讀取前512 bytes 的資料，稱為主引導記錄(MBR)，而MBR會再告訴電腦該從哪個分區載入引導程式（boot loader），而boot loader則會幫助我們將kernel載入。

<h1><font color="#F7A004">Project 1. Copy on Write</font></h1>



:::spoiler code 
```c=1
#include <stdio.h>
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>
#include <sys/wait.h> 

void * my_get_physical_addresses(void *vaddr){
        unsigned long paddr;

        long result = syscall(450, &vaddr, &paddr);

        return (void *)paddr;
};

int global_a=123;  //global variable

void hello(void)
{                    
    printf("======================================================================================================\n");
}  


int main()
{ 
    int      loc_a;
    void     *parent_use, *child_use;  

    printf("===========================Before Fork==================================\n");             
    parent_use=my_get_physical_addresses(&global_a);
    printf("pid=%d: global variable global_a:\n", getpid());  
    printf("Offset of logical address:[%p]   Physical address:[%p]\n", &global_a,parent_use);              
    printf("========================================================================\n");  


    if(fork())
    { /*parent code*/
        printf("vvvvvvvvvvvvvvvvvvvvvvvvvv  After Fork by parent  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n"); 
        parent_use=my_get_physical_addresses(&global_a);
        printf("pid=%d: global variable global_a:\n", getpid()); 
        printf("******* Offset of logical address:[%p]   Physical address:[%p]\n", &global_a,parent_use); 
        printf("vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n");                      
        wait(NULL);                    
    }
    else
    { /*child code*/

        printf("llllllllllllllllllllllllll  After Fork by child  llllllllllllllllllllllllllllllll\n"); 
        child_use=my_get_physical_addresses(&global_a);
        printf("******* pid=%d: global variable global_a:\n", getpid());  
        printf("******* Offset of logical address:[%p]   Physical address:[%p]\n", &global_a, child_use); 
        printf("llllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllll\n");  
        printf("____________________________________________________________________________\n");  

        /*----------------------- trigger CoW (Copy on Write) -----------------------------------*/    
        global_a=789;

        printf("iiiiiiiiiiiiiiiiiiiiiiiiii  Test copy on write in child  iiiiiiiiiiiiiiiiiiiiiiii\n"); 
        child_use=my_get_physical_addresses(&global_a);
        printf("******* pid=%d: global variable global_a:\n", getpid());  
        printf("******* Offset of logical address:[%p]   Physical address:[%p]\n", &global_a, child_use); 
        printf("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii\n");  
        printf("____________________________________________________________________________\n");                  
        sleep(1000);
    }
}
```
:::


<h3>
    result:
</h3>


```
===========================Before Fork==================================
pid=2205: global variable global_a:
Offset of logical address:[0x618d7c124010]   Physical address:[0x2d12f010]
========================================================================
vvvvvvvvvvvvvvvvvvvvvvvvvv  After Fork by parent  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
pid=2205: global variable global_a:
******* Offset of logical address:[0x618d7c124010]   Physical address:[0x2d12f010]
vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
llllllllllllllllllllllllll  After Fork by child  llllllllllllllllllllllllllllllll
******* pid=2206: global variable global_a:
******* Offset of logical address:[0x618d7c124010]   Physical address:[0x2d12f010]
llllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllll
____________________________________________________________________________
iiiiiiiiiiiiiiiiiiiiiiiiii  Test copy on write in child  iiiiiiiiiiiiiiiiiiiiiiii
******* pid=2206: global variable global_a:
******* Offset of logical address:[0x618d7c124010]   Physical address:[0x35049010]
iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii
____________________________________________________________________________

```
可以看到由於 Child Process 改動 global_a 的值，Kernel 會為 Child Process 分配一個新的 Page，而非再與 Parent 共享 Page。至於 Logical Address，對於 Process 並沒有改變，印出皆相同。
<h1><font color="#F7A004">Project 2. Demand Paging</font></h1>

:::spoiler code
```c=1
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#define __NR_my_get_physical_addresses 450

// 修改函數定義以匹配新的系統調用
void * my_get_physical_addresses(void *vaddr) {
    unsigned long paddr;
    long result = syscall(450, &vaddr, &paddr);
    if (result != 0){
      return NULL;
    }
    return (void *)paddr;
}

int a[2000000]; 
                 


int main()
{ 
int      loc_a;
void     *phy_add;  
    
    // for (int i =0; i <= 1007; i++){
    //   a[i] = 0;
    // }


    for (int i =0; i <= 1008; i++){
      a[i] = 0;
    }


    for (int i = 0; i < 200000; i++)
    {
      phy_add = my_get_physical_addresses(&a[i]);
      printf("global element a[%d]:\n", i);
      if (phy_add != NULL)
        printf("Offset of logical address:[%p]   Physical address:[%p]\n", &a[i], phy_add);
      else
      {
        printf("Offset of logical address:[%p]   Physical address:[%p]\n", &a[i], phy_add);
        break;
      }
    }
}
```
:::

<h3>
    Case 1
</h3>

<h4>
    condition
</h4>

沒有初始化內容，單純的宣告。
```c=1
int a[2000000]; 
```

<h4>
    result:
</h4>

```
global element a[1000]:
Offset of logical address:[0x5f4a47f6efe0]   Physical address:[0x52fedfe0]
global element a[1001]:
Offset of logical address:[0x5f4a47f6efe4]   Physical address:[0x52fedfe4]
global element a[1002]:
Offset of logical address:[0x5f4a47f6efe8]   Physical address:[0x52fedfe8]
global element a[1003]:
Offset of logical address:[0x5f4a47f6efec]   Physical address:[0x52fedfec]
global element a[1004]:
Offset of logical address:[0x5f4a47f6eff0]   Physical address:[0x52fedff0]
global element a[1005]:
Offset of logical address:[0x5f4a47f6eff4]   Physical address:[0x52fedff4]
global element a[1006]:
Offset of logical address:[0x5f4a47f6eff8]   Physical address:[0x52fedff8]
global element a[1007]:
Offset of logical address:[0x5f4a47f6effc]   Physical address:[0x52fedffc]
global element a[1008]:
Offset of logical address:[0x5f4a47f6f000]   Physical address:[(nil)]

```

<h3>
    Case 2:
</h3>

<h4>
    condition
</h4>

初始化內容 a[0]~a[1007]。
```c=1
int a[2000000];
for (int i = 0 ; i <= 1007 ; i++)
{
  a[i] = 0;
}
```

<h4>
    result:
</h4>

```
global element a[1000]:
Offset of logical address:[0x63eeac2b5fe0]   Physical address:[0x15a1cfe0]
global element a[1001]:
Offset of logical address:[0x63eeac2b5fe4]   Physical address:[0x15a1cfe4]
global element a[1002]:
Offset of logical address:[0x63eeac2b5fe8]   Physical address:[0x15a1cfe8]
global element a[1003]:
Offset of logical address:[0x63eeac2b5fec]   Physical address:[0x15a1cfec]
global element a[1004]:
Offset of logical address:[0x63eeac2b5ff0]   Physical address:[0x15a1cff0]
global element a[1005]:
Offset of logical address:[0x63eeac2b5ff4]   Physical address:[0x15a1cff4]
global element a[1006]:
Offset of logical address:[0x63eeac2b5ff8]   Physical address:[0x15a1cff8]
global element a[1007]:
Offset of logical address:[0x63eeac2b5ffc]   Physical address:[0x15a1cffc]
global element a[1008]:
Offset of logical address:[0x63eeac2b6000]   Physical address:[(nil)]
```

<h3>
    Case 3:
</h3>

<h4>
    condition
</h4>

初始化內容 a[0]~a[1008]。
```c=1
int a[2000000];
for (int i = 0; i <= 1008 ; i++)
{
  a[i] = 0;
}
```

<h4>
    result:
</h4>

```
global element a[1006]:
Offset of logical address:[0x57ec7b642ff8]   Physical address:[0x5b446ff8]
global element a[1007]:
Offset of logical address:[0x57ec7b642ffc]   Physical address:[0x5b446ffc]
global element a[1008]:
Offset of logical address:[0x57ec7b643000]   Physical address:[0x5b487000]
global element a[1009]:
Offset of logical address:[0x57ec7b643004]   Physical address:[0x5b487004]
global element a[1010]:
Offset of logical address:[0x57ec7b643008]   Physical address:[0x5b487008]
global element a[1011]:
Offset of logical address:[0x57ec7b64300c]   Physical address:[0x5b48700c]
global element a[1012]:
Offset of logical address:[0x57ec7b643010]   Physical address:[0x5b487010]
global element a[1013]:
Offset of logical address:[0x57ec7b643014]   Physical address:[0x5b487014]

```
```
global element a[2026]:
Offset of logical address:[0x57ec7b643fe8]   Physical address:[0x5b487fe8]
global element a[2027]:
Offset of logical address:[0x57ec7b643fec]   Physical address:[0x5b487fec]
global element a[2028]:
Offset of logical address:[0x57ec7b643ff0]   Physical address:[0x5b487ff0]
global element a[2029]:
Offset of logical address:[0x57ec7b643ff4]   Physical address:[0x5b487ff4]
global element a[2030]:
Offset of logical address:[0x57ec7b643ff8]   Physical address:[0x5b487ff8]
global element a[2031]:
Offset of logical address:[0x57ec7b643ffc]   Physical address:[0x5b487ffc]
global element a[2032]:
Offset of logical address:[0x57ec7b644000]   Physical address:[(nil)]

```



存取到a[1008]時為不同 Page，會換一個實體地址，然後配置連續的實體記憶體，注意到換的這個實體記憶體的地址和上一個 Page 的地址不同。

<h1><font color="#F7A004">研究問題</font></h1>


在 Case 1、Case 2 都只會印出 a[0]~a[1007] 的地址，共 1008 個元素，每個元素 4B，所以有 4032B，一個 Page 4KB = 4096B，4096B - 4032B = 64B 可能保留作其他用途？

Case 3 會多印出 a[1008]~a[2031] 的地址，共 1024 個元素，每個元素 4B，所以有 4096B，一個 Page 4KB = 4096B，恰好為一個 Page 大小

解:目前推斷是 BSS 的原因，當程式剛被加載的時候會去看如果我們宣告的是一個全域且資料為零的資料時，系統就會將這些使用 BSS Segment 儲存因此分配到的記憶體空間才會不到一個 Page。


[解決虛擬機disc不夠大的問題，配置更大的空間](https://blog.csdn.net/m0_65745608/article/details/125674749)

<h1><font color="#F7A004">參考資料</font></h1>

* [Kernel 的替換 & syscall 的添加](https://satin-eyebrow-f76.notion.site/Kernel-syscall-3ec38210bb1f4d289850c549def29f9f)
* [bootlin linux v5.15.137](https://elixir.bootlin.com/linux/v5.15.137/source/include/linux)
* [add a system call to kernel (v5.15.137)](https://hackmd.io/aist49C9R46-vaBIlP3LDA?view)
* [Linux Kernel](https://hackmd.io/@eugenechou/H1LGA9AiB#Linux-Kernel)







