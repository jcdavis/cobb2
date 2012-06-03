#include <ctype.h>
#include <string.h>
#include "cmalloc.h"
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
op_result normalize(char* in, string_data* data) {
  if(in == NULL || data == NULL)
        return BAD_PARAM;

  int len = strlen(in);

  data->full = in;
  data->length = len;

  /* We will add a null terminator, but only to make debugging easier. Nothing
   * otherwise actually requires it, all operations are length-based.
   */
  data->normalized = cmalloc(len+1);
  if(data->normalized == NULL)
    return MALLOC_FAIL;
  for(int i = 0; i < len; i++) {
    data->normalized[i] = (char)tolower(in[i]);
  }
  data->normalized[len] = '\0';
  return NO_ERROR;
}

/* Pre-set up bit maps for a null-terminated string of characters, as to avoid
 * recalculating every time next_start is called
 */
static void bit_map_init(unsigned char* map,
                         char* chars) {
  char* current = chars;
  memset(map, 0, MAP_SIZE);
  while(*current != '\0') {
    map[*current>>3] |= (char)(1 << (*current & 7));
    current++;
  }
}

/* Sets up bitmaps for start/middle characters*/
void parser_data_init(parser_data* data,
                      char* start,
                      char* middle) {
  bit_map_init(data->start_map, start);
  bit_map_init(data->middle_map, middle);
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
int next_start(string_data* string,
               parser_data* parser,
               int last_token) {
  if(string == NULL || parser == NULL)
    return -2;
  int token_start = last_token < 0 ? 0 : last_token + 1;
  /* If this is the first call we start in "middle mode", where any non-middle
   * character starts a suffix
   */
  int prev_middle = token_start == 0 ? 1 : 0;

  for(int c = token_start; c < string->length; c++) {
    int is_middle = in_map(parser->middle_map, string->normalized[c]);
    if((prev_middle && !is_middle) ||
        in_map(parser->start_map, string->normalized[c])) {
      return c;
    }
    prev_middle = is_middle;
  }
  /* Reached the end of the string, no more suffixes*/
  return -1;
}
