#include "ppp_modem.h"
#include "main.h"
#include "cmsis_os.h"
#include "lwip.h"
#include "netif/ppp/ppp.h"
#include "netif/ppp/pppos.h"
#include "lwip/init.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define AT_DEBUG 1
#define MODEM_BUF_SIZE (1024)
#define MODME_APN "e-ideas"
#define CMD_SUCCESS 1
#define CMD_FAILED -1
#define CMD_TIMEDOUT 0
#define PPP_DATA_BUF_SIZE 256
#define UART_DMA_BUF_SIZE 1024
#define ARRAY_LEN(x)            (sizeof(x) / sizeof((x)[0]))

extern UART_HandleTypeDef huart6;
static ppp_pcb *ppp;
static struct netif pppos_netif;
bool GSM_triggerModemReconnect = false;

uint8_t UartDmaBuffer[UART_DMA_BUF_SIZE] = {0};

osMessageQueueId_t uartRxDmaQueueHandle;
osThreadId_t pppThreadHandle;
const osThreadAttr_t pppThread_attributes = {
  .name = "pppThread",
  .stack_size = 4096,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t pppUartRxThreadHandle;
const osThreadAttr_t pppUartRxThread_attributes = {
  .name = "pppUartRxThread",
  .stack_size = 4096,
  .priority = (osPriority_t) osPriorityNormal,
};

void reg_Ok()
{
    printf("Registation OK\r\n");
    // GSM_network_OK = true;
    // Modem_GetSignalQuality();
}

void highBaudSet()
{
    // uart_flush_input(UART_NUM_2);
    // uart_flush(UART_NUM_2);
    // uart_set_baudrate(UART_NUM_2, 460800);
    printf("High baud set\r\n");
}

void modemDetected()
{
    printf("Modem detected\r\n");
    // Modem_GetIMEI();
}

void sim_OK()
{
    printf("Sim OK\r\n");
}

static ModemCmd modemCmdRFOn =
    {
        .cmd = "AT+CFUN=1\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 15000,
        .waitBeforeMs = 1000,
        .skip = 0,
        .exit = 0,
        .successCallBack = NULL};

static ModemCmd modemCmdAlive =
    {
        .cmd = "AT\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 300,
        .waitBeforeMs = 0,
        .skip = 0,
        .exit = 0,
        .successCallBack = NULL};

static ModemCmd modemATZ =
    {
        .cmd = "ATZ\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 300,
        .waitBeforeMs = 0,
        .skip = 0,
        .exit = 0,
        .successCallBack = NULL};

static ModemCmd modemCmdEchoOff =
    {
        .cmd = "ATE0\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 300,
        .waitBeforeMs = 0,
        .skip = 0,
        .exit = 0,
        .successCallBack = modemDetected};

static ModemCmd modemCmdSetBaud =
    {
        .cmd = "AT+IPR=460800\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 1000,
        .waitBeforeMs = 100,
        .skip = 0,
        .exit = 0,
        .successCallBack = highBaudSet};

static ModemCmd modemCmdSimPin =
    {
        .cmd = "AT+CPIN?\r\n",
        .cmdGooDResponse = "READY", // SMS Ready
        .timeout = 3000,
        .waitBeforeMs = 0,
        .skip = 0,
        .exit = 0,
        .successCallBack = sim_OK};

static ModemCmd modemCmdCheckReg =
    {
        .cmd = "AT+CREG?\r\n",
        .cmdGooDResponse = "CREG: 0,1",
        .timeout = 1000,
        .waitBeforeMs = 100,
        .skip = 0,
        .exit = 0,
        .successCallBack = reg_Ok};

static ModemCmd modemCmdConnect =
    {
        .cmd = "ATD*99#\r\n",
        .cmdGooDResponse = "CONNECT",
        .timeout = 30000,
        .waitBeforeMs = 3000,
        .skip = 0,
        .exit = 0,
        .successCallBack = NULL};

static ModemCmd modemCmdAPN =
    {
        .cmd = "AT+CGDCONT=1,\"IP\",\"IOT.COM\"\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 3000,
        .waitBeforeMs = 0,
        .skip = 0,
        .exit = 0,
        .successCallBack = NULL};

static ModemCmd modemCmdOpDetails =
    {
        .cmd = "AT+COPS?\r\n",
        .cmdGooDResponse = "OK",
        .timeout = 3000,
        .waitBeforeMs = 0,
        .skip = 0,
        .exit = 0,
        .successCallBack = NULL};

static ModemCmd *modemInitCommands[] = {
    &modemCmdAlive,
    &modemATZ,
    &modemCmdEchoOff,
    &modemCmdRFOn,
    &modemCmdAPN,
    &modemCmdSimPin,
    &modemCmdCheckReg,
    &modemCmdOpDetails,
    &modemCmdConnect,
};
#define modemInitCommandsSize (sizeof(modemInitCommands) / sizeof(ModemCmd *))

void Modem_enableAllCommands()
{
    for (unsigned int i = 0; i < modemInitCommandsSize; i++)
    {
        modemInitCommands[i]->skip = 0;
    }
}

void Modem_Write(char *data, uint16_t length)
{
    HAL_UART_Transmit(&huart6, (uint8_t *)data, length, osWaitForever);
}

bool Modem_GetByte(uint8_t *byte)
{
    if(osMessageQueueGet(uartRxDmaQueueHandle, byte, NULL, 0) == osOK)
    {
        return true;
    }
    return false;
}

int Modem_Read(uint8_t *dataToRead, int noOfBytes, int timeout)
{
    int ptr = 0;
    uint8_t byte = 0;
    int elapsedTime = 0;
    while(1)
    {
        if(Modem_GetByte(&byte))
        {
            dataToRead[ptr] = byte;
            ptr++;
            if(ptr >= noOfBytes)
            {
                break;
            }
        }
        
        osDelay(1);
        elapsedTime++;
        if(elapsedTime >= timeout)
        {
            break;
        }
    }
    return ptr;
}

char recd[MODEM_BUF_SIZE] = {0};
int Modem_CommandGetResult(char *cmd, char *responsePass, char *responseFail, uint32_t timeout)
{
#if AT_DEBUG
    printf("CMD: %s\r\n", cmd);
#endif
    Modem_Write(cmd, strlen(cmd));

    int retVal = 0;
    uint16_t count = 0;
    uint8_t byterx;
    memset(recd, 0, MODEM_BUF_SIZE);
    while (timeout--)
    {
        while (Modem_GetByte(&byterx) == true)
        {
            recd[count] = byterx;
            count++;

            if (responsePass != NULL)
            {
                if (strstr(recd, responsePass) != NULL)
                {
                    retVal = 1;
                    break;
                }
            }

            if (responseFail != NULL)
            {
                if (strstr(recd, responseFail) != NULL)
                {
                    retVal = -1;
                    break;
                }
            }

            if (count >= MODEM_BUF_SIZE)
            {
                break;
            }
        }
        if (count >= MODEM_BUF_SIZE)
        {
            break;
        }
        if (retVal)
        {
            break;
        }

        osDelay(1);
    }
#if AT_DEBUG
    printf("RESP: %s\r\n", recd);
#endif

    return retVal;
}

static u32_t ppp_output_cb(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(ctx);
    HAL_UART_Transmit(&huart6, (uint8_t *)data, len, osWaitForever);
    return len;
}

static void ppp_link_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    LWIP_UNUSED_ARG(ctx);
    struct netif *pppif = ppp_netif(pcb);
    switch (err_code)
    {
    case PPPERR_NONE:
        printf("ppp status: Connected\r\n");
        printf("    ipaddr    = %s\r\n", ipaddr_ntoa(&pppif->ip_addr));
        printf("    gateway   = %s\r\n", ipaddr_ntoa(&pppif->gw));
        printf("    netmask   = %s\r\n", ipaddr_ntoa(&pppif->netmask));
        break;
    case PPPERR_PARAM:
        printf("ppp status: Invalid parameter\r\n");
        break;
    case PPPERR_OPEN:
        printf("ppp status: Unable to open PPP session\r\n");
        break;
    case PPPERR_DEVICE:
        printf("ppp status: Invalid I/O device for PPP\r\n");
        break;
    case PPPERR_ALLOC:
        printf("ppp status: Unable to allocate resources\r\n");
        break;
    case PPPERR_USER:
        printf("ppp status: User interrupt\r\n");
        break;
    case PPPERR_CONNECT:
        printf("ppp status: Connection lost\r\n");
        break;
    case PPPERR_AUTHFAIL:
        printf("ppp status: Failed authentication challenge\r\n");
        break;
    case PPPERR_PROTOCOL:
        printf("ppp status: Failed to meet protocol\r\n");
        break;
    case PPPERR_PEERDEAD:
        printf("ppp status: Connection timeout\r\n");
        break;
    case PPPERR_IDLETIMEOUT:
        printf("ppp status: Idle Timeout\r\n");
        break;
    case PPPERR_CONNECTTIME:
        printf("ppp status: Max connect time reached\r\n");
        break;
    case PPPERR_LOOPBACK:
        printf("ppp status: Loopback detected\r\n");
        break;
    
    default:
        printf("ppp status: unknown: %d\r\n", err_code);
        break;
    }
}

