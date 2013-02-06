
#ifndef rcu_h
#define rcu_h

#include "list_head.h"

struct rcu {
    int lck;
    int zombie;
    struct list_head sync, async, nxtlist, entry;
    unsigned int nr;
    int* tlsp;
    int* intp;
    int* refp;
    int* tckp;
};
#define ddref(idp, x) (*((*idp)[x]))

#ifdef __cplusplus
extern "C" {
#endif
    struct rcu* rcu_alloc(unsigned int rd_max);
    #define rcu_free(rcu) do {rcu->zombie = 1; rcu = NULL;} while (0)

    int rcu_thread_init(struct rcu* rcu, int* (*idp)[3]);
    #define rcu_thread_deinit(idp) do {assert(ddref(idp, 1) == 0); ddref(idp, 0) = 0;} while (0)

    #define rcu_read_lock(idp) do { \
        ++ddref(idp, 1); \
        if (ddref(idp, 1) == 1) { \
            ++ddref(idp, 2); \
            if (ddref(idp, 2) == 1 || ddref(idp, 2) == 0) { \
                ddref(idp, 2) = 2; \
            } \
            ddref(idp, 0) = ddref(idp, 2); \
            smp_mb(); \
        } \
    } while (0)

    #define rcu_read_unlock(idp) do { \
        --ddref(idp, 1); \
        if (ddref(idp, 1) == 0) { \
            smp_mb(); \
            ddref(idp, 0) = 1; \
    } while (0)

    #define rcu_dereference(p) ({ \
        typeof(p) _________p1 = p; \
        smp_read_barrier_depends(); \
        (_________p1); \
    })

    #define rcu_assign_pointer(p, v) ({ \
        smp_wmb(); \
        (p) = (v); \
    })

    int call_rcu(struct rcu* rcu, void (* cb) (void* any), void* any);
    int synchronize_rcu(struct rcu* rcu);
#ifdef __cplusplus
}
#endif
#endif
