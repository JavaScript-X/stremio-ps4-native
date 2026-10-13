#pragma once
#include <stddef.h>
#include <sys/types.h>
size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(off_t,off_t,size_t,size_t,int,off_t*);
int sceKernelMapDirectMemory(void**,size_t,int,int,off_t,size_t);
int sceKernelReleaseDirectMemory(off_t,size_t);
int sceKernelMunmap(void*,size_t);
int sceKernelLoadStartModule(const char*,size_t,const void*,unsigned,void*,int*);
int sceKernelDlsym(int,const char*,void**);
int sceKernelUsleep(unsigned);
