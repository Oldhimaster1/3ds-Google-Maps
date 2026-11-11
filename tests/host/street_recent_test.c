#include "street_recent.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    StreetRecent c={0};
    PanoLiveImage a={.provider=STREET_PROVIDER_PANORAMAX},b=a,d=a;
    strcpy(a.id,"a");strcpy(b.id,"b");strcpy(d.id,"d");
    uint16_t *v1=malloc(2), *v2=malloc(2), *v3=malloc(2);
    assert(v1&&v2&&v3);
    assert(street_recent_take(&c,&a,v1));
    assert(street_recent_take(&c,&b,v2));
    assert(street_recent_find(&c,STREET_PROVIDER_PANORAMAX,"a")!=NULL);
    assert(street_recent_take(&c,&d,v3));
    assert(street_recent_find(&c,STREET_PROVIDER_PANORAMAX,"b")==NULL);
    assert(street_recent_find(&c,STREET_PROVIDER_PANORAMAX,"a")!=NULL);
    assert(street_recent_find(&c,STREET_PROVIDER_PANORAMAX,"d")!=NULL);
    PanoLiveImage google=a;google.provider=STREET_PROVIDER_GOOGLE;
    uint16_t *test=malloc(2);assert(test);
    assert(!street_recent_take(&c,&google,test));free(test);
    street_recent_clear(&c);
    assert(street_recent_find(&c,STREET_PROVIDER_PANORAMAX,"a")==NULL);
    return 0;
}
