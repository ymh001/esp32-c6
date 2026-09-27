#pragma once
#include <stddef.h>
#include <stdint.h>
#include <dirent.h>
namespace pocket::files {
constexpr size_t chunk_size=1024, path_size=288;
enum class Op { ListOpen, ListNext, ListClose, ReadOpen, Read, WriteOpen, Write, Commit, Remove, Cleanup };
struct Job {
    Op op{}; char path[path_size]{}; char name[256]{};
    unsigned char data[chunk_size]{};
    size_t count=0; uint64_t size=0; bool directory=false, end=false; int error=0;
};
// Access only from the UI/main task. Each operation is bounded to one chunk/entry.
class Session {
public:
    explicit Session(const char *root): root_(root) {}
    ~Session();
    void execute(Job &job);
private:
    const char *root_; int fd_=-1; DIR *dir_=nullptr;
    bool upload_=false; uint64_t expected_=0,written_=0;
    char target_[path_size]{}, temporary_[path_size]{}, listing_[path_size]{};
    int full_path(const char *relative,char *out,bool root=false);
    void cleanup();
};
int decode(const char *encoded,char *out,size_t capacity);
size_t json_escape(const char *text,char *out,size_t capacity);
}
