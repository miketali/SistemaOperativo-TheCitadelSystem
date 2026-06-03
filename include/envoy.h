/***********************************************
*
* @Proposit: Header del modulo de Envoys (procesos hijo)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Fase 4: El consell dels Emissaris
* Los Envoys son procesos independientes creados con fork()
* que ejecutan misiones (PLEDGE, TRADE) en paralelo.
* Comunicacion Maester <-> Envoy mediante pipes.
*
************************************************/

#ifndef _ENVOY_H
#define _ENVOY_H

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>

#include "config.h"
#include "inventory.h"
#include "network.h"
#include "protocol.h"
#include "filetransfer.h"
#include "utils.h"
#include "server.h"

/***********************************************
* Constantes
************************************************/
#define MAX_REALM_NAME 64
#define MAX_FILE_PATH 256
#define PIPE_READ 0
#define PIPE_WRITE 1
#define PLEDGE_TIMEOUT_SECONDS 120  //2 minutos de timeout para PLEDGE

/***********************************************
* Enumeraciones
************************************************/

//estado de un Envoy
typedef enum {
	ENVOY_FREE,              //disponible para misiones
	ENVOY_ON_MISSION,        //ejecutando una mision
	ENVOY_AWAITING_RESPONSE, //esperando respuesta de PLEDGE (bloqueado hasta 2 min)
	ENVOY_DEAD               //proceso terminado (error)
} EnvoyStatus;

//tipo de mision
typedef enum {
	MISSION_NONE,      //sin mision
	MISSION_PLEDGE,    //peticion de alianza
	MISSION_TRADE      //comercio
} MissionType;

//resultado de mision
typedef enum {
	MISSION_SUCCESS,
	MISSION_FAILED,
	MISSION_TIMEOUT
} MissionResultStatus;

/***********************************************
* Estructuras
************************************************/

//mensaje del Maester al Envoy (asignar mision)
typedef struct {
	MissionType type;
	char realm[MAX_REALM_NAME];
	char sigil_file[MAX_FILE_PATH];
	char target_ip[20];
	int target_port;
	char origin_ip[20];
	int origin_port;
	char realm_name[MAX_REALM_NAME];  //nombre de nuestro reino
} MissionMessage;

//mensaje del Envoy al Maester (resultado)
typedef struct {
	int envoy_id;
	MissionResultStatus status;
	MissionType type;
	char realm[MAX_REALM_NAME];
	char result_data[256];  //datos adicionales del resultado
} MissionResult;

//informacion de un Envoy individual
typedef struct {
	int id;                     //indice del Envoy (0, 1, 2...)
	pid_t pid;                  //PID del proceso hijo
	EnvoyStatus status;         //estado actual
	int pipe_to_envoy[2];       //pipe: Maester -> Envoy (Maester escribe, Envoy lee)
	int pipe_from_envoy[2];     //pipe: Envoy -> Maester (Envoy escribe, Maester lee)
	MissionType current_mission;
	char mission_target[MAX_REALM_NAME];
	time_t mission_start_time;  //timestamp de inicio de mision (para timeout)
} EnvoyInfo;

//pool de Envoys (gestionado por el Maester)
//struct amb nom per compatibilitat amb el forward declare de server.h
typedef struct EnvoyPool {
	EnvoyInfo *envoys;          //array de Envoys
	int num_envoys;             //numero total de Envoys
	int running;                //flag para indicar si el pool esta activo
} EnvoyPool;

//datos compartidos para el Envoy (copia de lo necesario)
typedef struct {
	char origin_ip[20];
	int origin_port;
	char realm_name[MAX_REALM_NAME];
	char folder_path[MAX_FILE_PATH];
} EnvoySharedConfig;

/***********************************************
* Funciones publicas - Gestion del Pool
************************************************/

//inicializar el pool de Envoys (crea los procesos hijo)
int initEnvoyPool(EnvoyPool *pool, int num_envoys, EnvoySharedConfig *config);

//destruir el pool de Envoys (termina procesos hijo)
void destroyEnvoyPool(EnvoyPool *pool);

//buscar un Envoy libre
int findFreeEnvoy(EnvoyPool *pool);

//asignar una mision a un Envoy
int assignMission(EnvoyPool *pool, int envoy_id, MissionMessage *mission);

//verificar si hay resultados de Envoys (no bloqueante)
int checkEnvoyResults(EnvoyPool *pool, MissionResult *result);

//obtener estado de un Envoy
EnvoyStatus getEnvoyStatus(EnvoyPool *pool, int envoy_id);

//obtener info de mision actual de un Envoy
void getEnvoyMissionInfo(EnvoyPool *pool, int envoy_id, MissionType *type, char *target);

//reservar un Envoy sin enviar mision (para TRADE)
int reserveEnvoy(EnvoyPool *pool, int envoy_id, MissionType type, const char *realm);

//liberar un Envoy reservado manualmente
int releaseEnvoy(EnvoyPool *pool, int envoy_id);

//marcar Envoy como esperando respuesta de PLEDGE
int setEnvoyAwaitingResponse(EnvoyPool *pool, int envoy_id, const char *realm);

//buscar Envoy esperando respuesta de un reino especifico
int findEnvoyAwaitingResponseFrom(EnvoyPool *pool, const char *realm);

//notificar resultado de alianza a un Envoy (acepta o rechaza)
int notifyAllianceResult(EnvoyPool *pool, int envoy_id, int accepted, const char *result_data);

//verificar y liberar Envoys con timeout expirado
//config puede ser NULL si no se quiere actualizar estado de alianza
int checkEnvoyTimeouts(EnvoyPool *pool, MaesterConfig *config, SharedData *shared);

/***********************************************
* Funciones publicas - Proceso Envoy (hijo)
************************************************/

//bucle principal del proceso Envoy
void envoyMainLoop(int envoy_id, int pipe_read_fd, int pipe_write_fd, EnvoySharedConfig *config);

//ejecutar mision PLEDGE
int executePledgeMission(MissionMessage *mission, MissionResult *result);

/***********************************************
* Funciones publicas - Monitor de Envoys (thread del Maester)
************************************************/

//arranca el thread monitor que sondea los pipes de los Envoys
int startEnvoyMonitor(SharedData *shared);

//detiene el thread monitor y espera su join
void stopEnvoyMonitor(void);

//procesa todos los resultados pendientes en los pipes (no bloqueante)
//tambien gestiona los timeouts de Envoys AWAITING_RESPONSE
void checkAndProcessEnvoyResults(SharedData *shared);

//muestra por pantalla el estado de todos los Envoys (comando ENVOY STATUS)
void handleEnvoyStatus(SharedData *shared);

/***********************************************
* Funciones publicas - Cleanup del proceso hijo Envoy
************************************************/

//guarda referencias del padre para que el hijo Envoy pueda liberarlas antes de exit
void setEnvoyParentRefs(MaesterConfig *parent_config, Inventory *parent_inventory,
                        EnvoyPool *parent_pool);

//libera la memoria heredada del padre y sale del proceso hijo con exit(code)
void envoyChildCleanupAndExit(int code);

#endif
