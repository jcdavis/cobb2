#ifndef _DLINE_H_
#define _DLINE_H_

#include "cobb2.h"

#define DLINE_UPSERT_MODE_INITIAL 0
#define DLINE_UPSERT_MODE_INSERT  1
#define DLINE_UPSERT_MODE_UPDATE  2

enum dline_op_ret {
  NO_ERROR = 0,
  MALLOC_FAIL = 1,
  BAD_PARAM = 2
};

typedef short dline_result;
typedef void dline_t;

typedef struct dline_entry {
  global_data* global_ptr;
  unsigned int score;
  unsigned int len;
} dline_entry;

dline_result dline_upsert(dline_t* existing,
                          dline_t** result,
                          char* string,
                          unsigned int start,
                          unsigned int total_len,
                          global_data** global_ptr,
                          int score,
                          short* dline_upsert_mode,
                          int* old_score);

dline_result dline_remove(dline_t* existing,
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
                 int result_size);

void dline_debug(dline_t* dline);

#endif
