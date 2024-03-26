// 编译: gcc -o deadlock deadlock.c -lpthread -ldl
// 使用时,将:
// init_hook();
// start_check();
// 两个函数添加到程序的main函数中,即可以实现线程死锁的检测
// 原理:通过线程之间持有和进程资源的关系构建有向图,检测有向图中是否存在环来判定是否死锁并定位死锁的线程. 

#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <stdint.h>

// 构建有向图
#if 1

typedef unsigned long int uint64;

#define MAX		100

enum Type {PROCESS, RESOURCE};//进程 资源

// 节点类型，可以是进程或资源
struct source_type {
    uint64 id;        // 节点唯一标识
    enum Type type;   // 节点类型：进程或资源
    uint64 lock_id;   // 锁的标识
    int degress;      // 节点的出度
};

// 有向图中的节点
struct vertex {
    struct source_type s; // 节点信息
    struct vertex *next;  // 指向下一个节点的指针
};

// 任务图
struct task_graph {
    struct vertex list[MAX];      // 节点列表
    int num;                      // 节点数量
    struct source_type locklist[MAX];  // 锁列表
    int lockidx;                  // 锁索引
    pthread_mutex_t mutex;        // 互斥锁
};

struct task_graph *tg = NULL;
int path[MAX+1];
int visited[MAX];
int k = 0;
int deadlock = 0;

// 创建一个新的节点
struct vertex *create_vertex(struct source_type type) {
    struct vertex *tex = (struct vertex *)malloc(sizeof(struct vertex));
    tex->s = type;
    tex->next = NULL;
    return tex;
}

// 查找节点
int search_vertex(struct source_type type) {
    int i = 0;
    for (i = 0; i < tg->num; i++) {
        if (tg->list[i].s.type == type.type && tg->list[i].s.id == type.id) {
            return i;
        }
    }
    return -1;
}

// 添加节点
void add_vertex(struct source_type type) {
    if (search_vertex(type) == -1) {
        tg->list[tg->num].s = type;
        tg->list[tg->num].next = NULL;
        tg->num++;
    }
}

// 添加边
int add_edge(struct source_type from, struct source_type to) {
    add_vertex(from);
    add_vertex(to);
    struct vertex *v = &(tg->list[search_vertex(from)]);
    while (v->next != NULL) {
        v = v->next;
    }
    v->next = create_vertex(to);
}

// 验证边是否存在
int verify_edge(struct source_type i, struct source_type j) {
    if (tg->num == 0) return 0;
    int idx = search_vertex(i);
    if (idx == -1) {
        return 0;
    }
    struct vertex *v = &(tg->list[idx]);
    while (v != NULL) {
        if (v->s.id == j.id) return 1;
        v = v->next;
    }
    return 0;
}

// 移除边
int remove_edge(struct source_type from, struct source_type to) {
    int idxi = search_vertex(from);
    int idxj = search_vertex(to);
    if (idxi != -1 && idxj != -1) {
        struct vertex *v = &tg->list[idxi];
        struct vertex *remove;
        while (v->next != NULL) {
            if (v->next->s.id == to.id) {
                remove = v->next;
                v->next = v->next->next;
                free(remove);
                break;
            }
            v = v->next;
        }
    }
}

// 打印检测到的死锁路径
void print_deadlock(void) {
    int i = 0;
    printf("cycle : ");
    for (i = 0; i < k - 1; i++) {
        printf("%ld --> ", tg->list[path[i]].s.id);
    }
    printf("%ld\n", tg->list[path[i]].s.id);
}

// 深度优先搜索
int DFS(int idx) {
    struct vertex *ver = &tg->list[idx];
    if (visited[idx] == 1) {
        path[k++] = idx;
        print_deadlock();
        deadlock = 1;
        return 0;
    }
    visited[idx] = 1;
    path[k++] = idx;
    while (ver->next != NULL) {
        DFS(search_vertex(ver->next->s));
        k--;
        ver = ver->next;
    }
    return 1;
}

// 搜索环
int search_for_cycle(int idx) {
    struct vertex *ver = &tg->list[idx];
    visited[idx] = 1;
    k = 0;
    path[k++] = idx;
    while (ver->next != NULL) {
        int i = 0;
        for (i = 0; i < tg->num; i++) {
            if (i == idx) continue;
            visited[i] = 0;
        }
        for (i = 1; i <= MAX; i++) {
            path[i] = -1;
        }
        k = 1;
        DFS(search_vertex(ver->next->s));
        ver = ver->next;
    }
}

#endif

#if 1

// 搜索锁
int search_lock(uint64 lock) {
    int i = 0;
    for (i = 0; i < tg->lockidx; i++) {
        if (tg->locklist[i].lock_id == lock) {
            return i;
        }
    }
    return -1;
}

// 搜索空锁
int search_empty_lock(uint64 lock) {
    int i = 0;
    for (i = 0; i < tg->lockidx; i++) {
        if (tg->locklist[i].lock_id == 0) {
            return i;
        }
    }
    return tg->lockidx;
}

// 锁定前处理
void lock_before(uint64_t tid, uint64_t lockaddr) {
    int idx = 0;
    for (idx = 0; idx < tg->lockidx; idx++) {
        if (tg->locklist[idx].lock_id == lockaddr) {
            struct source_type from;
            from.id = tid;
            from.type = PROCESS;
            add_vertex(from);
            struct source_type to;
            to.id = tg->locklist[idx].id;
            to.type = PROCESS;
            add_vertex(to);
            tg->locklist[idx].degress++;
            if (!verify_edge(from, to)){
                add_edge(from, to);
            }   
        }
    }    
}

