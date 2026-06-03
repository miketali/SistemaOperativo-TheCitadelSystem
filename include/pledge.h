/***********************************************
*
* @Proposit: Header del modulo de peticiones de alianza (PLEDGE)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Gestiona los comandos PLEDGE <regne> <sigil> y PLEDGE RESPOND <regne>
* ACCEPT/REJECT. La emision de PLEDGE delega la mision a un Envoy del pool;
* la respuesta se envia desde el thread principal porque es un acto puntual.
*
************************************************/

#ifndef _PLEDGE_H
#define _PLEDGE_H

#include "config.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "envoy.h"
#include "network.h"
#include "protocol.h"
#include "utils.h"
//inicia una peticion de alianza enviando el sigil a traves de un Envoy
void handlePledge(const char *realm, const char *sigil_file,
                  MaesterConfig *config, SharedData *shared);

//responde a una peticion de alianza pendiente (ACCEPT o REJECT)
void handlePledgeRespond(const char *realm, const char *response,
                         MaesterConfig *config);

#endif
