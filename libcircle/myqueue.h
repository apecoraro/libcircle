#ifndef INTERNAL_MY_QUEUE_H
#define INTERNAL_MY_QUEUE_H

#include <stdint.h>
#include <stdlib.h>

typedef struct CIRCLE_internal_queue_t {
    void* handle;
    int32_t count;      /* The number of actively queued strings */
    char* base; /* buffer to receive new items on */
    size_t bytes; /* size of base */
} CIRCLE_internal_queue_t;

CIRCLE_internal_queue_t* CIRCLE_internal_queue_init(void);
int8_t CIRCLE_internal_queue_free(CIRCLE_internal_queue_t* qp);

int8_t CIRCLE_internal_queue_push(CIRCLE_internal_queue_t* qp, char* str);
int8_t CIRCLE_internal_queue_pop(CIRCLE_internal_queue_t* qp, char* str);

int8_t CIRCLE_internal_queue_write(CIRCLE_internal_queue_t* qp, int rank);
int8_t CIRCLE_internal_queue_read(CIRCLE_internal_queue_t* qp, int rank);
int8_t CIRCLE_internal_queue_extend(CIRCLE_internal_queue_t* qp, size_t size);

int8_t CIRCLE_internal_queue_push_multi(
		CIRCLE_internal_queue_t* qp, int32_t count, int* offsets);
size_t CIRCLE_internal_queue_pop_multi(
        CIRCLE_internal_queue_t* qp, int32_t count, int* offsets);

#endif /* INTERNAL_MY_QUEUE_H */
