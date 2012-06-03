#include <jemalloc/jemalloc.h>
#include "cmalloc.h"

/* Thin malloc wrappers */
void* ccalloc(size_t count, size_t size) {
  return je_calloc(count, size);
}

void cfree(void *ptr) {
  je_free(ptr);
}

void* cmalloc(size_t size) {
  return je_malloc(size);
}

void cmalloc_stats() {
  je_malloc_stats_print(NULL, NULL, NULL);
}
