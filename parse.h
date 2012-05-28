#ifndef _PARSE_H_
#define _PARSE_H_

#include "cobb2.h"

#define MAP_SIZE 32

typedef struct parser_data {
  unsigned char start_map[MAP_SIZE];
  unsigned char middle_map[MAP_SIZE];
} parser_data;

/* For now, the full string must have the same byte length when normalized*/
typedef struct string_data {
  char* full; /*full string to upsert*/
  char* normalized; /*normalized string to be indexed*/
  unsigned int length; /*NOT including a trailing \0*/
} string_data;

op_result normalize(char* in, string_data* data);

void parser_data_init(parser_data* data,
                      char* start,
                      char* middle);

int next_start(string_data* string,
               parser_data* parser,
               int last_token);

#endif