#include <stdlib.h>
#include "cmalloc.h"

/* Thin malloc wrappers */
void* ccalloc(size_t count, size_t size) {
  return calloc(count, size);
}

void cfree(void *ptr) {
  free(ptr);
}

void* cmalloc(size_t size) {
  return malloc(size);
}
