#include "intl.h"
#include <fribidi/fribidi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
static unsigned language;
static bool music;
static unsigned music_volume=55;
static const char *names[INTL_LANGUAGE_COUNT]={"English","العربية","Français","Español","Deutsch","Português","Italiano","Русский","Türkçe"};
typedef struct {const char *text[INTL_LANGUAGE_COUNT];} Message;
static const Message messages[]={
#include "intl_messages.inc"
};
const char *intl_language_name(unsigned lang){return names[lang<INTL_LANGUAGE_COUNT?lang:0];}
unsigned intl_language(void){return language;}
void intl_set_language(unsigned lang){language=lang<INTL_LANGUAGE_COUNT?lang:0;}
unsigned intl_music_volume(void){return music_volume;}
void intl_set_music_volume(unsigned percent){music_volume=percent>100?100:percent;}
bool intl_music(void){return music;}
void intl_set_music(bool enabled){music=enabled;}
const char *intl_translate(const char *english){
    if(!english || !language)return english;
    const char *key=english;
    if(!strcmp(key,"Classic random"))key="Classic";
    if(!strcmp(key,"Moving"))key="Free move";
    for(size_t i=0;i<sizeof(messages)/sizeof(messages[0]);i++)
        if(!strcasecmp(key,messages[i].text[0]))return messages[i].text[language];
    return english;
}
bool intl_visual(const char *logical,char *visual,size_t capacity){
    if(!visual || !capacity)return false;
    visual[0]=0;if(!logical)return false;
    /* Bound stack usage and reject partial/oversized input rather than
     * silently corrupting UTF-8. Shape each paragraph independently. */
    size_t bytes=strlen(logical);
    if(bytes>=1024)return false;
    static FriBidiChar chars[1024];
    FriBidiStrIndex count=fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8,logical,(int)bytes,chars);
    if(count<0 || count>1023)return false;
    FriBidiStrIndex begin=0;
    for(FriBidiStrIndex end=0;end<=count;end++){
        if(end<count && chars[end]!='\n')continue;
        FriBidiStrIndex n=end-begin;
        if(n){
            static FriBidiCharType types[1024];static FriBidiBracketType brackets[1024];
            static FriBidiLevel levels[1024];static FriBidiArabicProp props[1024];
            FriBidiParType direction=FRIBIDI_PAR_ON;
            fribidi_get_bidi_types(chars+begin,n,types);
            fribidi_get_bracket_types(chars+begin,n,types,brackets);
            if(!fribidi_get_par_embedding_levels_ex(types,brackets,n,&direction,levels))return false;
            fribidi_get_joining_types(chars+begin,n,props);
            fribidi_join_arabic(types,n,levels,props);
            fribidi_shape(FRIBIDI_FLAGS_DEFAULT|FRIBIDI_FLAGS_ARABIC,levels,n,props,chars+begin);
            if(!fribidi_reorder_line(FRIBIDI_FLAGS_DEFAULT,types,n,0,direction,levels,chars+begin,NULL))return false;
        }
        begin=end+1;
    }
    count=fribidi_remove_bidi_marks(chars,count,NULL,NULL,NULL);
    /* UTF-8 needs at most four bytes per codepoint and one terminator. */
    static char result[4097];
    int written=fribidi_unicode_to_charset(FRIBIDI_CHAR_SET_UTF8,chars,count,result);
    if(written<0 || (size_t)written>=capacity)return false;
    memcpy(visual,result,(size_t)written);visual[written]=0;return true;
}
bool intl_load(const char *path){
    FILE *f=path?fopen(path,"rb"):NULL;if(!f)return false;
    unsigned version=0,lang=0,enabled=0,volume=55;
    int n=fscanf(f,"%u %u %u %u",&version,&lang,&enabled,&volume);fclose(f);
    if((version!=1 && version!=2) || n<(version==2?4:3) ||
       lang>=INTL_LANGUAGE_COUNT || enabled>1 || (version==2 && volume>100))return false;
    language=lang;music=enabled!=0;music_volume=version==1?55:volume;return true;
}
bool intl_save(const char *path){
    if(!path)return false;
    char tmp[320];int n=snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    if(n<0 || n>=(int)sizeof(tmp))return false;
    FILE *f=fopen(tmp,"wb");if(!f)return false;
    bool ok=fprintf(f,"2 %u %u %u\n",language,music?1u:0u,music_volume)>0;
    if(fclose(f))ok=false;
    if(!ok || rename(tmp,path)){remove(tmp);return false;}return true;
}

