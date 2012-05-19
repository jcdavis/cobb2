#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "cobb2.h"
#include "parse.h"

/* Functions to process input strings for update/search on a trie.
 * Currently ASCII only, with Unicode to be added later (likely via ICU)
 */

/* Returns a normalized copy of a given string. Currently, this just converts
 * the entire string to lowercase. len does not include a null terminator.
 * TODO: leading/trailing whitespace removal, middle whitespace normalization/
 * reduction?. Also, Unicode :(
 */
op_result normalize(char* in, int len, char** out) {
  if(in == NULL || out == NULL)
        return BAD_PARAM;
  /* We will add a null terminator, but only to make debugging easier. Nothing
   * otherwise actually requires it, all operations are length-based.
   */
  *out = malloc(len+1);
  if(*out == NULL)
    return MALLOC_FAIL;
  for(int i = 0; i < len; i++) {
    (*out)[i] = (char)tolower(in[i]);
  }
  (*out)[len] = '\0';
  return NO_ERROR;
}

/* Pre-set up bit maps for a null-terminated string of characters, as to avoid
 * recalculating every time next_start is called
 */
void bit_map_init(unsigned char* map,
                  char* chars) {
  char* current = chars;
  memset(map, 0, MAP_SIZE);
  while(*current != '\0') {
    map[*current>>3] |= (char)(1 << (*current & 7));
    current++;
  }
}

/* Helper to do the bitwise check if a given map has a given character */
static inline int in_map(unsigned char* map, char c) {
  return map[((unsigned char)c) >> 3] & (1 << (c & 7));
}
/* Iterates though the string to find the next start of a suffix in a string.
 * Its like strtok/strsep, except hopefully less shitty. (no modifying the input)
 * start_map is a bit map of characters for which seeing indicates the start of a
 * new suffix.
 * middle_map is a bit map of characters for which seeing indicates that the next
 * characters to be seen not in middle_map is the start of a suffix
 * last_token is the index of the last found start, it should be <0 on first call
 * If there are no more suffixes left in the string, next_start returns -1
 * 0 Is always the start of a new suffix, unless it starts with a middle
 * character, in which case the first suffix is the first non middle character
 */
int next_start(char* normalized,
               int len,
               unsigned char* start_map,
               unsigned char* middle_map,
               int last_token) {
  if(normalized == NULL || start_map == NULL || middle_map == NULL)
    return -2;
  int token_start = last_token < 0 ? 0 : last_token + 1;
  /* If this is the first call we start in "middle mode", where any non-middle
   * character starts a suffix
   */
  int prev_middle = token_start == 0 ? 1 : 0;

  for(int c = token_start; c < len; c++) {
    int is_middle = in_map(middle_map, normalized[c]);
    if((prev_middle && !is_middle) || in_map(start_map, normalized[c])) {
      return c;
    }
    prev_middle = is_middle;
  }
  /* Reached the end of the string, no more suffixes*/
  return -1;
}
