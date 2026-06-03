/***********************************************
*
* @Proposit: Header del dispatcher de comandos del terminal interactivo.
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest modul nomes conte:
*   - executeCommand: parser i dispatcher
*   - Els handlers dels comandos locals trivials (LIST REALMS,
*     LIST PRODUCTS, PLEDGE STATUS, EXIT)
*
* Els comandos complexos viuen en moduls separats: pledge.h, trade.h,
* envoy.h (per handleEnvoyStatus i el monitor).
*
************************************************/

#ifndef _COMMANDS_H
#define _COMMANDS_H

#include "config.h"
#include "inventory.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pledge.h"
#include "trade.h"
#include "envoy.h"
#include "network.h"
#include "protocol.h"
#include "utils.h"
#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 10

//handlers locals senzills (no necessiten ni xarxa ni Envoys)
void handleListRealms(const MaesterConfig *config);
void handleListProducts(const Inventory *inventory);
void handlePledgeStatus(const MaesterConfig *config);
void handleExit(const MaesterConfig *config);

//parser i dispatcher principal del terminal
int executeCommand(const char *command, const MaesterConfig *config,
                   const Inventory *inventory, SharedData *shared);

#endif
