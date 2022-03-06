
#ifndef SIMPLESAT_UTILS_H
#define SIMPLESAT_UTILS_H

#include <stddef.h>
#include <stdlib.h>

void *xmalloc(size_t);
void *xrealloc(void *, size_t);

#define CREATE_ARRAY(A, S) A = xmalloc((S) * sizeof(*A))
#define RESIZE_ARRAY(A, N) A = xrealloc(A, (N) * sizeof(*A))
#define DELETE_ARRAY(A)    free(A);

#endif

