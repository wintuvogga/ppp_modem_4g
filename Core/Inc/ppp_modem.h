#ifndef MODEM_PPP_H_
#define MODEM_PPP_H_

#include <stdint.h>

typedef void (* successCallBack_t)();
typedef struct
{
    char cmd[40];
    char *cmdGooDResponse;
    uint16_t timeout;
    uint16_t waitBeforeMs;
    uint8_t skip;
    uint8_t exit;
    successCallBack_t successCallBack;
} ModemCmd;

void pppModemInit();

#endif