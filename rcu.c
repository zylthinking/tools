#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sched.h>
#include "lock.h"
#include "rcu.h"

struct rcu_wait_queue {
    struct list_head entry;
    void* any;
    void (*task) (void* any);
    int* intp;
};

static int lck = 0;
static pthread_t tid = 0;
static struct list_head head;

#define read_done(stat, snap, nr) \
({ \
    int done = 1; \
    for (unsigned int i = 0; i < nr; ++i) { \
        if (stat[i] == snap[i] && stat[i] != 1 && stat[i] != 0) { \
            done = 0; \
            break; \
        } \
    } \
    done; \
})

static void wakeup(struct rcu* rcu)
{
    lock(&rcu->lck);
    for (struct list_head* ent = rcu->sync.next; ent != &rcu->sync;) {
        struct rcu_wait_queue* wait = list_entry(ent, struct rcu_wait_queue, entry);
        ent = ent->next;
        unlock(&rcu->lck);
        
        int wake = read_done(rcu->tlsp, wait->intp, rcu->nr);

        lock(&rcu->lck);
        if (wake == 1) {
            list_del(&wait->entry);
            wait->task(wait->any);
        }
    }
    unlock(&rcu->lck);

    int wake = read_done(rcu->tlsp, rcu->intp, rcu->nr);
    if (wake == 1) {
        while (!list_empty(&rcu->async)) {
            struct list_head* ent = rcu->async.next;
            list_del(ent);
            struct rcu_wait_queue* wait = list_entry(ent, struct rcu_wait_queue, entry);
            wait->task(wait->any);
            free(wait);
        }
    }

    if (list_empty(&rcu->async)) {
        lock(&rcu->lck);
        if (!list_empty(&rcu->nxtlist)) {
            memcpy(rcu->intp, rcu->tlsp, rcu->nr * sizeof(int));
            list_add(&rcu->async, &rcu->nxtlist);
            list_del_init(&rcu->nxtlist);
        }
        unlock(&rcu->lck);
    }
}

static void* rcu_daemon(void* any)
{
    (void) any;

    while (1) {
        lock(&lck);
        struct list_head* ent = head.next;
        if (ent == &head) {
            tid = 0;
            unlock(&lck);
            break;
        }
        unlock(&lck);

        for (; ent != &head;) {
            struct rcu* rcu = list_entry(ent, struct rcu, entry);
            ent = ent->next;
            if (rcu->zombie == 1) {
                lock(&lck);
                list_del(&rcu->entry);
                unlock(&lck);
                free(rcu);
            } else {
                wakeup(rcu);
            }
        }
        sched_yield();
    }
    return NULL;
}

struct rcu* rcu_alloc(unsigned int rd_max)
{
    if (rd_max < 1) {
        return NULL;
    }

    size_t bytes = sizeof(int) * (rd_max * 4);
    struct rcu* rcu = (struct rcu*) malloc(sizeof(struct rcu) + bytes);
    if (rcu == NULL) {
        return NULL;
    }

    rcu->zombie = 0;
    rcu->nr = rd_max;
    rcu->tlsp = (int *) (rcu + 1);
    rcu->refp = rcu->tlsp + rd_max;
    rcu->intp = rcu->refp + rd_max;
    rcu->tckp = rcu->intp + rd_max;
    memset(rcu->tlsp, 0, bytes);
    INIT_LIST_HEAD(&rcu->async);
    INIT_LIST_HEAD(&rcu->sync);
    INIT_LIST_HEAD(&rcu->nxtlist);

    lock(&lck);
    list_add(&rcu->entry, &head);
    if (tid == 0) {
        if(0 != pthread_create(&tid, NULL, rcu_daemon, NULL)) {
            tid = 0;
            free(rcu);
            rcu = NULL;
        } else {
            pthread_detach(tid);
        }
    }
    unlock(&lck);

    return rcu;
}

int rcu_thread_init(struct rcu* rcu, int* (*idp) [3])
{
    int n = -1;
    if (rcu == NULL || idp == NULL) {
        return -1;
    }

    lock(&rcu->lck);
    for (unsigned int i = 0; i < rcu->nr; ++i) {
        if (rcu->tlsp[i] == 0) {
            rcu->tlsp[i] = 1;
            (*idp)[0] = &rcu->tlsp[i];
            (*idp)[1] = &rcu->refp[i];
            (*idp)[2] = &rcu->tckp[i];
            n = 0;
            break;
        }
    }
    unlock(&rcu->lck);
    return n;
}

int call_rcu(struct rcu* rcu, void (* cb) (void* any), void* any)
{
    struct rcu_wait_queue* wait = (struct rcu_wait_queue *) malloc(sizeof(struct rcu_wait_queue));
    if (wait == NULL) {
        return -1;
    }

    INIT_LIST_HEAD(&wait->entry);
    wait->intp = NULL;
    wait->task = cb;
    wait->any = any;

    lock(&rcu->lck);
    list_add(&wait->entry, &rcu->nxtlist);
    unlock(&rcu->lck);
    return 0;
}

static void semup(void* any)
{
    sem_t* sem = (sem_t *) any;
    sem_post(sem);
}

int synchronize_rcu(struct rcu* rcu)
{
    struct rcu_wait_queue wait = {
        .entry = LIST_HEAD_INIT(wait.entry),
        .intp = NULL,
        .task = semup,
    };

    size_t bytes = sizeof(int) * rcu->nr;
    wait.intp = (int *) malloc(bytes);
    if (wait.intp == NULL) {
        return -1;
    }

    smp_rmb();
    memcpy(wait.intp, rcu->tlsp, sizeof(int) * rcu->nr);

    int wake = read_done(rcu->tlsp, wait.intp, rcu->nr);
    if (wake == 1) {
        free(wait.intp);
        return 0;
    }

    sem_t sem;
    if (0 != sem_init(&sem, 0, 0)) {
        free(wait.intp);
        return -1;
    }
    wait.any = (void *) &sem;

    lock(&rcu->lck);
    list_add(&wait.entry, &rcu->sync);
    unlock(&rcu->lck);

    sem_wait(&sem);
    free(wait.intp);
    sem_destroy(&sem);

    return 0;
}
