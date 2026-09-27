#include "sd_files.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace sd_files {
int path(const char *root, const char *relative, char *out, size_t capacity) {
    if (!root || !relative || !out || !capacity || relative[0]=='/') return EINVAL;
    const char *part=relative;
    for (const char *c=relative;;++c) {
        // Reject FAT aliases, drive prefixes and control characters.
        if (*c && ((unsigned char)*c<32 || *c==127 || strchr("\\:*?\"<>|",*c))) return EINVAL;
        if (*c=='/' || !*c) {
            size_t n=c-part;
            if (!n || (n==1 && part[0]=='.') || (n==2 && !memcmp(part,"..",2)) ||
                part[n-1]==' ' || part[n-1]=='.') return EINVAL;
            part=c+1;
        }
        if (!*c) break;
    }
    int n=snprintf(out,capacity,"%s/%s",root,relative);
    return n<0 || (size_t)n>=capacity ? ENAMETOOLONG : 0;
}
int write_new(const char *root, const char *relative, const void *data, size_t size) {
    if (!data && size) return EINVAL;
    char file[path_size];int err=path(root,relative,file,sizeof(file));if(err)return err;
    // Never truncate an existing user file, including our diagnostic filename.
    int fd=open(file,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return errno;
    size_t offset=0;
    while(offset<size){
        ssize_t n=::write(fd,(const char *)data+offset,size-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){err=n<0?errno:EIO;break;}offset+=(size_t)n;
    }
    if(!err && fsync(fd))err=errno;
    if(close(fd) && !err)err=errno;
    return err;
}
int read(const char *root,const char *relative,void *data,size_t capacity,size_t *size) {
    if(!size || (!data && capacity))return EINVAL;
    *size=0;char file[path_size];int err=path(root,relative,file,sizeof(file));if(err)return err;
    int fd=open(file,O_RDONLY);if(fd<0)return errno;
    while(*size<capacity){
        ssize_t n=::read(fd,(char *)data+*size,capacity-*size);
        if(n<0 && errno==EINTR)continue;
        if(n<0){err=errno;break;}if(!n)break;*size+=(size_t)n;
    }
    if(close(fd) && !err)err=errno;
    return err;
}
static unsigned char pattern(size_t i){return (unsigned char)((i*37U+(i>>8)+0x5aU)&255U);}
int self_test(const char *root,unsigned nonce,char *name,size_t capacity) {
    if(!name || !capacity)return EINVAL;
    // Short FAT filename, exclusive creation; write across multiple sectors.
    char file[path_size];int fd=-1,err=0;
    for(unsigned attempt=0;attempt<16;++attempt){
        int n=snprintf(name,capacity,"PH%06X.TST",(nonce+attempt)&0xffffffU);
        if(n<0 || (size_t)n>=capacity)return ENAMETOOLONG;
        if((err=path(root,name,file,sizeof(file))))return err;
        fd=open(file,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd>=0)break;
        if(errno!=EEXIST)return errno;
    }
    if(fd<0)return EEXIST;
    unsigned char block[512];
    for(size_t offset=0;offset<8192 && !err;offset+=sizeof(block)){
        for(size_t i=0;i<sizeof(block);++i)block[i]=pattern(offset+i);
        size_t done=0;
        while(done<sizeof(block)){
            ssize_t n=::write(fd,block+done,sizeof(block)-done);
            if(n<0 && errno==EINTR)continue;
            if(n<=0){err=n<0?errno:EIO;break;}done+=(size_t)n;
        }
    }
    if(!err && fsync(fd))err=errno;
    if(close(fd) && !err)err=errno;
    if(err)return err; // Keep a failed test file for diagnosis.
    fd=open(file,O_RDONLY);if(fd<0)return errno;
    size_t total=0;
    while(!err){
        ssize_t n=::read(fd,block,sizeof(block));
        if(n<0 && errno==EINTR)continue;
        if(n<0){err=errno;break;}if(!n)break;
        if(total+(size_t)n>8192){err=EIO;break;}
        for(ssize_t i=0;i<n;++i)if(block[i]!=pattern(total+(size_t)i)){err=EIO;break;}
        total+=(size_t)n;
    }
    if(!err && total!=8192)err=EIO;
    if(close(fd) && !err)err=errno;
    if(!err && unlink(file))err=errno;
    return err;
}
}
