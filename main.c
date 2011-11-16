#include <stdio.h>
#include <string.h>
#include "dline.h"

static char* string = "Hello World!";
int main(int argc, char** argv) {
  printf("%s %lu\n", string, strlen(string));
  char* global_ptr = NULL;
  int old_score = 0;
  short mode = DLINE_UPSERT_MODE_INITIAL;
  dline* result = dline_upsert(NULL,
                               string,
                               2,
                               strlen(string),
                               &global_ptr,
                               9001,
                               &mode,
                               &old_score);
  dline* result2 = dline_upsert(result,
                                string,
                                6,
                                strlen(string),
                                &global_ptr,
                                9000,
                                &mode,
                                &old_score);
  dline_debug(result);
  printf("-------\n");
  dline_debug(result2);
  return 0;
}