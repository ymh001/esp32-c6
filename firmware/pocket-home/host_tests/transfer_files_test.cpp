#include "core/transfer_files.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>
using namespace pocket::files;
int main(){
 char temp[]="/tmp/pocket-transfer-XXXXXX";assert(mkdtemp(temp));
 Session s(temp);Job j;
 auto op=[&](Op o){j.op=o;s.execute(j);return j.error;};
 auto set=[&](const char *p){snprintf(j.path,sizeof(j.path),"%s",p);};
 std::filesystem::create_directory(std::string(temp)+"/目录");
 std::vector<unsigned char> input(128*1024+17);for(size_t i=0;i<input.size();++i)input[i]=(i*31+(i>>8))&255;
 set("目录/照片 空格.bin");j.size=input.size();assert(!op(Op::WriteOpen));
 assert(!std::filesystem::exists(std::string(temp)+"/目录/照片 空格.bin"));
 for(size_t n=0;n<input.size();n+=chunk_size){j.count=std::min(chunk_size,input.size()-n);memcpy(j.data,input.data()+n,j.count);assert(!op(Op::Write));}
 assert(!op(Op::Commit));assert(!op(Op::ReadOpen));assert(j.size==input.size());
 std::vector<unsigned char> output;
 do {assert(!op(Op::Read));output.insert(output.end(),j.data,j.data+j.count);}while(!j.end);
 assert(output==input);assert(!op(Op::Cleanup));
 j.size=1;assert(op(Op::WriteOpen)==EEXIST); // Never overwrite completed files.
 set("incomplete.bin");j.size=5000;assert(!op(Op::WriteOpen));j.count=17;assert(!op(Op::Write));assert(!op(Op::Cleanup));
 assert(!std::filesystem::exists(std::string(temp)+"/incomplete.bin"));
 set("short.bin");j.size=5000;assert(!op(Op::WriteOpen));j.count=17;assert(!op(Op::Write));assert(op(Op::Commit)==EIO);
 set("too-large.bin");j.size=2;assert(!op(Op::WriteOpen));j.count=3;assert(op(Op::Write)==EFBIG);assert(!op(Op::Cleanup));
 set("empty.bin");j.size=0;assert(!op(Op::WriteOpen));assert(!op(Op::Commit));
 for(auto p:{"../escape","/absolute","x/../../escape",".ph-upload-secret.tmp",".PH-UPLOAD-secret.tmp"}){set(p);assert(op(Op::WriteOpen)!=0);assert(op(Op::Remove)!=0);}
 set("");assert(!op(Op::ListOpen));unsigned count=0;do{assert(!op(Op::ListNext));if(!j.end&&j.name[0]){assert(strncmp(j.name,".ph-upload-",11));++count;}}while(!j.end);assert(count==2);assert(!op(Op::ListClose));
 set("目录");assert(op(Op::Remove)==EISDIR);set("目录/照片 空格.bin");assert(!op(Op::Remove));assert(op(Op::ReadOpen)==ENOENT);
 char decoded[32],escaped[64];assert(!decode("%E4%B8%AD%20a%2Bb",decoded,sizeof(decoded)));assert(!strcmp(decoded,"中 a+b"));
 for(auto p:{"bad%00name","bad%0Aname","bad%","bad%GG"})assert(decode(p,decoded,sizeof(decoded)));
 assert(decode("toolong",decoded,3)==ENAMETOOLONG);
 assert(json_escape("a\"b\\c\n",escaped,sizeof(escaped)));assert(!strcmp(escaped,"a\\\"b\\\\c\\u000a"));
 assert(!op(Op::Cleanup));std::filesystem::remove_all(temp);
 puts("PASS: streaming round-trip, abort cleanup, atomic completion, collision protection, empty files, paths, JSON and URL decoding");
}
