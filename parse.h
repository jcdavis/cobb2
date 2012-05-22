#ifndef _PARSE_H_
#define _PARSE_H_

#include "cobb2.h"

#define MAP_SIZE 32

typedef struct parser_data {
  unsigned char start_map[MAP_SIZE];
  unsigned char middle_map[MAP_SIZE];
} parser_data;

op_result normalize(char* in, int len, char** out);

void parser_data_init(parser_data* data,
                      char* start,
                      char* middle);

int next_start(char* normalized,
               int len,
               parser_data* data,
               int last_token);

#endif