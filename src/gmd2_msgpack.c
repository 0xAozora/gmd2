#include "gmd2.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

typedef struct { const uint8_t *p,*end; int bad; } Reader;
typedef struct {
    uint32_t index;
    double left,bottom,right,top;
} RawGlyph;

static uint64_t be(Reader *r, unsigned n) {
    if(r->bad||(size_t)(r->end-r->p)<n) {r->bad=1;return 0;}
    uint64_t v=0;
    for(unsigned i=0;i<n;i++) v=(v<<8)|*r->p++;
    return v;
}
static uint8_t tag(Reader *r) {return (uint8_t)be(r,1);}
static int size_of_map(Reader *r, uint32_t *n) {
    uint8_t t=tag(r);
    if((t&0xf0)==0x80){*n=t&15;return !r->bad;}
    if(t==0xde){*n=(uint32_t)be(r,2);return !r->bad;}
    if(t==0xdf){*n=(uint32_t)be(r,4);return !r->bad;}
    r->bad=1;return 0;
}
static int size_of_array(Reader *r,uint32_t *n) {
    uint8_t t=tag(r);
    if((t&0xf0)==0x90){*n=t&15;return !r->bad;}
    if(t==0xdc){*n=(uint32_t)be(r,2);return !r->bad;}
    if(t==0xdd){*n=(uint32_t)be(r,4);return !r->bad;}
    r->bad=1;return 0;
}
static int text(Reader *r,char *s,size_t cap) {
    uint8_t t=tag(r);uint32_t n;
    if((t&0xe0)==0xa0)n=t&31;
    else if(t==0xd9)n=(uint32_t)be(r,1);
    else if(t==0xda)n=(uint32_t)be(r,2);
    else if(t==0xdb)n=(uint32_t)be(r,4);
    else {r->bad=1;return 0;}
    if(r->bad||(size_t)(r->end-r->p)<n||n>=cap){r->bad=1;return 0;}
    memcpy(s,r->p,n);s[n]=0;r->p+=n;return 1;
}
static int number(Reader *r,double *out) {
    uint8_t t=tag(r);uint64_t v;
    if(t<=0x7f){*out=(double)t;return 1;}
    if(t>=0xe0){*out=(double)(int8_t)t;return 1;}
    switch(t) {
        case 0xcc:v=be(r,1);break;
        case 0xcd:v=be(r,2);break;
        case 0xce:v=be(r,4);break;
        case 0xcf:v=be(r,8);break;
        case 0xd0:*out=(double)(int8_t)be(r,1);return !r->bad;
        case 0xd1:*out=(double)(int16_t)be(r,2);return !r->bad;
        case 0xd2:*out=(double)(int32_t)be(r,4);return !r->bad;
        case 0xd3:*out=(double)(int64_t)be(r,8);return !r->bad;
        case 0xca: {uint32_t bits=(uint32_t)be(r,4);float f;
            memcpy(&f,&bits,4);*out=f;return !r->bad;}
        case 0xcb: {uint64_t bits=be(r,8);double d;
            memcpy(&d,&bits,8);*out=d;return !r->bad;}
        default:r->bad=1;return 0;
    }
    *out=(double)v;return !r->bad;
}
static int skip(Reader *r,unsigned depth) {
    if(depth>64){r->bad=1;return 0;}
    uint8_t t=tag(r);if(r->bad)return 0;
    if(t<=0x7f||t>=0xe0||t==0xc0||t==0xc2||t==0xc3)return 1;
    if((t&0xe0)==0xa0) {size_t n=t&31;
        if((size_t)(r->end-r->p)<n){r->bad=1;return 0;}
        r->p+=n;return 1;}
    uint32_t n=0;int map=0,array=0;
    if((t&0xf0)==0x80){n=t&15;map=1;}
    else if((t&0xf0)==0x90){n=t&15;array=1;}
    else if(t==0xde){n=(uint32_t)be(r,2);map=1;}
    else if(t==0xdf){n=(uint32_t)be(r,4);map=1;}
    else if(t==0xdc){n=(uint32_t)be(r,2);array=1;}
    else if(t==0xdd){n=(uint32_t)be(r,4);array=1;}
    if(map||array) {
        if(map&&n>UINT32_MAX/2){r->bad=1;return 0;}
        uint64_t count=(uint64_t)n*(map?2:1);
        if(count>(uint64_t)(r->end-r->p)) {r->bad=1;return 0;}
        while(count--)if(!skip(r,depth+1))return 0;
        return !r->bad;
    }
    uint64_t sz=0;
    switch(t) {
        case 0xc4:case 0xd9:sz=be(r,1);break;
        case 0xc5:case 0xda:sz=be(r,2);break;
        case 0xc6:case 0xdb:sz=be(r,4);break;
        case 0xc7:sz=be(r,1)+1;break;
        case 0xc8:sz=be(r,2)+1;break;
        case 0xc9:sz=be(r,4)+1;break;
        case 0xd4:sz=2;break; case 0xd5:sz=3;break;
        case 0xd6:sz=5;break; case 0xd7:sz=9;break;
        case 0xd8:sz=17;break;
        case 0xcc:case 0xd0:sz=1;break;
        case 0xcd:case 0xd1:sz=2;break;
        case 0xce:case 0xd2:case 0xca:sz=4;break;
        case 0xcf:case 0xd3:case 0xcb:sz=8;break;
        default:r->bad=1;return 0;
    }
    if(r->bad||sz>(uint64_t)(r->end-r->p)){r->bad=1;return 0;}
    r->p+=(size_t)sz;return 1;
}
static int read_atlas(Reader *r,uint32_t *w,uint32_t *h) {
    uint32_t n;if(!size_of_map(r,&n))return 0;
    for(uint32_t i=0;i<n;i++) {
        char k[64];if(!text(r,k,sizeof(k)))return 0;
        if(!strcmp(k,"w")||!strcmp(k,"h")) {
            double v;if(!number(r,&v)||!isfinite(v)||v<1||v>UINT32_MAX||floor(v)!=v)
                return 0;
            if(k[0]=='w')*w=(uint32_t)v;else *h=(uint32_t)v;
        }else if(!skip(r,0))return 0;
    }
    return 1;
}
static int read_bounds(Reader *r,RawGlyph *g) {
    uint32_t n;if(!size_of_map(r,&n))return 0;
    for(uint32_t i=0;i<n;i++) {
        char k[64];if(!text(r,k,sizeof(k)))return 0;
        if(strlen(k)==1&&strchr("lbrt",k[0])) {
            double v;if(!number(r,&v)||!isfinite(v))return 0;
            switch(k[0]) {
                case 'l':g->left=v;break;
                case 'b':g->bottom=v;break;
                case 'r':g->right=v;break;
                case 't':g->top=v;break;
            }
        }else if(!skip(r,0))return 0;
    }
    return 1;
}
static int read_glyphs(Reader *r,RawGlyph **glyphs,size_t *count) {
    uint32_t n;if(!size_of_array(r,&n))return 0;
    if(n && SIZE_MAX/(size_t)n < sizeof(RawGlyph)){r->bad=1;return 0;}
    RawGlyph *a=(RawGlyph*)calloc(n?n:1,sizeof(RawGlyph));if(!a)return 0;
    for(uint32_t i=0;i<n;i++) {
        uint32_t members;
        if(!size_of_map(r,&members)){free(a);return 0;}
        for(uint32_t j=0;j<members;j++) {
            char k[64];if(!text(r,k,sizeof(k))){free(a);return 0;}
            if(!strcmp(k,"i")) {
                double x;if(!number(r,&x)||!isfinite(x)||x<0||x>UINT32_MAX||floor(x)!=x){free(a);return 0;}
                a[i].index=(uint32_t)x;
            }else if(!strcmp(k,"b")) {
                if(!read_bounds(r,&a[i])){free(a);return 0;}
            }else if(!skip(r,0)){free(a);return 0;}
        }
    }
    *glyphs=a;*count=n;return 1;
}

