#ifndef SEARCH_INPUT_H
#define SEARCH_INPUT_H
#include <stddef.h>
#include <string.h>
/* Trim ASCII whitespace without damaging UTF-8 characters inside queries. */
static inline size_t search_trim_query(char *query) {
    if (!query) return 0;
    char *first=query;
    while (*first==' ' || *first=='\t' || *first=='\n' || *first=='\r') first++;
    if (first!=query) memmove(query,first,strlen(first)+1);
    size_t length=strlen(query);
    while (length && (query[length-1]==' ' || query[length-1]=='\t' ||
                      query[length-1]=='\n' || query[length-1]=='\r'))
        query[--length]=0;
    return length;
}
#endif
