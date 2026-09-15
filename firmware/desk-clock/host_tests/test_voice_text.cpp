#include "voice_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main()
{
    struct Example { const char *input, *expected; };
    const Example examples[] = {
        {"打开屏幕挂灯�", "打开屏幕挂灯"},
        {"打开屏幕挂灯�� \r\n", "打开屏幕挂灯"},
        {"关闭屏幕挂灯，", "关闭屏幕挂灯，"},
        {"打开屏幕挂灯", "打开屏幕挂灯"},
        // Never guess a replacement for a corrupted word inside the command.
        {"打开�挂灯", "打开�挂灯"},
        {"客厅灯开着吗？", "客厅灯开着吗？"},
    };
    char output[64];
    for(const auto &example:examples){
        assert(voice_text_clean(example.input,output,sizeof(output)));
        assert(!strcmp(output,example.expected));
    }
    assert(!voice_text_clean("� \n",output,sizeof(output)) && output[0]==0);
    assert(!voice_text_clean(nullptr,output,sizeof(output)));
    assert(!voice_text_clean("打开屏幕挂灯",output,8) && output[0]==0);
    assert(!voice_text_clean("a",nullptr,0));
    assert(voice_text_clean("开�",output,4) && !strcmp(output,"开"));
    puts("voice text tests passed");
}