void Modem_Reset()
{
    printf("Resetting modem\r\n");
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);
    osDelay(1500);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);
}

void WaitForModemAlive()
{
    int resetCount = 0;
    printf("Wait for reset\r\n");
    Modem_Reset();
    osDelay(4000);
    while(1) 
    {
        resetCount++;
        if(resetCount > 100)
        {
            resetCount = 0;
        }
        if(Modem_CommandGetResult("AT\r\n", "OK", NULL, 100) == CMD_SUCCESS)
        {
            printf("Modem found\r\n");
            return;
        }
        osDelay(100);
    }
}

bool Modem_InitializeAndDialUp()
{
    printf("PPP Modem initializing, APN: %s\r\n", MODME_APN);
    sprintf(modemCmdAPN.cmd, "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", MODME_APN);
    Modem_enableAllCommands();

    WaitForModemAlive();

    Modem_CommandGetResult("ATE0\r\n", NULL, NULL, 100);

    int retryCount = 0;
    unsigned int gsmCmdIter = 0;

    while (gsmCmdIter < modemInitCommandsSize)
    {
        if (modemInitCommands[gsmCmdIter]->skip)
        {
            gsmCmdIter++;
            continue;
        }
    
        int res = Modem_CommandGetResult(modemInitCommands[gsmCmdIter]->cmd,
                                         modemInitCommands[gsmCmdIter]->cmdGooDResponse,
                                         NULL,
                                         modemInitCommands[gsmCmdIter]->timeout);
        if (res != CMD_SUCCESS)
        {
            retryCount++;
            if (retryCount > 5)
            {
                printf("Modem connect timeout, Re initiating modem connect\r\n");
                Modem_enableAllCommands();
                WaitForModemAlive();
                retryCount = 0;
            }
            gsmCmdIter = 0;
            continue;
        }
        else if (res == CMD_SUCCESS)
        {
            if (modemInitCommands[gsmCmdIter]->successCallBack != NULL)
            {
                modemInitCommands[gsmCmdIter]->successCallBack();
            }
        }

        if (modemInitCommands[gsmCmdIter]->waitBeforeMs > 0)
        {
            osDelay(modemInitCommands[gsmCmdIter]->waitBeforeMs);
        }

        modemInitCommands[gsmCmdIter]->skip = 1;

        // Next command
        gsmCmdIter++;
        retryCount = 0;
    }

    printf("Dialup success\r\n");
    netif_set_default(&pppos_netif);
    return false;
}

