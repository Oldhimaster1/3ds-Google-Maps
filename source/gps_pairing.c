#include "gps_pairing.h"
#include <string.h>
#include <ctype.h>
void gps_pairing_token(const unsigned char entropy[16], char out[33]) {
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++){out[i*2]=hex[entropy[i]>>4];out[i*2+1]=hex[entropy[i]&15];}
    out[32]=0;
}
bool gps_pairing_authorized(const char *headers, const char token[33]) {
    if(!headers || !token || strlen(token)!=32)return false;
    const char *p=strstr(headers,"\r\n");
    unsigned matches=0;
    bool valid=false;
    while(p && p[2] && p[2]!='\r'){
        p+=2;
        const char *end=strstr(p,"\r\n");
        if(!end)return false;
        const char name[]="authorization:";
        unsigned i=0;
        while(i<sizeof(name)-1 && p+i<end &&
              tolower((unsigned char)p[i])==name[i])i++;
        if(i==sizeof(name)-1){
            matches++;
            const char *v=p+i;
            while(v<end && (*v==' '||*v=='\t'))v++;
            if(end-v==39 && !strncmp(v,"Bearer ",7)){
                unsigned diff=0;
                for(unsigned j=0;j<32;j++)diff|=(unsigned char)v[7+j]^(unsigned char)token[j];
                valid=diff==0;
            }
        }
        p=end;
    }
    return matches==1 && valid;
}
