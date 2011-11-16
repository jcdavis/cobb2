#ifndef _DLINE_H_
#define _DLINE_H_

typedef void dline;

#define DLINE_UPSERT_MODE_INITIAL 0
#define DLINE_UPSERT_MODE_INSERT  1
#define DLINE_UPSERT_MODE_UPDATE  2

dline* dline_upsert(dline* existing,
                    char* string,
                    unsigned int start,
                    unsigned int total_len,
                    char** global_ptr,
                    int score,
                    short* dline_upsert_mode,
                    int* old_score);

void dline_debug(dline* dline);

#endif
