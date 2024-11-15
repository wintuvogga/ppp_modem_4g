#include <lwip/apps/mqtt.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include "cmsis_os.h"
#include "connectionmanager.h"
#include <stdio.h>
#include <string.h>
#include "main.h"

#define MQTT_HOST_IP    "43.228.126.5"
#define MQTT_HOST_PORT   1883
#define MQTT_USERNAME   ""
#define MQTT_PASSWORD   ""

static mqtt_client_t *mqttclient = NULL;
struct mqtt_connect_client_info_t mqttclientInfo;
bool mqttConnected = false;
char clientId[17] = {0};
ip_addr_t broker_ipaddr;
int offlineCounter = MAX_PINGFAIL_OFFLINE;

char random_char(int index) 
{
	char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	return charset[index];
}

void generateRandomCharacters(char *str, uint16_t len)
{
	int i, index;
	for (i = 0; i < len - 1; i++) 
    {
		index = rand() % 62;
		str[i] = random_char(index);
	}
	str[i] = '\0';
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        printf("MQTT: mqtt_connection_cb: Successfully connected\r\n");
        mqttConnected = true;
    }
    else
    {
        printf("MQTT: mqtt_connection_cb: Disconnected, reason: %d\n", status);
        mqttConnected = false;
    }
}

void ConnectionManager()
{
    err_t err;
    uint8_t oneTimeDone = 0;
    uint16_t publishNowCounter = 10;
    mqttclient = mqtt_client_new();
    if(mqttclient == NULL)
    {
        printf("Error: could not create MQTT client\r\n");
    }

    generateRandomCharacters(clientId, 16);
    printf("MQTT Client ID: %s\r\n", clientId);

    memset(&mqttclientInfo, 0, sizeof(struct mqtt_connect_client_info_t));
    mqttclientInfo.keep_alive = 60;
    mqttclientInfo.client_id = clientId;
    mqttclientInfo.client_user = MQTT_USERNAME;
    mqttclientInfo.client_pass = MQTT_PASSWORD;

    broker_ipaddr.addr = ipaddr_addr(MQTT_HOST_IP);

    osThreadNew(pingThread, NULL, NULL);    

    while(1)
    {
        if (oneTimeDone == 0)
        {
            if (isOnline())
            {
                printf("MQTT: connecting to %s\r\n", ipaddr_ntoa(((const ip_addr_t *)&broker_ipaddr)));
                err = mqtt_client_connect(mqttclient, &broker_ipaddr, MQTT_HOST_PORT, mqtt_connection_cb, 0, &mqttclientInfo);
                if (err != ERR_OK)
                {
                    printf("MQTT: mqtt_client_connect failed. err: %d\r\n", err);
                }
                oneTimeDone = 1;
            }

            vTaskDelay(1000);
            continue;
        }

        osDelay(1000);
    }
}

void pingThread()
{
    printf("ping thread started\r\n");
    while(1)
    {
        if(ping("8.8.8.8", 53))
        {
            offlineCounter = 0;
        }
        else
        {
            offlineCounter++;
            printf("PING failed, : %d\r\n", offlineCounter);
            if (offlineCounter > MAX_PINGFAIL_OFFLINE)
            {
                offlineCounter = MAX_PINGFAIL_OFFLINE;
            }
        }
        osDelay(3000);
    }
}

bool ping(char *address, int port)
{
    int server_fd;
    struct sockaddr_in server_addr;
    bool retVal = false;
    fd_set fdset;
    struct timeval tv;

    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_addr.s_addr = inet_addr(address);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    FD_ZERO(&fdset);
    FD_SET(server_fd, &fdset);
    tv.tv_sec = 5; /* 5 second timeout */
    tv.tv_usec = 0;

    if (select(server_fd + 1, NULL, &fdset, NULL, &tv) == 1)
    {
        int so_error;
        socklen_t len = sizeof so_error;
        getsockopt(server_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error == 0)
        {
            retVal = true;
        }
    }

    close(server_fd);
    return retVal;
}

bool isOnline()
{
    if (offlineCounter < MAX_PINGFAIL_OFFLINE)
    {
        return true;
    }
    return false;
}