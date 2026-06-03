/***********************************************
*
* @Proposit: Header del modulo de comerç entre regnes
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Encapsula el flujo de comercio:
*   - LIST PRODUCTS <regne>: pide la llista de productos a un aliado y la
*     cachea para usarla luego en START TRADE.
*   - START TRADE <regne>: submenu interactivo (add/remove/list/send/cancel)
*     que construye una llista de la compra y la envia al aliado.
*
* Tambien gestiona internamente la cache de productes remots, de manera que
* commands.c no necesita conocer su estructura.
*
************************************************/

#ifndef _TRADE_H
#define _TRADE_H

#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "products_cache.h"
#include "trade_manifest.h"
#include "envoy.h"
#include "network.h"
#include "protocol.h"
#include "filetransfer.h"
#include "utils.h"
//pide la lista de productos a un regne aliat usando un Envoy reservado
void handleListProductsRemote(const char *realm, SharedData *shared);

//entra en el submenu interactivo de comerç con un regne aliat
void handleStartTrade(const char *realm, SharedData *shared);

#endif