// 锁定后处理
void lock_after(uint64_t tid, uint64_t lockaddr) {
    int idx = 0;
    if (-1 == (idx = search_lock(lockaddr))) {
        int eidx = search_empty_lock(lockaddr);
        tg->locklist[eidx].id = tid;
        tg->locklist[eidx].lock_id = lockaddr;
        tg->lockidx++;
    } else {
        struct source_type from;
        from.id = tid;
        from.type = PROCESS;
        add_vertex(from);
        struct source_type to;
        to.id = tg->locklist[idx].id;
        to.type = PROCESS;
        add_vertex(to);
        tg->locklist[idx].degress--;
        if (verify_edge(from, to))
            remove_edge(from, to);
        tg->locklist[idx].id = tid;
    }
}

// 解锁后处理
void unlock_after(uint64_t tid, uint64_t lockaddr) {
    int idx = search_lock(lockaddr);
    if (tg->locklist[idx].degress == 0) {
        tg->locklist[idx].id = 0;
        tg->locklist[idx].lock_id = 0;
    }
}

// 检测死锁
void check_dead_lock(void) {
    int i = 0;
    deadlock = 0;
    for (i = 0; i < tg->num; i++) {
        if (deadlock == 1) break;
        search_for_cycle(i);
    }
    if (deadlock == 0) {
        printf("no deadlock\n");
    }
}

// 线程例程，用于周期性地检测死锁
static void *thread_routine(void *args) {
    while (1) {
        sleep(5);
        check_dead_lock();
    }
}

// 启动死锁检测线程
void start_check(void) {
    tg = (struct task_graph*)malloc(sizeof(struct task_graph));
    tg->num = 0;
    tg->lockidx = 0;
    pthread_t tid;
    pthread_create(&tid, NULL, thread_routine, NULL);
}

// 钩子函数部分
// 定义函数指针类型 pthread_mutex_lock_t，用于指向原始的 pthread_mutex_lock 函数
typedef int (*pthread_mutex_lock_t)(pthread_mutex_t *mutex);
pthread_mutex_lock_t pthread_mutex_lock_f = NULL;

// 定义函数指针类型 pthread_mutex_unlock_t，用于指向原始的 pthread_mutex_unlock 函数
typedef int (*pthread_mutex_unlock_t)(pthread_mutex_t *mutex);
pthread_mutex_unlock_t pthread_mutex_unlock_f = NULL;

// implement
// 重定义 pthread_mutex_lock 函数
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    // 获取当前线程 ID
    pthread_t selfid = pthread_self();
    // 调用锁定前的处理函数
    lock_before((uint64_t)selfid, (uint64_t)mutex);
    // 调用原始的 pthread_mutex_lock 函数
    pthread_mutex_lock_f(mutex);
    // 调用锁定后的处理函数
    lock_after((uint64_t)selfid, (uint64_t)mutex);
}

// 重定义 pthread_mutex_unlock 函数
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    // 调用原始的 pthread_mutex_unlock 函数
    pthread_mutex_unlock_f(mutex);
    // 获取当前线程 ID
    pthread_t selfid = pthread_self();
    // 调用解锁后的处理函数
    unlock_after((uint64_t)selfid, (uint64_t)mutex);
}


// 钩子函数初始化
void init_hook(void) {
    if (!pthread_mutex_lock_f)
        pthread_mutex_lock_f = dlsym(RTLD_NEXT, "pthread_mutex_lock");
    if (!pthread_mutex_unlock_f)
        pthread_mutex_unlock_f = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
}

#endif

// 示例代码
#if 0

pthread_mutex_t r1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r3 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r4 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r5 = PTHREAD_MUTEX_INITIALIZER;

// 线程回调函数
void *t1_cb(void *arg) {
    pthread_mutex_lock(&r1);
    sleep(1);
    pthread_mutex_lock(&r2);
    pthread_mutex_unlock(&r2);
    pthread_mutex_unlock(&r1);
}

void *t2_cb(void *arg) {
    pthread_mutex_lock(&r2);
    sleep(1);
    pthread_mutex_lock(&r3);
    pthread_mutex_unlock(&r3);
    pthread_mutex_unlock(&r2);
}

void *t3_cb(void *arg) {
    pthread_mutex_lock(&r3);
    sleep(1);
    pthread_mutex_lock(&r4);
    pthread_mutex_unlock(&r4);
    pthread_mutex_unlock(&r3);
}

void *t4_cb(void *arg) {
    pthread_mutex_lock(&r4);
    sleep(1);
    pthread_mutex_lock(&r5);
    pthread_mutex_unlock(&r5);
    pthread_mutex_unlock(&r4);
}

void *t5_cb(void *arg) {
    pthread_mutex_lock(&r5);
    sleep(1);
    pthread_mutex_lock(&r1);
    pthread_mutex_unlock(&r1);
    pthread_mutex_unlock(&r5);
}

// 主函数
int main() {
    init_hook();
    start_check();

    pthread_t t1, t2, t3, t4, t5;

    pthread_create(&t1, NULL, t1_cb, NULL);
    pthread_create(&t2, NULL, t2_cb, NULL);

    pthread_create(&t3, NULL, t3_cb, NULL);
    pthread_create(&t4, NULL, t4_cb, NULL);
    pthread_create(&t5, NULL, t5_cb, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);
    pthread_join(t5, NULL);

    printf("complete\n");

    return 0;
}

#endif
