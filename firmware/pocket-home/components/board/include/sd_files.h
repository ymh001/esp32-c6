#pragma once
#include <stddef.h>
// Portable, bounded filesystem helpers. Return 0 on success or an errno value.
namespace sd_files {
constexpr size_t path_size = 288;
int path(const char *root, const char *relative, char *out, size_t capacity);
int write_new(const char *root, const char *relative, const void *data, size_t size);
int read(const char *root, const char *relative, void *data, size_t capacity,
         size_t *size);
int self_test(const char *root, unsigned nonce, char *name, size_t capacity);
}
