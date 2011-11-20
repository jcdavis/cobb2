#ifndef _COBB2_H_
#define _COBB2_H_

typedef struct global_data {
  void* unused;
  int len;
} global_data;

#define GLOBAL_STR(g) ((char*)g + sizeof(global_data))

#endif
