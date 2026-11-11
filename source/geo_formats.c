#include "geo_formats.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char alphabet[]="23456789CFGHJMPQRVWX";
static const char *space(const char *p){while(*p&&isspace((unsigned char)*p))p++;return p;}
static bool plus_code(const char *input,double *lat,double *lon){
    char s[24];unsigned n=0;
    for(const char *p=input;*p;p++){
        if(isspace((unsigned char)*p))continue;
        if(n>=sizeof(s)-1)return false;
        s[n++]=(char)toupper((unsigned char)*p);
    }
    s[n]=0;
    /* Accept precise, full, non-padded 10-digit codes. Short codes require
       a geographic reference, and must not be silently misinterpreted. */
    if(n!=11||s[8]!='+')return false;
    double a=-90,b=-180,resolution=20;
    for(unsigned i=0;i<10;i+=2){
        unsigned c=i<8?i:i+1,d=i<8?i+1:i+2;
        const char *x=strchr(alphabet,s[c]),*y=strchr(alphabet,s[d]);
        if(!x||!y)return false;
        a+=(x-alphabet)*resolution;b+=(y-alphabet)*resolution;
        resolution/=20.0;
    }
    a+=resolution*10; b+=resolution*10;
    if(a< -90||a>90||b< -180||b>180)return false;
    *lat=a;*lon=b;return true;
}
static bool marker(const char *p){return *p==':'||*p=='\''||*p=='"'||*p=='\xC2'||*p=='\xB0'||*p=='d'||*p=='D';}
static void skip_symbol(const char **q){const unsigned char *p=(const unsigned char *)*q;
    if(p[0]==0xC2&&p[1]==0xB0)p+=2;
    else if(*p)p++;
    *q=(const char*)p;
}
static bool coord(const char **ptr,double *out,int axis){
    const char *p=space(*ptr);
    char *end=NULL;double degrees=strtod(p,&end);
    if(end==p||!isfinite(degrees))return false;
    p=end;int sign=degrees<0?-1:1;
    double value=fabs(degrees), minutes=0,seconds=0;
    const char *scan=space(p);
    if(marker(scan)){
        /* A degree symbol or a colon authorizes optional D/M/S segments. */
        bool deg=(scan[0]=='\xC2' || scan[0]=='\xB0'|| scan[0]=='d'||scan[0]=='D');
        if(deg||*scan==':'){
            skip_symbol(&scan);scan=space(scan);
            if(isdigit((unsigned char)*scan)){
                minutes=strtod(scan,&end);if(!isfinite(minutes)||minutes<0||minutes>=60)return false;
                scan=space(end);
                if(*scan=='\''||*scan==':'||*scan=='\xE2'){
                    if(*scan=='\xE2') {if((unsigned char)scan[1]!=0x80)return false;scan+=3;}else skip_symbol(&scan);
                    scan=space(scan);
                    if(isdigit((unsigned char)*scan)){
                        seconds=strtod(scan,&end);if(!isfinite(seconds)||seconds<0||seconds>=60)return false;
                        scan=space(end);
                        if(*scan=='"')scan++;
                    }
                }
            }
        }
    }
    if(scan>p)p=scan;
    p=space(p);
    char hemi=(char)toupper((unsigned char)*p);
    if(hemi=='N'||hemi=='S'||hemi=='E'||hemi=='W'){
        if((axis==0 && hemi!='N'&&hemi!='S') ||
           (axis==1 && hemi!='E'&&hemi!='W'))return false;
        if(degrees<0 && (hemi=='N'||hemi=='E'))return false;
        sign=(hemi=='S'||hemi=='W')?-1:1;
        p++;
    }
    value+=minutes/60+seconds/3600;
    if(value>(axis==0?90.0:180.0))return false;
    *out=sign*value;*ptr=p;return true;
}
GeoFormat geo_parse_coordinates(const char *text,double *lat,double *lon){
    if(!text||!lat||!lon)return GEO_INVALID;
    if(strchr(text,'+')){
        if(plus_code(text,lat,lon))return GEO_PLUS_CODE;
    }
    const char *p=text;double a,b;
    if(!coord(&p,&a,0))return GEO_INVALID;
    p=space(p);if(*p==',')p++;
    if(!coord(&p,&b,1))return GEO_INVALID;
    if(*space(p))return GEO_INVALID;
    *lat=a;*lon=b;
    return strpbrk(text,"'\":")||strstr(text,"\xC2\xB0")?GEO_DMS:GEO_DECIMAL;
}
