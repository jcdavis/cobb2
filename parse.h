#ifndef _PARSE_H_
#define _PARSE_H_

#include "cobb2.h"

#define MAP_SIZE 32

op_result normalize(char* in, int len, char** out);

void bit_map_init(unsigned char* map, char* chars);

int next_start(char* normalized,
               int len,
               unsigned char* start_map,
               unsigned char* middle_map,
               int last_token);

#endif