int gmd2_layout_from_msgpack(const uint8_t *data,size_t len,
                            Gmd2Metadata *layout,char *err,size_t cap) {
    if(!layout||!data){if(err&&cap)snprintf(err,cap,"invalid parameters");return -1;}
    memset(layout,0,sizeof(*layout));
    Reader r={data,data+len,0};uint32_t n=0,w=0,h=0;
    RawGlyph *raw=NULL;size_t count=0;
    if(!size_of_map(&r,&n))goto bad;
    for(uint32_t i=0;i<n;i++) {
        char k[64];if(!text(&r,k,sizeof(k)))goto bad;
        if(!strcmp(k,"a")) {
            if(!read_atlas(&r,&w,&h))goto bad;
        }else if(!strcmp(k,"g")) {
            if(raw||!read_glyphs(&r,&raw,&count))goto bad;
        }else if(!skip(&r,0))goto bad;
    }
    if(r.bad||r.p!=r.end||!w||!h||!raw||
       (uint64_t)w*h>UINT32_MAX)goto bad;
    layout->glyphs=(Gmd2GlyphRect*)calloc(count?count:1,sizeof(Gmd2GlyphRect));
    if(!layout->glyphs)goto bad;
    layout->glyph_count=count;layout->width=w;layout->height=h;
    for(size_t i=0;i<count;i++) {
        double fw=nearbyint(raw[i].right-raw[i].left);
        double fh=nearbyint(raw[i].top-raw[i].bottom);
        double xx=floor(raw[i].left),yy=(double)h-ceil(raw[i].top);
        if(!isfinite(fw)||!isfinite(fh)||!isfinite(xx)||!isfinite(yy)||
           fw<INT32_MIN||fw>UINT32_MAX||fh<INT32_MIN||fh>UINT32_MAX||
           xx<INT32_MIN||xx>INT32_MAX||yy<INT32_MIN||yy>INT32_MAX)goto bad;
        layout->glyphs[i]=(Gmd2GlyphRect){raw[i].index,(int32_t)xx,(int32_t)yy,
                         fw>0?(uint32_t)fw:0,fh>0?(uint32_t)fh:0,(uint32_t)i};
    }
    free(raw);return 0;
bad:
    free(raw);gmd2_free_layout(layout);
    if(err&&cap)snprintf(err,cap,"invalid/unsupported FontLayoutMsgp structure");
    return -1;
}
void gmd2_free_layout(Gmd2Metadata *layout) {
    if(layout){free(layout->glyphs);memset(layout,0,sizeof(*layout));}
}
