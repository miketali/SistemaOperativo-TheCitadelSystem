/***********************************************
*
* @Proposit: Handlers individuals per tipus de trama entrant.
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Cada funcio gestiona un unic TYPE del protocol descrit a l'Annex II.
* El dispatcher processFrame() de router.c les crida via switch.
*
************************************************/

#ifndef _FRAME_HANDLERS_H
#define _FRAME_HANDLERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "protocol.h"
#include "config.h"
#include "inventory.h"
#include "network.h"
#include "utils.h"
#include "filetransfer.h"
#include "envoy.h"
#include "server.h"
#include "router.h"
//0x01 - peticio d'aliança: rep el header, ACK, sigil, MD5
void handleAllianceRequest(Frame *frame, SharedData *shared, int fd_client);

//0x11/0x12 - peticio de llista de productes: respon amb el seu inventari
void handleProductListRequest(Frame *frame, SharedData *shared, int fd_client);

//0x14 - header de comanda de comerç: ACK, rep dades, verifica MD5
void handleTradeRequestHeader(Frame *frame, SharedData *shared, int fd_client);

//0x15 - dades de la comanda: processa, actualitza stock, respon
void handleTradeOrder(Frame *frame, SharedData *shared, int fd_client);

//0x27 - notificacio de desconnexio d'un aliat
void handleDisconnect(Frame *frame, SharedData *shared);

//0x03 - resposta a una aliança nostra: ACCEPT/REJECT/TIMEOUT
void handleAllianceResponse(Frame *frame, SharedData *shared, int fd_client);

#endif
