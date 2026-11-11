#include "gps_pairing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 unsigned char entropy[16]={0};char token[33],hdr[256];
 gps_pairing_token(entropy,token);assert(strlen(token)==32);
 snprintf(hdr,sizeof(hdr),"POST /location HTTP/1.1\r\naUtHoRiZaTiOn: Bearer %s\r\n\r\n",token);
 assert(gps_pairing_authorized(hdr,token));
 assert(!gps_pairing_authorized("POST /location HTTP/1.1\r\n\r\n",token));
 for(unsigned i=0;i<32;i++){char bad[33];strcpy(bad,token);bad[i]='1';assert(!gps_pairing_authorized(hdr,bad));}
 snprintf(hdr,sizeof(hdr),"POST /location HTTP/1.1\r\nAuthorization: Bearer %s\r\nAuthorization: Bearer %s\r\n\r\n",token,token);
 assert(!gps_pairing_authorized(hdr,token));
 assert(!gps_pairing_authorized("GET /Authorization: Bearer 00000000000000000000000000000000 HTTP/1.1\r\n\r\n",token));
 puts("GPS bearer pairing: missing, mismatched, duplicate, path injection passed");
}
