/***********************************************
*
* @Proposit: Header de manejadores de señales y eventos
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _HANDLERS_H
#define _HANDLERS_H

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "utils.h"
#include "server.h"
#include "envoy.h"

#include "network.h"
#include "protocol.h"
#include "config.h"
#include "inventory.h"
#include <sys/wait.h>
//manejador de señal SIGINT (CTRL+C)
void signalHandler(int sig);

//manejador de señal SIGCHLD (caida de Envoys)
void sigchldHandler(int sig);

//configurar manejadores de señales
void setupSignalHandlers(void);

//registrar datos para cleanup en SIGINT
void setCleanupData(SharedData *shared, MaesterConfig *config, Inventory *inventory);

//registrar pool de Envoys para gestionar SIGCHLD
void setEnvoyPool(EnvoyPool *pool);

//indicar que estamos en shutdown (no reportar Envoys como crashed)
void setShutdownInProgress(void);

#endif
