```
組別:第39組
113525008 沈育安
113522049 鄭鼎立
113522059 陳奕昕
113522128 林芷筠
```
<h1><font color="#F7A004">Goal</font></h1>

 實作一個Wait Queue，並驗證從Queue離開的順序為Random或FIFO
 
 <h1><font color="#F7A004">System info</font></h1>

```
OS: Ubuntu-22.04.5-desktop-amd64
Kernel Version: 5.15.137
Vitual Machine: VMware
VM Memory: 4GB
```

<h1><font color="#F7A004">User code</font></h1>




:::spoiler wait_queue.c
```c=
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>

#define NUM_THREADS 10
#define SYS_CALL_MY_WAIT_QUEUE 451

void *enter_wait_queue(void *thread_id)
{
    fprintf(stderr, "enter wait queue thread_id: %d\n", *(int *)thread_id);

    // 使用系統呼叫 451，id 為 1 表示加入等待隊列
    syscall(SYS_CALL_MY_WAIT_QUEUE, 1);

    fprintf(stderr, "exit wait queue thread_id: %d\n", *(int *)thread_id);
    return NULL;
}

void *clean_wait_queue()
{
    // 使用系統呼叫 451，id 為 2 表示清除等待隊列並喚醒所有執行緒
    syscall(SYS_CALL_MY_WAIT_QUEUE, 2);
    return NULL;
}

int main()
{
    void *ret;
    pthread_t id[NUM_THREADS];
    int thread_args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++)
    {
        thread_args[i] = i;
        pthread_create(&id[i], NULL, enter_wait_queue, (void *)&thread_args[i]);
    }

    sleep(1);

    fprintf(stderr, "start clean queue ...\n");
    clean_wait_queue();

    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(id[i], &ret);
    }

    return 0;
}
```
:::

