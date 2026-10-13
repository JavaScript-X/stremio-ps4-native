#include "playback.h"
static unsigned clamp_byte(int n){return n<0?0:n>255?255:(unsigned)n;}
int playback_nv12(uint32_t *out,int ow,int oh,const uint8_t *data,size_t bytes,
    unsigned pitch,unsigned height,unsigned left,unsigned top,unsigned width,unsigned visible_height,int full_range){
    if(!out||!data||ow<=0||oh<=0||!pitch||!height||!width||!visible_height||pitch>8192||height>8192||pitch%2||height%2||
       left>=pitch||top>=height||width>pitch-left||visible_height>height-top||bytes<(size_t)pitch*height*3/2)return 0;
    int dw=ow,dh=(int)((uint64_t)ow*visible_height/width);
    if(dh>oh){dh=oh;dw=(int)((uint64_t)oh*width/visible_height);}if(dw<1||dh<1)return 0;
    int ox=(ow-dw)/2,oy=(oh-dh)/2;
    for(size_t i=0;i<(size_t)ow*oh;++i)out[i]=0xff000000u;
    const uint8_t *uv=data+(size_t)pitch*height;
    for(int y=0;y<dh;++y){unsigned sy=top+(unsigned)((uint64_t)y*visible_height/dh);
        for(int x=0;x<dw;++x){unsigned sx=left+(unsigned)((uint64_t)x*width/dw);
            int l=data[(size_t)sy*pitch+sx],u=uv[(size_t)(sy/2)*pitch+(sx&~1u)]-128,v=uv[(size_t)(sy/2)*pitch+(sx&~1u)+1]-128;
            int c=full_range?l*256:(l-16)*298;
            int r=(c+(full_range?359:409)*v+128)>>8;
            int g=(c-(full_range?88:100)*u-(full_range?183:208)*v+128)>>8;
            int b=(c+(full_range?454:516)*u+128)>>8;
            out[(size_t)(y+oy)*ow+x+ox]=0xff000000u|(clamp_byte(r)<<16)|(clamp_byte(g)<<8)|clamp_byte(b);
        }
    }return 1;
}
