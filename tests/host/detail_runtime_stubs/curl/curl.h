#ifndef DETAIL_HOST_CURL_H
#define DETAIL_HOST_CURL_H
typedef struct MockCurl {int dummy;} CURL;
#define CURLOPT_TIMEOUT 1
#define CURLOPT_CONNECTTIMEOUT 2
#define CURLOPT_FOLLOWLOCATION 3
#define CURLOPT_USERAGENT 4
#define curl_easy_setopt(h,opt,arg) do{(void)(h);(void)(opt);(void)(arg);}while(0)
#endif
