extern "C" {
    #include "myqueue.h"
    #include "libcircle.h"
    #include "log.h"
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <deque>
#include <fstream>
#include <string>

CIRCLE_internal_queue_t* CIRCLE_internal_queue_init(void)
{
    // allocate queue and deque
    CIRCLE_internal_queue_t* qp =
        (CIRCLE_internal_queue_t*)malloc(sizeof(CIRCLE_internal_queue_t));

    qp->handle = new std::deque<std::string>();
    qp->count = 0;
    qp->base = NULL;
    qp->bytes = 0;

    return qp;
}

int8_t CIRCLE_internal_queue_free(CIRCLE_internal_queue_t* qp)
{
    if(qp != NULL) {
        if(qp->handle != NULL) {
            std::deque<std::string>* qhandle =
                static_cast< std::deque<std::string>* >(qp->handle);
            delete qhandle;
        }
        qp->count = 0;
        if(qp->base) {
            free(qp->base);
        }
        qp->bytes = 0;
        free(qp);
    } else {
        LOG(CIRCLE_LOG_ERR, "Attempted to free a null queue structure.");
        return -1;
    }
    return 1;
}

int8_t CIRCLE_internal_queue_push(CIRCLE_internal_queue_t* qp, char* str)
{
    if(!str) {
        LOG(CIRCLE_LOG_ERR, "Attempted to push null pointer.");
        return -1;
    }

    std::deque<std::string>& queue =
        *(static_cast< std::deque<std::string>* >(qp->handle));

    queue.push_front(std::string(str));
    qp->count = static_cast<int32_t>(queue.size());

    return 1;
}

int8_t CIRCLE_internal_queue_pop(CIRCLE_internal_queue_t* qp, char* str)
{
    if(!qp) {
        LOG(CIRCLE_LOG_ERR, "Attempted to pop from an invalid queue.");
        return -1;
    }

    if(qp->count < 1) {
        LOG(CIRCLE_LOG_DBG, "Attempted to pop from an empty queue.");
        return -1;
    }

    if(!str) {
        LOG(CIRCLE_LOG_ERR,
            "You must allocate a buffer for storing the result.");
        return -1;
    }

    std::deque<std::string>& queue =
        *(static_cast< std::deque<std::string>* >(qp->handle));
    std::string& last_str = queue.back();

    size_t len = last_str.copy(str, last_str.size());
    str[len] = '\0';

    queue.pop_back();
    qp->count = static_cast<int32_t>(queue.size());

    return 1;
}

int8_t CIRCLE_internal_queue_peek_size(CIRCLE_internal_queue_t* qp, size_t* str_size)
{
    if(!qp) {
        LOG(CIRCLE_LOG_ERR, "Attempted to peek_size at an invalid queue.");
        return -1;
    }

    if(qp->count < 1) {
        LOG(CIRCLE_LOG_DBG, "Attempted to peek_size at an empty queue.");
        return -1;
    }

    if(!str_size) {
        LOG(CIRCLE_LOG_ERR,
            "You must provide a size_t pointer for returning the result.");
        return -1;
    }

    std::deque<std::string>& queue =
        *(static_cast< std::deque<std::string>* >(qp->handle));
    std::string& last_str = queue.back();

    *str_size = last_str.size();

    return 1;
}

int8_t CIRCLE_internal_queue_read(CIRCLE_internal_queue_t* qp, int rank)
{
    if(!qp) {
        LOG(CIRCLE_LOG_ERR, "Libcircle queue not initialized.");
        return -1;
    }

    LOG(CIRCLE_LOG_DBG, "Reading from checkpoint file %d.", rank);

    if(qp->count != 0) {
        LOG(CIRCLE_LOG_WARN, \
            "Reading items from checkpoint file into non-empty work queue.");
    }

    char filename[256];
    sprintf(filename, "circle%d.txt", rank);

    LOG(CIRCLE_LOG_DBG, "Attempting to open %s.", filename);

    std::ifstream checkpoint_file;

    checkpoint_file.open(filename);

    if(checkpoint_file.is_open()) {
        LOG(CIRCLE_LOG_ERR, "Unable to open checkpoint file %s", filename);
        return -1;
    }

    LOG(CIRCLE_LOG_DBG, "Checkpoint file opened.");

    std::string str;

    while(!checkpoint_file.eof()) {
        std::getline(checkpoint_file, str);

        if(checkpoint_file.fail()) {
            LOG(CIRCLE_LOG_ERR, "Failed to read element from %s", filename);
        }

        if(CIRCLE_internal_queue_push(qp,
                    const_cast<char*>(str.c_str())) < 0) {
            LOG(CIRCLE_LOG_ERR, "Failed to push element on queue \"%s\"",
                    str.c_str());
        }

        LOG(CIRCLE_LOG_DBG, "Pushed %s onto queue.", str.c_str());
    }

    checkpoint_file.close();
    return 0;
}

int8_t CIRCLE_internal_queue_write(CIRCLE_internal_queue_t* qp, int rank)
{
    LOG(CIRCLE_LOG_INFO, \
        "Writing checkpoint file with %d elements.", qp->count);

    if(qp->count == 0) {
        return 0;
    }

    char filename[256];
    sprintf(filename, "circle%d.txt", rank);
    FILE* checkpoint_file = fopen(filename, "w");

    if(checkpoint_file == NULL) {
        LOG(CIRCLE_LOG_ERR, "Unable to open checkpoint file %s", filename);
        return -1;
    }

    std::deque<std::string>& queue =
        *(static_cast< std::deque<std::string>* >(qp->handle));
    for(std::deque<std::string>::iterator itr = queue.begin();
            itr != queue.end();
            ++itr) {
        std::string& str = *itr;
        if(fprintf(checkpoint_file, "%s\n", str.c_str()) < 0) {
            LOG(CIRCLE_LOG_ERR, "Failed to write \"%s\" to file.", str.c_str());
            return -1;
        }
    }

    queue.clear();
    qp->count = static_cast<int32_t>(queue.size());

    int fclose_rc = fclose(checkpoint_file);
    return (int8_t) fclose_rc;
}

int8_t CIRCLE_internal_queue_extend(
        CIRCLE_internal_queue_t* qp, size_t new_size)
{
    size_t current = qp->bytes;

    /* TODO: check for overflow */
    while(current < new_size) {
        current += ((size_t)sysconf(_SC_PAGESIZE)) * 4096;
    }

    if(qp->base) {
        free(qp->base);
    }
    // free-ing assumes that CIRCLE_internal_queue_splice_in will be called
    // immediately after this call and thus we won't need the data in qp->base
    // anymore.
    qp->base = static_cast<char*>(malloc(current));

    if(!qp->base) {
        LOG(CIRCLE_LOG_ERR, "Failed to allocate a basic queue structure.");
        return -1;
    }

    qp->bytes = current;
    return 0;
}

static int8_t CIRCLE_internal_queue_extend_realloc(
        CIRCLE_internal_queue_t* qp, size_t new_size)
{
    size_t current = qp->bytes;

    /* TODO: check for overflow */
    while(current < new_size) {
        current += ((size_t)sysconf(_SC_PAGESIZE)) * 4096;
    }

    qp->base = static_cast<char*>(realloc(qp->base, current));

    if(!qp->base) {
        LOG(CIRCLE_LOG_ERR, "Failed to allocate a basic queue structure.");
        return -1;
    }

    qp->bytes = current;
    return 0;
}

size_t CIRCLE_internal_queue_pop_multi(
        CIRCLE_internal_queue_t* qp, int32_t count, int32_t* offsets) {

    std::deque<std::string>& queue =
        *(static_cast< std::deque<std::string>* >(qp->handle));

    int32_t cur_offset = 0;
    for(int32_t i = 0; i < count && queue.size() > 0; ++i) {
        std::string& last_str = queue.back();

        if(qp->bytes < (cur_offset + last_str.size())) {
            if(CIRCLE_internal_queue_extend_realloc(
                        qp,
                        (cur_offset
                         + last_str.size())) != 0) {
                return 0;
            }
        }
        last_str.copy(&(qp->base[cur_offset]), last_str.size());
        cur_offset += static_cast<int32_t>(last_str.size());
        // store the offset to the end of this string in the offsets buffer
        offsets[i] = cur_offset;

        queue.pop_back();
    }

    // shrink the queue internals to fit
    std::deque<std::string>(queue).swap(queue);

    return static_cast<size_t>(cur_offset);
}

int8_t CIRCLE_internal_queue_push_multi(
        CIRCLE_internal_queue_t* qp, int32_t count, int32_t* offsets) {

    int32_t start_offset = 0;
    for(int32_t i = 0; i < count; ++i) {
        char* cur_item = &(qp->base[start_offset]);

        int32_t end_offset = offsets[i];

        std::string cur_str(cur_item,
                static_cast<size_t>(end_offset - start_offset));

        if(CIRCLE_internal_queue_push(qp,
                    const_cast<char*>(cur_str.c_str())) != 1) {
            return -1;
        }

        start_offset = end_offset;
    }

    return 1;
}
