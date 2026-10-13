#include "torrent/crypto_compat.h"
#include <cassert>
#include <cstring>
#include <iostream>
static void hex(const unsigned char *p,char out[41]){static const char d[]="0123456789abcdef";for(int i=0;i<20;++i){out[i*2]=d[p[i]>>4];out[i*2+1]=d[p[i]&15];}out[40]=0;}
int main(){unsigned char digest[20];char text[41];assert(SHA1((const unsigned char*)"abc",3,digest));hex(digest,text);assert(!std::strcmp(text,"a9993e364706816aba3e25717850c26c9cd0d89d"));unsigned char random[32]={0};assert(RAND_bytes(random,32)==1);bool any=false;for(auto c:random)any|=c!=0;assert(any);std::cout<<"Torrent crypto compatibility tests passed\n";}
