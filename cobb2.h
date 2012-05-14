#ifndef _COBB2_H_
#define _COBB2_H_

/* String contents are stored immediately after the end of this struct*/
typedef struct global_data {
  int len;
} global_data;

#define GLOBAL_STR(g) ((char*)g + sizeof(global_data))

/* For now, the full string must have the same byte length when normalized*/
typedef struct string_data {
  char* full; /*full string to upsert*/
  char* normalized; /*normalized string to be indexed*/
  unsigned int length; /*NOT including a trailing \0*/
} string_data;

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

typedef struct remove_state {
  global_data* global_ptr;
} remove_state;

/* Results need an additional offset to calculate start position*/
typedef struct result_entry {
  global_data* global_ptr;
  unsigned int score;
  unsigned int len;
  unsigned int offset;
} result_entry;

#endif
