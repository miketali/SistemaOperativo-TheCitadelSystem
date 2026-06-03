/***********************************************
*
* @Proposit: Header del modulo de servidor (threads)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Fase 4: Añadido soporte para EnvoyPool
*
************************************************/

#ifndef _SERVER_H
#define _SERVER_H

#include <pthread.h>
#include "config.h"
#include "inventory.h"

//forward declarations per evitar la dependencia circular server.h <-> envoy.h
//SharedData nomes usa EnvoyPool com a punter, aixi un forward declare basta
typedef struct EnvoyPool EnvoyPool;

#include "router.h"
#include "network.h"
#include "utils.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
/***********************************************
*
* @Finalitat: Cola de notificaciones pendientes
* Se usa para almacenar mensajes cuando estamos en trade mode
*
************************************************/
#define MAX_NOTIFICATIONS 20
#define MAX_NOTIFICATION_LEN 256

typedef struct {
	char messages[MAX_NOTIFICATIONS][MAX_NOTIFICATION_LEN];
	int count;
	pthread_mutex_t lock;
} NotificationQueue;

/***********************************************
*
* @Finalitat: Estructura de datos compartidos con mutex
*
* Fase 4: Añadido envoy_pool para gestionar procesos Envoy
*
************************************************/
typedef struct SharedData {
	MaesterConfig *config;
	Inventory *inventory;
	pthread_mutex_t config_lock;
	pthread_mutex_t inventory_lock;
	//Fase 4: Pool de Envoys
	EnvoyPool *envoy_pool;
	pthread_mutex_t envoy_lock;
	//Fase 4: Sistema de notificaciones
	volatile int in_trade_mode;          //1 si estamos en modo trade
	NotificationQueue notifications;     //cola de notificaciones pendientes
} SharedData;

/***********************************************
*
* @Finalitat: Estructura para pasar datos al thread servidor
*
************************************************/
typedef struct {
	char *ip;
	int port;
	SharedData *shared;
} ServerThreadData;

/***********************************************
*
* @Finalitat: Estructura para pasar datos al thread de cliente
*
************************************************/
typedef struct {
	int fd_client;
	SharedData *shared;
} ClientThreadData;

/***********************************************
* Funciones publicas
************************************************/

//inicializar datos compartidos con mutex
void initSharedData(SharedData *shared, MaesterConfig *config, Inventory *inventory);

//destruir mutex de datos compartidos
void cleanupSharedData(SharedData *shared);

//iniciar servidor (crea thread servidor)
int startServer(SharedData *shared, char *ip, int port);

/***********************************************
* Funciones de notificaciones
************************************************/

//encolar notificacion (se muestra despues si estamos en trade mode)
//retorna 1 si se encolo, 0 si se mostro directamente
int queueOrShowNotification(SharedData *shared, const char *format, ...);

//mostrar todas las notificaciones pendientes
void flushNotifications(SharedData *shared);

//entrar/salir de trade mode
void enterTradeMode(SharedData *shared);
void exitTradeMode(SharedData *shared);

#endif
