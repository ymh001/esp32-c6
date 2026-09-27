#include "sd_files.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

int main() {
    char temp[]="/tmp/pocket-sd-test-XXXXXX";
    const char *root=mkdtemp(temp);assert(root);
    char out[sd_files::path_size];
    for(const char *bad:{"","/outside","../outside","dir/../../x","a//b","a/./b","a/..","a/","a\\b","0:foo","x.","x ","a\nx"})
        assert(sd_files::path(root,bad,out,sizeof(out))==EINVAL);
    assert(sd_files::path(root,"中文文件.txt",out,sizeof(out))==0);
    assert(sd_files::path(root,"hello.txt",out,4)==ENAMETOOLONG);
    assert(sd_files::path(root,std::string(300,'x').c_str(),out,sizeof(out))==ENAMETOOLONG);
    std::filesystem::create_directory(std::string(root)+"/folder");
    std::array<unsigned char,1537> data{};
    for(size_t i=0;i<data.size();++i)data[i]=(unsigned char)(i*53);
    assert(sd_files::write_new(root,"folder/中文.bin",data.data(),data.size())==0);
    assert(sd_files::write_new(root,"folder/中文.bin","replace",7)==EEXIST);
    std::array<unsigned char,2048> result{};size_t size=0;
    assert(sd_files::read(root,"folder/中文.bin",result.data(),result.size(),&size)==0);
    assert(size==data.size() && !memcmp(data.data(),result.data(),size));
    assert(sd_files::read(root,"folder/中文.bin",result.data(),17,&size)==0 && size==17);
    assert(sd_files::read(root,"absent",result.data(),result.size(),&size)==ENOENT && size==0);
    assert(sd_files::write_new(root,"../outside","x",1)==EINVAL);
    // A test filename collision must preserve the user's original file.
    assert(sd_files::write_new(root,"PH000123.TST","original",8)==0);
    char name[16];
    for(int i=0;i<10;++i){
        assert(sd_files::self_test(root,0x123,name,sizeof(name))==0);
        assert(!strcmp(name,"PH000124.TST"));
        assert(!std::filesystem::exists(std::string(root)+"/"+name));
    }
    assert(sd_files::read(root,"PH000123.TST",result.data(),result.size(),&size)==0);
    assert(size==8 && !memcmp(result.data(),"original",8));
    // Exhausted exclusive names and missing media both fail without overwriting.
    for(unsigned i=1;i<16;++i){snprintf(name,sizeof(name),"PH%06X.TST",0x123+i);assert(sd_files::write_new(root,name,"x",1)==0);}
    assert(sd_files::self_test(root,0x123,name,sizeof(name))==EEXIST);
    assert(sd_files::self_test("/nonexistent-pocket-sd-root",0,name,sizeof(name))==ENOENT);
    std::filesystem::remove_all(root);
    puts("SD filesystem tests passed: binary round-trip, UTF-8, bounded reads, traversal rejection, exclusive writes, self-test collision and cleanup");
}
