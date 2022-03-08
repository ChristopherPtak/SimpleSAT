
#include "utils.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"

void *xmalloc(size_t size)
{
    void *ptr = malloc(size);

    if (ptr == NULL) {
        fprintf(stderr, PROGRAM_NAME ": %s\n", strerror(errno));
        abort();
    }

    return ptr;
}

void *xrealloc(void *ptr, size_t new_size)
{
    void *new_ptr = realloc(ptr, new_size);

    if (new_ptr == NULL) {
        fprintf(stderr, PROGRAM_NAME ": %s\n", strerror(errno));
        abort();
    }

    return new_ptr;
}

