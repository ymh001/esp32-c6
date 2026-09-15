#!/usr/bin/env python3
"""Compile and exercise the actual polling/header functions from an IDF source file.

Run after idf.py build. Passing the unpatched IDF transport_ws.c instead should
fail the buffered-frame case, reproducing the HA greeting timeout regression.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/patched/transport_ws.c"
match = re.search(r"static int ws_poll_read\([^\n]+\)\n\{.*?\n\}", source.read_text(), re.S)
header_match = re.search(r"static int ws_read_header\([^\n]+\)\n\{.*?\n\}", source.read_text(), re.S)
if not match or not header_match:
    raise SystemExit("ws_poll_read was not found; build the firmware first")

harness = r'''
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define MAX_WEBSOCKET_HEADER_SIZE 14
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef void *esp_transport_handle_t;
typedef struct {
    size_t buffer_len; esp_transport_handle_t parent;
    const char *buffer;
    struct { bool header_received, fin; int opcode, payload_len, bytes_remaining;
        char mask_key[4]; } frame_state;
} transport_ws_t;
static int calls, parent_result, last_timeout;
static void *esp_transport_get_context_data(esp_transport_handle_t t) { return t; }
static int esp_transport_poll_read(esp_transport_handle_t t, int timeout) {
    (void)t; ++calls; last_timeout=timeout; return parent_result;
}
static int esp_transport_read_exact_size(transport_ws_t *ws, char *out, int len, int timeout) {
    (void)timeout;
    if ((size_t)len>ws->buffer_len) return -1;
    memcpy(out, ws->buffer, len); ws->buffer+=len; ws->buffer_len-=len;
    return len;
}
'''
harness += match.group(0) + "\n" + header_match.group(0)
harness += r'''
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)
int main(void) {
    transport_ws_t transport={0};
    // HA's greeting arrived with HTTP 101. TCP has no additional bytes.
    // Check a partial frame too: any buffered byte requires read processing.
    const size_t lengths[]={1, 50, 2048};
    for (unsigned i=0; i<sizeof(lengths)/sizeof(lengths[0]); ++i) {
        transport.buffer_len=lengths[i]; calls=0; parent_result=0;
        CHECK(ws_poll_read(&transport, 1000)==1);
        CHECK(calls==0);
    }
    // After draining buffered bytes, readiness/timeouts/errors come from TCP.
    transport.buffer_len=0;
    for (int result=-1; result<=1; ++result) {
        calls=0; parent_result=result;
        CHECK(ws_poll_read(&transport, 731)==result);
        CHECK(calls==1 && last_timeout==731);
    }
    // Header parsing must not poll the empty socket a second time either.
    const char frame[]={ (char)0x81, 48 };
    char scratch[14];
    transport.buffer=frame; transport.buffer_len=sizeof(frame);
    calls=0; parent_result=0;
    CHECK(ws_read_header(&transport, scratch, sizeof(scratch), 1000)==48);
    CHECK(calls==0);
    CHECK(transport.frame_state.header_received && transport.frame_state.fin);
    CHECK(transport.frame_state.opcode==1 && transport.frame_state.bytes_remaining==48);
    CHECK(transport.buffer_len==0);
    // Empty buffer still honors socket timeout and does not claim a new header.
    CHECK(ws_read_header(&transport, scratch, sizeof(scratch), 123)==0);
    CHECK(calls==1 && last_timeout==123 && !transport.frame_state.header_received);
    puts("WebSocket buffered-read regression passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="voice-transport-test-") as directory:
    executable = str(pathlib.Path(directory) / "test")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-x", "c", "-", "-o", executable],
                   input=harness, text=True, check=True)
    raise SystemExit(subprocess.run([executable]).returncode)
