#pragma once
#include <stdint.h>
namespace pocket {
struct TransferSnapshot {
    bool enabled=false, running=false, busy=false, stopping=false;
    unsigned progress=0;
    char code[9]{}, message[80]{};
};
// Main/UI task API. HTTP file jobs are executed by transfer_process().
bool transfer_enable(bool enabled);
bool transfer_enabled();
bool transfer_busy();
TransferSnapshot transfer_snapshot();
void transfer_process();
}