uint8_t pppDataBuf[PPP_DATA_BUF_SIZE] = {0};
void pppComThread()
{
    int dataRx;
    GSM_triggerModemReconnect = true;
    printf("PPP com thread started\r\n");

    while(1)
    {
        if(GSM_triggerModemReconnect)
        {
            GSM_triggerModemReconnect = false;
            ppp_close(ppp, 1);
            Modem_InitializeAndDialUp();
            ppp_connect(ppp, 0);
        }

        dataRx = Modem_Read(pppDataBuf, PPP_DATA_BUF_SIZE, 30);
        if(dataRx > 0)
        {
            pppos_input_tcpip(ppp, (u8_t *)pppDataBuf, dataRx);
        }
        osDelay(10);
    }
}

void pppUartRxThread()
{
    uint16_t currentBufferPtr = 0;
    uint16_t readfrom = 0;
    uint8_t tmp = 0;

    while(1)
    {
        currentBufferPtr = UART_DMA_BUF_SIZE - huart6.hdmarx->Instance->NDTR;
        if(readfrom > currentBufferPtr) 
        {
            for(int i=readfrom; i < UART_DMA_BUF_SIZE; i++)
            {
                tmp = UartDmaBuffer[i];
                osMessageQueuePut(uartRxDmaQueueHandle, &tmp, 0, 0);
            }
            for(int i=0; i<currentBufferPtr; i++)
            {
                tmp = UartDmaBuffer[i];
                osMessageQueuePut(uartRxDmaQueueHandle, &tmp, 0, 0);
            }
        }
        else
        {
            for(int i=readfrom; i<currentBufferPtr; i++)
            {
                tmp = UartDmaBuffer[i];
                osMessageQueuePut(uartRxDmaQueueHandle, &tmp, 0, 0);
            }
        }
        readfrom = currentBufferPtr;
        osDelay(10);
    }
}



void pppModemInit()
{
    lwip_init();
    printf("lwip init done\r\n");
    ppp = pppos_create(&pppos_netif, ppp_output_cb, ppp_link_status_cb, NULL);
    if (!ppp)
    {
        printf("Error: Could not create PPP control interface\r\n");
        return;
    }

    uartRxDmaQueueHandle = osMessageQueueNew(UART_DMA_BUF_SIZE, sizeof(uint8_t), NULL);
    HAL_UART_Receive_DMA(&huart6, UartDmaBuffer, UART_DMA_BUF_SIZE);

    pppUartRxThreadHandle = osThreadNew(pppUartRxThread, NULL, &pppUartRxThread_attributes);
    pppThreadHandle = osThreadNew(pppComThread, NULL, &pppThread_attributes);
}