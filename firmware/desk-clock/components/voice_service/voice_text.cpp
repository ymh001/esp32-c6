#include "voice_text.h"
#include <string.h>

bool voice_text_clean(const char *input, char *output, size_t capacity)
{
    if (!output || !capacity) return false;
    output[0]=0;
    if (!input) return false;
    size_t length=strlen(input);
    while (length) {
        const char c=input[length-1];
        if(c==' ' || c=='\t' || c=='\r' || c=='\n'){--length;continue;}
        if(length>=3 && !memcmp(input+length-3,"\xef\xbf\xbd",3)){length-=3;continue;}
        break;
    }
    if(!length || length>=capacity)return false;
    memcpy(output,input,length);output[length]=0;
    return true;
}
