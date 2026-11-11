#include "search_input.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char s[128]="  10 Downing Street, London  ";
    assert(search_trim_query(s)==25);
    assert(strcmp(s,"10 Downing Street, London")==0);
    char empty[]="   \t\r\n ";assert(search_trim_query(empty)==0&&!empty[0]);
    char utf8[]=" \tM\xC3\xBCnchen, Germany \n";
    assert(search_trim_query(utf8)==strlen("M\xC3\xBCnchen, Germany"));
    assert(strcmp(utf8,"M\xC3\xBCnchen, Germany")==0);
    char exact[]="New York";assert(search_trim_query(exact)==8);
    assert(search_trim_query(NULL)==0);
    puts("Search input UTF-8 preservation and whitespace tests passed");return 0;
}
