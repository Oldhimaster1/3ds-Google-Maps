#include "qrcode.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Independent BCH(15,5) check of both format-info copies, all masks. */
static unsigned fmt_bits(int mask) {
    unsigned data=8u|(unsigned)mask, rem=data<<10;
    for(int i=4;i>=0;i--)if(rem&(1u<<(i+10)))rem^=0x537u<<i;
    return ((data<<10)|rem)^0x5412u;
}
static int fmt_ok(int sz,const uint8_t *q,int mask){
    unsigned bits=fmt_bits(mask);
    for(int i=0;i<15;i++){
        int a=i<6?i:(i<8?i+1:sz-15+i);
        int c=i<8?sz-1-i:(i==8?7:14-i);
        int bit=(bits>>i)&1;
        if(q[a*sz+8]!=bit||q[8*sz+c]!=bit)return 0;
    }
    return q[(sz-8)*sz+8]==1;
}
int main(void){
    static const char *urls[]={
        "https://192.168.1.118:8443",
        "https://10.0.0.5:8443",
        "https://172.20.10.2:8443",
        "https://192.168.100.100:8443",
        "https://192.168.1.118:8443/gps",
        "https://192.168.1.118:8443/gps?hello=world"
    };
    for(unsigned i=0;i<sizeof(urls)/sizeof(urls[0]);i++){
        uint8_t modules[QR_MAX_MODULES*QR_MAX_MODULES]={0};
        int size=0;
        int v=qr_encode(urls[i],(int)strlen(urls[i]),modules,&size);
        assert(v>=1&&v<=4&&size==17+4*v);
        int found=0;
        for(int mask=0;mask<8;mask++)if(fmt_ok(size,modules,mask))found++;
        assert(found==1);
    }
    uint8_t q[QR_MAX_MODULES*QR_MAX_MODULES];int n=0;
    assert(qr_encode(NULL,3,q,&n)==0);
    assert(qr_encode("hello",-1,q,&n)==0);
    assert(qr_encode("hello",5,NULL,&n)==0);
    assert(qr_encode("hello",5,q,NULL)==0);
    puts("GPS QR format-information and input validation tests passed");
    return 0;
}
