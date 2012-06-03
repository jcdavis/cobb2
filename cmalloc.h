#ifndef _CMALLOC_H_
#define _CMALLOC_H_

#include <stddef.h>

void* ccalloc(size_t count, size_t size);
void cfree(void *ptr);
void* cmalloc(size_t size);
void cmalloc_stats();

#endif

