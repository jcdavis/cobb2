#ifndef _COBB2_H_
#define _COBB2_H_

typedef struct global_data {
  void* unused;
  int len;
} global_data;

#define GLOBAL_STR(g) ((char*)g + sizeof(global_data))

enum op_ret {
  NO_ERROR = 0,
  MALLOC_FAIL = 1,
  BAD_PARAM = 2,
  NOT_FOUND = 3
};

typedef unsigned short op_result;

enum upsert_mode {
  UPSERT_MODE_INITIAL = 0,
  UPSERT_MODE_INSERT = 1,
  UPSERT_MODE_UPDATE = 2
};

typedef struct upsert_state {
  global_data* global_ptr;
  int old_score;
  unsigned short mode;
} upsert_state;

#endif
