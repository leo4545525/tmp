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


    /*for (int i =0; i < 2000000; i++){
      a[i] = 0;
    }*/


    for (int i = 0; i < 2000000; i++)
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
