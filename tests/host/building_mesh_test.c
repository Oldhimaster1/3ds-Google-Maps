#include "building_mesh.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
 BuildingMesh *m=calloc(1,sizeof(*m));assert(m);
 BuildingGeoPoint square[5]={{40, -74},{40,-73.9999},{40.0001,-73.9999},{40.0001,-74},{40,-74}};
 assert(building_mesh_add(m,square,5,40.00005,-73.99995,100,12,0,0));
 assert(m->vertex_count==20&&m->index_count==30&&m->buildings==1);
 for(unsigned i=0;i<m->index_count;i++)assert(m->indices[i]<m->vertex_count);
 float expected=m->verts[2].y;assert(expected>.20f&&expected<.25f);
 BuildingGeoPoint concave[6]={{40,-74},{40,-73.9999},{40.00005,-73.9999},
      {40.00005,-73.99995},{40.0001,-73.99995},{40.0001,-74}};
 assert(building_mesh_add(m,concave,6,40.00005,-73.99995,100,20,0,0));
 assert(m->index_count==30+(6*6+4*3));
 BuildingGeoPoint bowtie[4]={{40,-74},{40.0001,-73.9999},{40,-73.9999},{40.0001,-74}};
 assert(!building_mesh_add(m,bowtie,4,40.00005,-73.99995,100,10,0,0));
 assert(!building_mesh_add(m,square,5,40.00005,-73.99995,100,NAN,0,0));
 for(unsigned i=0;i<m->index_count;i++)assert(m->indices[i]<m->vertex_count);
 free(m);puts("Stereoscopic building mesh geometry bounds and triangulation passed");return 0;
}
