

#include"buddy.h"
#include<assert.h>

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include<stdlib.h>

#define IS_POWER_OF_2(x) (!((x) & ((x) - 1)))
#define LEFT_LEAF(index) ((index) * 2 + 1)
#define RIGHT_LEAF(index) ((index) * 2 + 2)
#define PARENT(index) (((index) + 1) / 2 - 1)
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static unsigned char next_order_of_2(int size)
{
    unsigned char order = 0;
    while ((1 << order) < size) {
        order++;
    }
    return order;
}

struct buddy_pool* buddy_create(unsigned int order, unsigned int min_order)
{
    struct buddy_pool* self = NULL;
    if (order >= 64 || min_order >= order ||
        NULL == (self = malloc(sizeof(struct buddy_pool))))
    {
        goto malloc_self_failed;
    }

    self->order = order;
    self->min_order = min_order;
    self->pool_size = 1 << (order);
    int bh_num = (1 << (order - self->min_order)) * 2 - 1;

    int ret = posix_memalign((void **)&(self->bh), 4096, bh_num);
    if (ret < 0) {
        goto malloc_bh_failed;
    }

    ret = posix_memalign((void **)&(self->buffer), 4096, self->pool_size);
    if (ret < 0) {
        goto malloc_buffer_failed;
    }

    int current_order = order + 1;
    for (int i = 0; i < bh_num; i++) {
        if (IS_POWER_OF_2(i + 1)) {
            current_order--;
        }
        self->bh[i] = current_order;
    }
    return self;

malloc_buffer_failed:
    free(self->bh);
malloc_bh_failed:
    free(self);
malloc_self_failed:
    return (struct buddy_pool *) (-1);
}

void buddy_destroy(struct buddy_pool* self)
{
    assert(self != NULL);
    assert(self->bh != NULL);
    assert(self->buffer != NULL);
    free(self->buffer);
    free(self->bh);
    free(self);

}

char* buddy_malloc(struct buddy_pool* self, int size)
{
    char* ret_ptr = NULL;
    if (size <= 0) {
        return NULL;
    }

    unsigned char order = next_order_of_2(size);
    if (order < self->min_order) {
        order = self->min_order;
    }

    int index = 0;
    if (self->bh[0] < order) {
        return NULL;
    }

    int current_order = self->order;
    for (current_order = self->order; current_order != order; current_order--) {
        if (self->bh[LEFT_LEAF(index)] >= order) {
            index = LEFT_LEAF(index);
        } else {
            index = RIGHT_LEAF(index);
        }
    }

    self->bh[index] = 0;
    ret_ptr = self->buffer + (index + 1) * (1 << order) - (self->pool_size);
    while (index) {
        index = PARENT(index);
        self->bh[index] = MAX(self->bh[LEFT_LEAF(index)], self->bh[RIGHT_LEAF(index)]);
    }

    return ret_ptr;
}

void buddy_free(struct buddy_pool* self, char* pointer)
{
    assert(self != NULL && self->bh != NULL &&self->buffer != NULL && pointer != NULL);
    int offset = pointer - self->buffer;
    assert(offset >= 0 && offset < self->pool_size);

    int current_order = self->min_order;
    int current_index = 0;
    int found = 0;
    unsigned char left_order, right_order;

    while (offset % (1 << current_order) == 0) {
        current_index = (1 << (self->order - current_order)) - 1 + offset / (1 << current_order);
        if (self->bh[current_index] == 0) {
            found = 1;
            break;
        }
        current_order++;
    }
    assert(found == 1);

    self->bh[current_index] = current_order;
    while (current_index) {
        current_index = PARENT(current_index);
        current_order++;

        left_order = self->bh[LEFT_LEAF(current_index)];
        right_order = self->bh[RIGHT_LEAF(current_index)];
        if (left_order == (current_order - 1) && right_order == (current_order - 1)) {
            self->bh[current_index] = current_order;
        } else {
            self->bh[current_index] = MAX(left_order, right_order);
        }
    }
}

int buddy_size(struct buddy_pool* self, char* pointer) {
    assert(self != NULL && self->bh != NULL && self->buffer != NULL && pointer != NULL);
    int offset = pointer - self->buffer;
    assert(offset >= 0 && offset < self->pool_size);

    int current_order = self->min_order;
    int current_index = 0;
    int found = 0;

    while (offset % (1 << current_order) == 0) {
        current_index = (1 << (self->order - current_order)) - 1 + offset / (1 << current_order);
        if (self->bh[current_index] == 0) {
            found = 1;
            break;
        }
        current_order++;
    } 
    
    assert(found == 1);
    return (1 << current_order);
}