<h1><font color="#F7A004">Kernel code</font></h1>


 
:::spoiler call_my_wait_queue.c
```c=
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/mutex.h>
#include <linux/delay.h>  // 加入 udelay 的標頭檔

// 宣告等待隊列
static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static int condition = 0;

// Mutex 保護共享變數
static DEFINE_MUTEX(condition_mutex);

// 追蹤等待中的執行緒
static struct task_struct *waiting_tasks[1024];
static int task_count = 0;

SYSCALL_DEFINE1(call_my_wait_queue, int, id)
{
    int i;  // 將變數宣告移到function開頭

    switch (id) {
    case 1:
        // 加入等待隊列，進入休眠
        mutex_lock(&condition_mutex);
        condition = 0;
        // 儲存目前執行緒的 task_struct
        waiting_tasks[task_count++] = current;
        printk("store a process%d\n", current->pid);
        mutex_unlock(&condition_mutex);

        // 等待條件變為非零
        wait_event_interruptible(my_wait_queue, condition != 0);

        return 1;

    case 2:
        mutex_lock(&condition_mutex);
        condition = 1;
        
        // 依序喚醒每個執行緒
        for (i = 0; i < task_count; i++) {
            if (waiting_tasks[i]) {
                wake_up_process(waiting_tasks[i]);
                msleep(100);  // 短暫延遲確保順序
                printk(KERN_INFO "[Wake Queue] Process %d (task: %s) woken up, remaining tasks: %d\n",
		       waiting_tasks[i]->pid,           // 進程 ID
		       waiting_tasks[i]->comm,          // 進程名稱
		       task_count - i - 1); 
            }
        }
        
        // 重置計數器
        task_count = 0;
        mutex_unlock(&condition_mutex);

        return 1;

    default:
        return -EINVAL;
    }
}
:::

:::info
從25行開始的switch功能概述

 `id=1`: 將執行緒加入等待隊列並進入休眠
 
 當呼叫者傳入 `id=1` 時：
 透過 `mutex` 保護共享變數 `condition`，將其設為 0，表示需要等待條件被改變。接著呼叫該系統呼叫的執行緒 (`current`) 的 `task_struct` 儲存到陣列 `waiting_tasks` 中，用以追蹤加入等待隊列的執行緒。使用 `mutex` 來保護共享變數，注意到`condition` 和 `waiting_tasks` 是多執行緒共享的變數，為避免 race condition，需要透過 `mutex` 鎖住操作區域來保證原子性:某個執行緒若執行一個原子操作 (atomic operation)，另一個執行緒無法看到該操作中的半完成狀態，只有操作前跟操作後兩個狀態。

 執行 `wait_event_interruptible()`，讓該執行緒進入休眠狀態，直到 `condition != 0` 時被喚醒，此機制能有效避免 busy-waiting，降低 CPU 資源的浪費。
 
 `id=2`: 喚醒等待隊列中的所有執行緒

 當呼叫者傳入 `id=2` 時：
 透過 `mutex` 保護共享變數 `condition`，將其設為 1，表示條件已滿足，可以喚醒所有等待的執行緒。遍歷 `waiting_tasks` 陣列中的所有執行緒，逐一使用 `wake_up_process()` 來喚醒。使用 `msleep(100)` 延遲喚醒執行緒之間的時間，確保喚醒順序較為清楚。在喚醒過程中，透過 printk 輸出訊息來記錄每個被喚醒的執行緒（包括其進程 ID 和名稱），以及剩餘尚未被喚醒的執行緒數量。
 
喚醒完成後，重置 `task_count` 為 0，準備下一次操作。

無效輸入 (default)
當 id 不為 1 或 2 時，return -EINVAL，表示無效參數。
:::

* 用sleep的理由
確保喚醒執行緒的順序在輸出上更為清楚

* FIFO 喚醒效果：

    透過 waiting_tasks 陣列記錄加入等待隊列的順序，結合延遲 (msleep(100))，可以較好地模擬 FIFO 喚醒效果
    
    

注意到程式的休眠有`msleep` 和 `sleep` 兩種
* msleep (Kernel API)，精度是ms
* sleep (User-space Function)，精度是s

由於在kernel中作修改所以我們用msleep()
    

<h1><font color="#F7A004">Trace important function calls</font></h1>

> In linux/v5.15.137/source/include/linux/wait.h
:::spoiler wait_event_interruptible(wq_head, condition)
```c=
/**
 * wait_event_interruptible - sleep until a condition gets true
 * @wq_head: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 *
 * The process is put to sleep (TASK_INTERRUPTIBLE) until the
 * @condition evaluates to true or a signal is received.
 * The @condition is checked each time the waitqueue @wq_head is woken up.
 *
 * wake_up() has to be called after changing any variable that could
 * change the result of the wait condition.
 *
 * The function will return -ERESTARTSYS if it was interrupted by a
 * signal and 0 if @condition evaluated to true.
 */
#define wait_event_interruptible(wq_head, condition)				\
({										\
	int __ret = 0;								\
	might_sleep();								\
	if (!(condition))							\
		__ret = __wait_event_interruptible(wq_head, condition);		\
	__ret;									\
})
```
:::

`might_sleep()`

這是一個kernel檢查function，用來確保當前context允許進入睡眠
如果在不允許睡眠的context中使用該function（例如，ISR或持有 spinlock），可能會觸發警告。

檢查條件是否已滿足 `(if (!(condition)))`

* 如果 `condition` 已經為 `true`，說明執行緒無需等待，直接return 0。
* 如果 `condition` 為 `false`，則執行`__wait_event_interruptible()`。


---

`__wait_event_interruptible()`

這是一個內部function，負責具體實現等待邏輯。其功能是：
將當前執行緒掛到waiting queue `wq_head`上，並設置狀態為 `TASK_INTERRUPTIBLE`。

持續檢查 `condition` 是否變為 `true`。
如果條件滿足，`return 0`；如果在等待過程中收到信號，則return `-ERESTARTSYS`。


---

return結果 (`__ret`)

如果 `condition` 一開始就滿足，`return 0`。
如果通過等待（`__wait_event_interruptible`）被喚醒後，根據狀態return：
`0` 表示條件滿足並正常喚醒。
`-ERESTARTSYS` 表示因信號中斷等待。

---
> In linux/v5.15.137/source/kernel/sched/core.c
:::spoiler wake_up_process
```c=
/**
 * wake_up_process - Wake up a specific process
 * @p: The process to be woken up.
 *
 * Attempt to wake up the nominated process and move it to the set of runnable
 * processes.
 *
 * Return: 1 if the process was woken up, 0 if it was already running.
 *
 * This function executes a full memory barrier before accessing the task state.
 */
