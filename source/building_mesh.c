#include "building_mesh.h"
#include <math.h>
#include <string.h>
static float cross(float ax,float ay,float bx,float by,float cx,float cy){
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}
static bool inside(float ax,float az,float bx,float bz,float cx,float cz,float px,float pz,float winding){
    return winding*cross(ax,az,bx,bz,px,pz)>=-1e-6f &&
           winding*cross(bx,bz,cx,cz,px,pz)>=-1e-6f &&
           winding*cross(cx,cz,ax,az,px,pz)>=-1e-6f;
}
static bool segment_cross(float ax,float az,float bx,float bz,float cx,float cz,float dx,float dz){
    float a=cross(ax,az,bx,bz,cx,cz),b=cross(ax,az,bx,bz,dx,dz);
    float c=cross(cx,cz,dx,dz,ax,az),d=cross(cx,cz,dx,dz,bx,bz);
    return ((a>1e-5f&&b< -1e-5f)||(a< -1e-5f&&b>1e-5f))&&
           ((c>1e-5f&&d< -1e-5f)||(c< -1e-5f&&d>1e-5f));
}
bool building_mesh_add(BuildingMesh *m,const BuildingGeoPoint *input,unsigned count,
                       double lat,double lon,float tile_m,float height,float base,float origin){
    if(!m||!input||count<3||count>BUILDING_FOOTPRINT_MAX||
       !isfinite(lat)||!isfinite(lon)||fabs(lat)>85||fabs(lon)>180||
       !isfinite(tile_m)||tile_m<10||!isfinite(height)||height<2||height>1500||
       !isfinite(base)||!isfinite(origin))return false;
    /* OSM closed ways repeat the first point. */
    if(count>3 && fabs(input[0].lat-input[count-1].lat)<1e-10&&
        fabs(input[0].lon-input[count-1].lon)<1e-10)count--;
    if(count<3)return false;
    unsigned needed_vertices=count*4+count; /* walls + roof vertices */
    unsigned needed_indices=count*6+(count-2)*3;
    if(m->vertex_count+needed_vertices>BUILDING_MESH_VERT_MAX||
       m->index_count+needed_indices>BUILDING_MESH_IDX_MAX)return false;
    float xs[BUILDING_FOOTPRINT_MAX],zs[BUILDING_FOOTPRINT_MAX];
    const double rad=.017453292519943295;
    for(unsigned i=0;i<count;i++){
        if(!isfinite(input[i].lat)||!isfinite(input[i].lon)||
           fabs(input[i].lat)>85||fabs(input[i].lon)>180)return false;
        double dx=remainder(input[i].lon-lon,360.)*111320.*cos(lat*rad);
        double dz=(input[i].lat-lat)*111320.;
        xs[i]=(float)(dx*2./tile_m);
        zs[i]=(float)(-dz*2./tile_m);
        if(fabsf(xs[i])>1.6f||fabsf(zs[i])>1.6f)return false;
    }
    /* Reject degenerate or self-intersecting outlines rather than rendering
       inverted/flying geometry. Holes/multipolygons require separate parsing. */
    float area=0;
    for(unsigned i=0;i<count;i++){
        unsigned j=(i+1)%count;area+=xs[i]*zs[j]-xs[j]*zs[i];
        for(unsigned k=i+2;k<count;k++){
            unsigned t=(k+1)%count;
            if(t==i || (i==0&&k==count-1))continue;
            if(segment_cross(xs[i],zs[i],xs[j],zs[j],xs[k],zs[k],xs[t],zs[t]))return false;
        }
    }
    if(fabsf(area)<1e-5f)return false;
    float winding=area>0?1.f:-1.f;
    float by=(base-origin)*2.f/tile_m,top=by+height*2.f/tile_m;
    /* Build roof triangulation in local temporary storage. A bad roof
       must not leave partial data in the live GPU mesh. */
    uint16_t roof[(BUILDING_FOOTPRINT_MAX-2)*3];unsigned nr=0;
    unsigned remaining[BUILDING_FOOTPRINT_MAX];
    for(unsigned i=0;i<count;i++)remaining[i]=i;
    unsigned n=count;
    while(n>2){
        bool found=false;
        for(unsigned j=0;j<n;j++){
            unsigned a=remaining[(j+n-1)%n],b=remaining[j],c=remaining[(j+1)%n];
            if(winding*cross(xs[a],zs[a],xs[b],zs[b],xs[c],zs[c])<=1e-6f)continue;
            bool contains=false;
            for(unsigned k=0;k<n;k++){
                unsigned p=remaining[k];if(p==a||p==b||p==c)continue;
                if(inside(xs[a],zs[a],xs[b],zs[b],xs[c],zs[c],xs[p],zs[p],winding)){
                    contains=true;break;
                }
            }
            if(contains)continue;
            roof[nr++]=(uint16_t)a;roof[nr++]=(uint16_t)b;roof[nr++]=(uint16_t)c;
            for(unsigned k=j;k+1<n;k++)remaining[k]=remaining[k+1];
            --n;found=true;break;
        }
        if(!found)return false;
    }
    unsigned start=m->vertex_count,baseidx=m->index_count;
    for(unsigned i=0;i<count;i++){
        unsigned j=(i+1)%count;
        unsigned v=start+i*4;
        m->verts[v+0]=(BuildingMeshVertex){xs[i],by,zs[i],0,0};
        m->verts[v+1]=(BuildingMeshVertex){xs[j],by,zs[j],1,0};
        m->verts[v+2]=(BuildingMeshVertex){xs[i],top,zs[i],0,1};
        m->verts[v+3]=(BuildingMeshVertex){xs[j],top,zs[j],1,1};
        uint16_t tri[6]={v,v+2,v+1,v+1,v+2,v+3};
        for(unsigned k=0;k<6;k++)m->indices[baseidx++]=tri[k];
        m->verts[start+count*4+i]=(BuildingMeshVertex){xs[i],top,zs[i],
            .5f+xs[i]*.25f,.5f+zs[i]*.25f};
    }
    for(unsigned i=0;i<nr;i++)m->indices[baseidx++]=(uint16_t)(start+count*4+roof[i]);
    m->vertex_count+=needed_vertices;m->index_count=baseidx;m->buildings++;
    return true;
}
