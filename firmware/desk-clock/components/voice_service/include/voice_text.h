#pragma once
#include <stddef.h>

// Remove only trailing whitespace and Unicode replacement characters emitted
// by ASR. Reject empty/oversized text rather than executing a truncated command.
bool voice_text_clean(const char *input, char *output, size_t capacity);