int wake_up_process(struct task_struct *p)
{
	return try_to_wake_up(p, TASK_NORMAL, 0);
}
```
:::

`try_to_wake_up` 是實際執行喚醒邏輯的核心函式。
該函式將目標執行緒狀態改變為 `TASK_RUNNING`，並執行必要的同步與調度操作。
第一個參數是目標執行緒的指標 p。
第二個參數是目標執行緒的新狀態，這裡傳入 `TASK_NORMAL`，代表常規喚醒。
第三個參數是附加的標誌，這裡傳入 0，代表沒有特殊行為。
* return 1：

表示執行緒原本處於非運行狀態（例如 `TASK_INTERRUPTIBLE` 或 `TASK_UNINTERRUPTIBLE`），且成功被喚醒，進入 `TASK_RUNNING` 狀態。
* return 0：

表示執行緒已經處於 `TASK_RUNNING` 狀態，無需進一步操作。

<h1><font color="#F7A004">Results</font></h1>

使用`dmesg`來查看kernel code result
```
\\ user code result
enter wait queue thread_id: 0
enter wait queue thread_id: 2
enter wait queue thread_id: 1
enter wait queue thread_id: 4
enter wait queue thread_id: 3
enter wait queue thread_id: 7
enter wait queue thread_id: 6
enter wait queue thread_id: 9
enter wait queue thread_id: 5
enter wait queue thread_id: 8
start clean queue ...
exit wait queue thread_id: 0
exit wait queue thread_id: 2
exit wait queue thread_id: 1
exit wait queue thread_id: 4
exit wait queue thread_id: 3
exit wait queue thread_id: 7
exit wait queue thread_id: 6
exit wait queue thread_id: 9
exit wait queue thread_id: 5
exit wait queue thread_id: 8


\\kernel code result
[   46.937032] store a process2283
[   46.937143] store a process2285
[   46.937147] store a process2284
[   46.937222] store a process2287
[   46.937288] store a process2286
[   46.937291] store a process2290
[   46.937346] store a process2289
[   46.937366] store a process2292
[   46.937420] store a process2288
[   46.937519] store a process2291
[   48.034318] [Wake Queue] Process 2283 (task: wait_queue) woken up, remaining tasks: 9
[   48.137808] [Wake Queue] Process 2285 (task: wait_queue) woken up, remaining tasks: 8
[   48.240913] [Wake Queue] Process 2284 (task: wait_queue) woken up, remaining tasks: 7
[   48.343952] [Wake Queue] Process 2287 (task: wait_queue) woken up, remaining tasks: 6
[   48.447477] [Wake Queue] Process 2286 (task: wait_queue) woken up, remaining tasks: 5
[   48.550813] [Wake Queue] Process 2290 (task: wait_queue) woken up, remaining tasks: 4
[   48.654006] [Wake Queue] Process 2289 (task: wait_queue) woken up, remaining tasks: 3
[   48.757331] [Wake Queue] Process 2292 (task: wait_queue) woken up, remaining tasks: 2
[   48.860805] [Wake Queue] Process 2288 (task: wait_queue) woken up, remaining tasks: 1
[   48.964076] [Wake Queue] Process 2291 (task: wait_queue) woken up, remaining tasks: 0

```
