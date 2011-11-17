#ifndef _DLINE_H_
#define _DLINE_H_

#define DLINE_UPSERT_MODE_INITIAL 0
#define DLINE_UPSERT_MODE_INSERT  1
#define DLINE_UPSERT_MODE_UPDATE  2

typedef void dline;

typedef struct dline_entry {
  void* global_ptr;
  unsigned int score;
  unsigned int len;
} dline_entry;

dline* dline_upsert(dline* existing,
                    char* string,
                    unsigned int start,
                    unsigned int total_len,
                    char** global_ptr,
                    int score,
                    short* dline_upsert_mode,
                    int* old_score);

int dline_search(dline* dline,
                 char* string,
                 unsigned int start,
                 unsigned int total_len,
                 int min_score,
                 dline_entry* results,
                 int result_size);
                    

void dline_debug(dline* dline);

#endif