int intl_snprintf(char *out,size_t capacity,const char *format,...){
    static char english[INTL_TEXT_CAP],translated[INTL_TEXT_CAP];
    va_list args,copy;va_start(args,format);va_copy(copy,args);
    int result=vsnprintf(english,sizeof(english),format,args);va_end(args);
    if(result<0){va_end(copy);if(out&&capacity)out[0]=0;return result;}
    if((size_t)result>=sizeof(english))result=vsnprintf(out,capacity,format,copy);
    else {
        const char *value=intl_display(english,translated,sizeof(translated))?translated:english;
        const char *translated_format=intl_translate(format);
        if(!strcmp(value,english) && translated_format!=format)
            result=vsnprintf(out,capacity,translated_format,copy);
        else result=snprintf(out,capacity,"%s",value);
    }
    va_end(copy);
    if(out && capacity && result>=(int)capacity){
        size_t end=capacity-1,start=end;
        while(start && ((unsigned char)out[start-1]&0xc0)==0x80)start--;
        if(start)start--;
        unsigned char lead=(unsigned char)out[start];
        size_t bytes=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;
        if(start+bytes>end)out[start]=0;
    }
    return result;
}

/* Runtime workers retain English status templates. Translate their completed
 * messages on the UI thread so changing language also updates live statuses. */
static const char *format_end(const char *p,char *type){
    if(*p!='%')return NULL;
    p++;
    if(*p=='%'){*type='%';return p+1;}
    while(*p && strchr("-+ #0.123456789*hljztL",*p))p++;
    if(!*p || !strchr("diuoxXfFeEgGaAcsp",*p))return NULL;
    *type=*p;return p+1;
}
static bool match_message(const char *format,const char *input,char captures[12][1024],unsigned *count){
    *count=0;
    while(*format){
        if(*format!='%'){if(*format++!=*input++)return false;continue;}
        char type;const char *end=format_end(format,&type);if(!end)return false;
        if(type=='%'){if(*input++!='%')return false;format=end;continue;}
        if(*count>=12)return false;
        const char *stop=input;
        if(type=='s'){
            char literal[128];unsigned n=0;const char *p=end;
            while(*p && *p!='%' && n+1<sizeof(literal))literal[n++]=*p++;
            literal[n]=0;
            if(!n && *end)return false; /* Adjacent string fields are ambiguous. */
            stop=n?strstr(input,literal):input+strlen(input);
            if(!stop)return false;
        }else if(type=='c'){if(!*input)return false;stop=input+1;}
        else {
            char *number_end;
            if(strchr("fFeEgGaA",type))strtod(input,&number_end);
            else strtoll(input,&number_end,strchr("xX",type)?16:type=='o'?8:10);
            if(number_end==input)return false;
            stop=number_end;
        }
        size_t n=(size_t)(stop-input);if(n>=1024)return false;
        memcpy(captures[*count],input,n);captures[*count][n]=0;(*count)++;
        input=stop;format=end;
    }
    return *input==0;
}
bool intl_display(const char *english,char *out,size_t capacity){
    if(!english||!out||!capacity)return false;
    const char *translated=intl_translate(english);
    if(translated!=english || !language){
        int n=snprintf(out,capacity,"%s",translated);return n>=0&&(size_t)n<capacity;
    }
    static char captures[12][1024];
    for(size_t m=0;m<sizeof(messages)/sizeof(messages[0]);m++){
        const char *key=messages[m].text[0];
        if(!strchr(key,'%') || !strcmp(key,"%s") || !strcmp(key,"%s%s"))continue;
        unsigned count;
        if(!match_message(key,english,captures,&count))continue;
        const char *p=messages[m].text[language];size_t used=0;unsigned index=0;
        while(*p){
            if(*p!='%'){if(used+1>=capacity)return false;out[used++]=*p++;continue;}
            char type;const char *end=format_end(p,&type);if(!end)return false;
            if(type=='%'){if(used+1>=capacity)return false;out[used++]='%';}
            else {
                if(index>=count)return false;
                const char *value=type=='s'?intl_translate(captures[index]):captures[index];
                size_t n=strlen(value);if(n>=capacity-used)return false;
                memcpy(out+used,value,n);used+=n;index++;
            }
            p=end;
        }
        out[used]=0;return true;
    }
    int n=snprintf(out,capacity,"%s",english);return n>=0&&(size_t)n<capacity;
}
