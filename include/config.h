/***********************************************
*
* @Proposit: Header de configuracion del Maester
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"

//constantes
#define MAX_NAME 100
#define MAX_IP 20
#define MAX_PATH 256
#define MAX_ROUTES 50

//estados de alianza
#define ALLIANCE_NONE 0
#define ALLIANCE_PENDING 1
#define ALLIANCE_ALLIED 2
#define ALLIANCE_FAILED 3

//tipos de datos
typedef struct {
	char *name;
	char *ip;
	int port;
	int alliance_status;
	char *pending_origin;  // Almacena ORIGIN durante peticion de alianza pendiente
	char *ally_ip;         // IP directa del aliado (obtenida durante PLEDGE)
	int ally_port;         // Puerto directo del aliado
} Route;

typedef struct {
	char *realm_name;
	char *folder_path;
	int num_envoys;
	char *ip;
	int port;
	Route *routes;
	int num_routes;
	int routes_capacity;
	char *stock_file;  //ruta al archivo de inventario (stock.db)
} MaesterConfig;

//funciones de configuracion
int readConfig(const char *filename, MaesterConfig *config);
void freeConfig(MaesterConfig *config);

//funciones de gestion de aliances
void setAllianceStatus(Route *route, int status);
Route* findRouteByName(const char *realm_name, MaesterConfig *config);
Route* findDefaultRoute(MaesterConfig *config);
Route* addDynamicRoute(const char *realm_name, const char *ip, int port, MaesterConfig *config);
void setAllyDirectConnection(Route *route, const char *origin);

//Comprueba si la ruta tiene IP wildcard ("*.*.*.*"): el regne existeix
//pero la ruta directa no es coneguda (PDF pagina 9, Figura 3)
int isWildcardRoute(const Route *route);

#endif
