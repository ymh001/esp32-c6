#include "transfer_files.h"
#include "sd_files.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
namespace pocket::files {
int decode(const char *s,char *out,size_t cap) {
    auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    size_t n=0;
    for(;*s;++s){
        unsigned char c=*s;
        if(c=='%'){if(!s[1]||!s[2])return EINVAL;int a=hex(s[1]),b=hex(s[2]);if(a<0||b<0)return EINVAL;c=(a<<4)|b;s+=2;}
        else if(c=='+')c=' ';
        if(!c||c<32||c==127)return EINVAL;
        if(n+1>=cap)return ENAMETOOLONG;
        out[n++]=c;
    }
    if(!cap)return ENAMETOOLONG;
    out[n]=0;return 0;
}
size_t json_escape(const char *s,char *out,size_t cap) {
    size_t n=0;
    for(;*s;++s){unsigned char c=*s;
        if(c<32){if(n+6>=cap)return 0;snprintf(out+n,7,"\\u%04x",c);n+=6;}
        else {if(n+(c=='"'||c=='\\'?2:1)>=cap)return 0;if(c=='"'||c=='\\')out[n++]='\\';out[n++]=c;}
    }
    if(!cap)return 0;
    out[n]=0;return n;
}
int Session::full_path(const char *relative,char *out,bool root) {
    if(root&&!relative[0]){snprintf(out,path_size,"%s",root_);return 0;}
    for(const char *p=relative;*p;++p)
        if((p==relative||p[-1]=='/')&&!strncasecmp(p,".ph-upload-",11))return EACCES;
    return sd_files::path(root_,relative,out,path_size);
}
void Session::cleanup() {
    if(fd_>=0){close(fd_);fd_=-1;}
    if(upload_&&temporary_[0])unlink(temporary_);
    upload_=false;temporary_[0]=0;
    if(dir_){closedir(dir_);dir_=nullptr;}
}
Session::~Session(){cleanup();}
void Session::execute(Job &j) {
    j.error=0;j.end=false;
    char full[path_size];struct stat st{};
    auto fail=[&](int err){j.error=err?err:EIO;};
    switch(j.op){
    case Op::Cleanup:cleanup();break;
    case Op::ListOpen:
        cleanup();if((j.error=full_path(j.path,listing_,true)))break;
        dir_=opendir(listing_);if(!dir_)fail(errno);break;
    case Op::ListNext: {
        if(!dir_){fail(EBADF);break;}
        // At most one entry per UI iteration, including hidden entries.
        errno=0;auto ent=readdir(dir_);j.name[0]=0;
        if(!ent){if(errno)fail(errno);else j.end=true;break;}
        if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,"..")||!strncasecmp(ent->d_name,".ph-upload-",11))break;
        if(strlen(ent->d_name)>=sizeof(j.name)){fail(ENAMETOOLONG);break;}
        if(snprintf(full,sizeof(full),"%s/%s",listing_,ent->d_name)>=(int)sizeof(full)){fail(ENAMETOOLONG);break;}
        if(stat(full,&st)){fail(errno);break;}
        snprintf(j.name,sizeof(j.name),"%s",ent->d_name);j.directory=S_ISDIR(st.st_mode);j.size=st.st_size;break;
    }
    case Op::ListClose:if(dir_){closedir(dir_);dir_=nullptr;}break;
    case Op::ReadOpen:
        cleanup();if((j.error=full_path(j.path,full)))break;
        if(stat(full,&st)){fail(errno);break;}
        if(!S_ISREG(st.st_mode)){fail(EISDIR);break;}
        fd_=open(full,O_RDONLY);if(fd_<0)fail(errno);else j.size=st.st_size;break;
    case Op::Read: {
        if(fd_<0||upload_){fail(EBADF);break;}
        ssize_t n;do{n=read(fd_,j.data,sizeof(j.data));}while(n<0&&errno==EINTR);
        if(n<0)fail(errno);else {j.count=n;j.end=n==0;}break;
    }
    case Op::WriteOpen: {
        cleanup();if((j.error=full_path(j.path,target_)))break;
        if(!stat(target_,&st)){fail(EEXIST);break;}if(errno!=ENOENT){fail(errno);break;}
        snprintf(temporary_,sizeof(temporary_),"%s",target_);
        auto slash=strrchr(temporary_,'/');if(!slash){fail(EINVAL);break;}
        size_t remain=sizeof(temporary_)-(slash+1-temporary_);
        if(remain<28){fail(ENAMETOOLONG);break;}
        // Exclusive creation keeps both user files and abandoned uploads intact.
        static unsigned nonce=0;
        for(unsigned i=0;i<32;++i){snprintf(slash+1,remain,".ph-upload-%08x.tmp",++nonce);fd_=open(temporary_,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd_>=0||errno!=EEXIST)break;}
        if(fd_<0){fail(errno);temporary_[0]=0;break;}
        upload_=true;expected_=j.size;written_=0;break;
    }
    case Op::Write: {
        if(fd_<0||!upload_){fail(EBADF);break;}
        if(j.count>sizeof(j.data)||written_+j.count>expected_){fail(EFBIG);break;}
        size_t done=0;
        while(done<j.count){ssize_t n=write(fd_,j.data+done,j.count-done);if(n<0&&errno==EINTR)continue;if(n<=0){fail(n<0?errno:EIO);break;}done+=n;}
        written_+=done;break;
    }
    case Op::Commit:
        if(fd_<0||!upload_){fail(EBADF);break;}
        if(written_!=expected_){fail(EIO);cleanup();break;}
        if(fsync(fd_))fail(errno);
        if(close(fd_)&&!j.error)fail(errno);
        fd_=-1;
        if(!j.error){if(!stat(target_,&st))fail(EEXIST);else if(errno!=ENOENT)fail(errno);}
        if(!j.error&&rename(temporary_,target_))fail(errno);
        if(!j.error){upload_=false;temporary_[0]=0;}
        cleanup();break;
    case Op::Remove:
        if((j.error=full_path(j.path,full)))break;
        if(stat(full,&st)){fail(errno);break;}
        if(!S_ISREG(st.st_mode)){fail(EISDIR);break;}
        if(unlink(full))fail(errno);
        break;
    }
}
}
