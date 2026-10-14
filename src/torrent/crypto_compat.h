#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <atomic>

/* Small dependency-free SHA-1 for the console build. BitTorrent requires
   SHA-1 for info hashes and piece validation, but OpenOrbis does not ship
   OpenSSL's compatibility API. */
inline uint32_t bt_rol(uint32_t v,unsigned n){return (v<<n)|(v>>(32-n));}
inline unsigned char *SHA1(const unsigned char *data,size_t len,unsigned char out[20]){
    uint64_t bits=(uint64_t)len*8;size_t total=(len+9+63)&~size_t(63);unsigned char *msg=(unsigned char*)std::calloc(1,total);if(!msg)return nullptr;
    std::memcpy(msg,data,len);msg[len]=0x80;for(int i=0;i<8;++i)msg[total-1-i]=(unsigned char)(bits>>(i*8));
    uint32_t h0=0x67452301,h1=0xefcdab89,h2=0x98badcfe,h3=0x10325476,h4=0xc3d2e1f0,w[80];
    for(size_t off=0;off<total;off+=64){for(int i=0;i<16;++i)w[i]=(uint32_t(msg[off+i*4])<<24)|(uint32_t(msg[off+i*4+1])<<16)|(uint32_t(msg[off+i*4+2])<<8)|msg[off+i*4+3];for(int i=16;i<80;++i)w[i]=bt_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;for(int i=0;i<80;++i){uint32_t f,k;if(i<20){f=(b&c)|((~b)&d);k=0x5a827999;}else if(i<40){f=b^c^d;k=0x6ed9eba1;}else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdc;}else{f=b^c^d;k=0xca62c1d6;}uint32_t t=bt_rol(a,5)+f+e+k+w[i];e=d;d=c;c=bt_rol(b,30);b=a;a=t;}h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;}
    std::free(msg);uint32_t h[5]={h0,h1,h2,h3,h4};for(int i=0;i<5;++i)for(int j=0;j<4;++j)out[i*4+j]=(unsigned char)(h[i]>>(24-j*8));return out;
}
// Peer IDs and tracker transaction IDs are non-secret identifiers. Avoid ELF
// thread_local here: the console's native threads do not establish musl TLS.
// Each call gets an independent state; the counter is shared atomically.
inline int RAND_bytes(unsigned char *out,int n){
    if(n<0||(!out&&n))return 0;
    static std::atomic<uint64_t> sequence{0};
    timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
    uint64_t state=sequence.fetch_add(0x9e3779b97f4a7c15ull,std::memory_order_relaxed)
        ^(uint64_t(t.tv_sec)*1000000000ull+uint64_t(t.tv_nsec));
    for(int i=0;i<n;){
        state+=0x9e3779b97f4a7c15ull;uint64_t v=state;
        v=(v^(v>>30))*0xbf58476d1ce4e5b9ull;v=(v^(v>>27))*0x94d049bb133111ebull;v^=v>>31;
        for(int j=0;j<8&&i<n;++j,++i)out[i]=static_cast<unsigned char>(v>>(j*8));
    }
    return 1;
}
