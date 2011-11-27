#ifndef _DLINE_H_
#define _DLINE_H_

#include "cobb2.h"

typedef void dline_t;

typedef struct dline_entry {
  global_data* global_ptr;
  unsigned int score;
  unsigned int len;
} dline_entry;

op_result dline_upsert(dline_t* existing,
                       dline_t** result,
                       char* string,
                       unsigned int start,
                       unsigned int total_len,
                       int score,
                       upsert_state* state);

op_result dline_remove(dline_t* existing,
                       dline_t** result,
                       char* string,
                       unsigned int start,
                       unsigned int total_len);

int dline_search(dline_t* dline,
                 char* string,
                 unsigned int start,
                 unsigned int total_len,
                 int min_score,
                 dline_entry* results,
                 int result_len);

void dline_debug(dline_t* dline);

#endif
