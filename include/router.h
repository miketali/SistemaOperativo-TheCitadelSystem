/***********************************************
*
* @Proposit: Header del modulo de routing
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _ROUTER_H
#define _ROUTER_H

#include "protocol.h"
#include "config.h"

//forward declare SharedData per quan router.h s'inclou abans que server.h
//(en altre cas, server.h ja l'haura definida)
typedef struct SharedData SharedData;

#include "server.h"  //per accedir als camps de SharedData en router.c
#include "frame_handlers.h"  //per processFrame() que despatxa als handlers
#include "utils.h"
#include "network.h"
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
/***********************************************
* Funciones publicas de routing
************************************************/

//verifica si una trama es para este reino
int isForMe(Frame *frame, MaesterConfig *config);

//procesa una trama recibida para este reino
void processFrame(Frame *frame, SharedData *shared, int fd_client);

//busca una ruta en la tabla de rutas
Route* findRoute(const char *realm_name, MaesterConfig *config);

//busca el nombre del reino dado su IP:Port
const char* findRealmNameByOrigin(const char *origin, MaesterConfig *config);

//envia una trama de error al origen
void sendErrorToOrigin(const char *origin, uint8_t error_type, const char *error_msg, MaesterConfig *config);

//reenvia una trama a otro reino (routing) - actua como relay TCP
//retorna 1 si fd_client ya fue cerrado por el relay, 0 si no
int forwardFrame(Frame *frame, MaesterConfig *config, int fd_client);

//envia una trama NACK a un cliente
int sendNACK(int fd_client, const char *realm_name);

#endif
