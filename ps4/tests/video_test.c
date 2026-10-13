#include "videoout/playback.h"
#include "videoout/avplayer_abi.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    uint8_t nv12[]={16,235,16,235,128,128};uint32_t out[4];
    assert(playback_nv12(out,2,2,nv12,sizeof(nv12),2,2,0,0,2,2,0));
    assert(out[0]==0xff000000u&&out[1]==0xffffffffu);
    assert(!playback_nv12(out,2,2,nv12,5,2,2,0,0,2,2,0));
    assert(!playback_nv12(out,2,2,nv12,sizeof(nv12),2,2,1,0,2,2,0));
    assert(!playback_nv12(out,2,2,nv12,sizeof(nv12),1,2,0,0,1,2,0));
    uint32_t letterbox[16];assert(playback_nv12(letterbox,4,4,nv12,sizeof(nv12),2,2,0,0,2,1,0));
    assert(letterbox[0]==0xff000000u&&letterbox[15]==0xff000000u);
    puts("NV12 bounds, range and letterbox tests passed");return 0;
}
