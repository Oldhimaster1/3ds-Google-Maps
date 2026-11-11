#ifndef TILE_PRIORITY_H
#define TILE_PRIORITY_H
/* Pure, fixed-memory viewport priority for interactive live map requests.
 * Keeping this independent of GPU/network lets the queue logic be host-tested. */
typedef struct {
    int zoom, world, start_x, start_y, tiles_x, tiles_y;
    int center_x, center_y;
    int gps_follow;
    int valid;
} TileViewport;
static inline int tile_wrap(int value, int world) {
    int result=value%world;
    return result<0?result+world:result;
}
static inline int tile_x_in_interval(int x,int start,int count,int world) {
    if (world<=0 || count<=0) return 0;
    if (count>=world) return 1;
    int delta=tile_wrap(x-start,world);
    return delta<count;
}
static inline int tile_view_score(const TileViewport *v,int x,int y,int zoom,int prefetch) {
    if (!v || !v->valid || v->world<=0 || zoom!=v->zoom) return -1;
    if (y<0 || y>=v->world) return -1;
    int visible_x=tile_x_in_interval(x,v->start_x,v->tiles_x,v->world);
    int visible_y=y>=v->start_y && y<v->start_y+v->tiles_y;
    if (!(visible_x && visible_y)) {
        /* No background ring during GPS follow. The existing prefetch
         * scheduler separately respects individual provider policies. */
        if (!prefetch || v->gps_follow) return -1;
        int px=tile_x_in_interval(x,v->start_x-1,v->tiles_x+2,v->world);
        int py=y>=v->start_y-1 && y<v->start_y+v->tiles_y+1;
        if (!px || !py) return -1;
    }
    int dx=tile_wrap(x-v->center_x,v->world);
    if (dx>v->world/2)dx-=v->world;
    int dy=y-v->center_y;
    int score=dx*dx+dy*dy;
    return visible_x&&visible_y?score:100+score;
}
/* Compare queued live requests as new visible tiles arrive. This preserves
 * centre-first priority even when the viewport stays within one tile grid. */
static inline int tile_request_outranks(const TileViewport *view,
    int nx,int ny,int nz,int np,int ox,int oy,int oz,int op) {
    if(np) return 0;
    if(op) return 1;
    int new_score=tile_view_score(view,nx,ny,nz,np);
    int other_score=tile_view_score(view,ox,oy,oz,op);
    return new_score>=0 && (other_score<0 || new_score<other_score);
}
#endif
