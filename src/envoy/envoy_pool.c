/***********************************************
*
* @Proposit: Gestio del pool de Envoys des del Maester (proces pare).
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest fitxer conte les operacions que el Maester fa sobre el conjunt
* d'Envoys: creacio amb fork() i pipes, destruccio neta amb SIGTERM +
* waitpid, transicions d'estat (FREE -> ON_MISSION -> AWAITING_RESPONSE
* -> FREE), assignacio i lectura no bloqueant de pipes, i gestio dels
* timeouts de 2 minuts de les peticions d'aliança.
*
************************************************/
#include "envoy.h"

//referencias del padre guardadas para cleanup del hijo Envoy antes de exit
static MaesterConfig *g_envoy_parent_config = NULL;
static Inventory *g_envoy_parent_inventory = NULL;
static EnvoyPool *g_envoy_parent_pool = NULL;

/***********************************************
* @Finalitat: Guarda referencias del padre para que el hijo Envoy
*             pueda liberarlas antes de exit() y evitar still reachable
* @Parametres: in: parent_config, parent_inventory, parent_pool
* @Retorn: ----
************************************************/
void setEnvoyParentRefs(MaesterConfig *parent_config, Inventory *parent_inventory,
                        EnvoyPool *parent_pool) {
	g_envoy_parent_config = parent_config;
	g_envoy_parent_inventory = parent_inventory;
	g_envoy_parent_pool = parent_pool;
}

/***********************************************
* @Finalitat: Libera la memoria heredada del padre por el fork
*             (config, inventory, envoy_pool) y sale del proceso hijo.
*             Llamado desde runEnvoyChild justo antes de salir.
* @Parametres: in: code = exit code
* @Retorn: ---- (no retorna)
************************************************/
void envoyChildCleanupAndExit(int code) {
	if (g_envoy_parent_inventory) freeInventory(g_envoy_parent_inventory);
	if (g_envoy_parent_config) freeConfig(g_envoy_parent_config);
	if (g_envoy_parent_pool && g_envoy_parent_pool->envoys) {
		free(g_envoy_parent_pool->envoys);
		g_envoy_parent_pool->envoys = NULL;
	}
	exit(code);
}


/***********************************************
*
* @Finalitat: Allibera els recursos dels Envoys ja inicialitzats (fins index-1)
*             quan algun pas de la inicialitzacio falla
* @Parametres: in/out: pool, in: up_to = nombre d'Envoys a netejar
* @Retorn: ----
*
************************************************/
static void cleanupPartialPool(EnvoyPool *pool, int up_to) {
	int k;

	for (k = 0; k < up_to; k++) {
		kill(pool->envoys[k].pid, SIGTERM);
		close(pool->envoys[k].pipe_to_envoy[PIPE_READ]);
		close(pool->envoys[k].pipe_to_envoy[PIPE_WRITE]);
		close(pool->envoys[k].pipe_from_envoy[PIPE_READ]);
		close(pool->envoys[k].pipe_from_envoy[PIPE_WRITE]);
	}
	free(pool->envoys);
}

/***********************************************
*
* @Finalitat: Crea els dos pipes (cap a/des de l'Envoy) per un slot
* @Parametres: in/out: envoy = entrada del pool a inicialitzar
* @Retorn: 0 ok, -1 error (cap pipe queda obert si retorna -1)
*
************************************************/
static int createEnvoyPipes(EnvoyInfo *envoy) {
	if (pipe(envoy->pipe_to_envoy) < 0) {
		return -1;
	}
	if (pipe(envoy->pipe_from_envoy) < 0) {
		close(envoy->pipe_to_envoy[PIPE_READ]);
		close(envoy->pipe_to_envoy[PIPE_WRITE]);
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Executa el bucle principal de l'Envoy fill, tancant tots
*             els descriptors heredats menys els pipes propis
* @Parametres: in: envoy_id, in: read_fd, in: write_fd, in: config
* @Retorn: ---- (no retorna; fa exit(0) al final)
*
************************************************/
static void runEnvoyChild(int envoy_id, int read_fd, int write_fd, EnvoySharedConfig *config) {
	int fd;

	for (fd = 3; fd < 256; fd++) {
		if (fd != read_fd && fd != write_fd) {
			close(fd);
		}
	}
	envoyMainLoop(envoy_id, read_fd, write_fd, config);
	envoyChildCleanupAndExit(0);
}

/***********************************************
*
* @Finalitat: Inicialitza una entrada concreta del pool: pipes + fork().
*             Si tot va be, el proces pare retorna 0 amb el child ja en marxa
* @Parametres: in/out: envoy, in: envoy_id, in: config
* @Retorn: 0 ok, -1 error
*
************************************************/
static int spawnSingleEnvoy(EnvoyInfo *envoy, int envoy_id, EnvoySharedConfig *config) {
	pid_t pid;

	envoy->id = envoy_id;
	envoy->status = ENVOY_FREE;
	envoy->current_mission = MISSION_NONE;
	envoy->mission_target[0] = '\0';

	if (createEnvoyPipes(envoy) < 0) {
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		close(envoy->pipe_to_envoy[PIPE_READ]);
		close(envoy->pipe_to_envoy[PIPE_WRITE]);
		close(envoy->pipe_from_envoy[PIPE_READ]);
		close(envoy->pipe_from_envoy[PIPE_WRITE]);
		return -1;
	}

	if (pid == 0) {
		runEnvoyChild(envoy_id, envoy->pipe_to_envoy[PIPE_READ],
		              envoy->pipe_from_envoy[PIPE_WRITE], config);
	}

	envoy->pid = pid;
	close(envoy->pipe_to_envoy[PIPE_READ]);
	close(envoy->pipe_from_envoy[PIPE_WRITE]);
	return 0;
}

/***********************************************
*
* @Finalitat: Inicialitza el pool: fork() + pipes per a cada Envoy
* @Parametres: out: pool = pool a inicialitzar
*              in: num_envoys = nombre de processos hijo a crear
*              in: config = config compartit (es passa al hijo)
* @Retorn: 0 ok, -1 error
*
************************************************/
int initEnvoyPool(EnvoyPool *pool, int num_envoys, EnvoySharedConfig *config) {
	int i;

	if (num_envoys <= 0) {
		return -1;
	}

	pool->envoys = malloc(sizeof(EnvoyInfo) * (size_t)num_envoys);
	if (!pool->envoys) {
		return -1;
	}

	pool->num_envoys = num_envoys;
	pool->running = 1;

	for (i = 0; i < num_envoys; i++) {
		if (spawnSingleEnvoy(&pool->envoys[i], i, config) < 0) {
			cleanupPartialPool(pool, i);
			return -1;
		}
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Destruir el pool: SIGTERM + waitpid de tots els hijos
* @Parametres: in/out: pool = pool a destruir
* @Retorn: ----
*
************************************************/
void destroyEnvoyPool(EnvoyPool *pool) {
	int i;
	int status;

	if (!pool || !pool->envoys) {
		return;
	}

	pool->running = 0;

	//enviar SIGTERM i tancar pipes
	for (i = 0; i < pool->num_envoys; i++) {
		if (pool->envoys[i].pid > 0) {
			//tancar pipe d'escriptura per que l'Envoy detecti EOF
			close(pool->envoys[i].pipe_to_envoy[PIPE_WRITE]);
			kill(pool->envoys[i].pid, SIGTERM);
		}
	}

	//esperar a que terminen
	for (i = 0; i < pool->num_envoys; i++) {
		if (pool->envoys[i].pid > 0) {
			waitpid(pool->envoys[i].pid, &status, 0);
			close(pool->envoys[i].pipe_from_envoy[PIPE_READ]);
		}
	}

	free(pool->envoys);
	pool->envoys = NULL;
	pool->num_envoys = 0;
}

/***********************************************
*
* @Finalitat: Cerca el primer Envoy en estat FREE
* @Parametres: in: pool
* @Retorn: index del Envoy o -1 si tots estan ocupats
*
************************************************/
int findFreeEnvoy(EnvoyPool *pool) {
	int i;

	if (!pool || !pool->envoys) {
		return -1;
	}

	for (i = 0; i < pool->num_envoys; i++) {
		if (pool->envoys[i].status == ENVOY_FREE) {
			return i;
		}
	}

	return -1;
}

/***********************************************
*
* @Finalitat: Envia una mision a un Envoy concret via pipe
* @Parametres: in/out: pool, in: envoy_id, in: mission
* @Retorn: 0 ok, -1 error
*
************************************************/
int assignMission(EnvoyPool *pool, int envoy_id, MissionMessage *mission) {
	ssize_t bytes_written;

	if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
		return -1;
	}

	if (pool->envoys[envoy_id].status != ENVOY_FREE) {
		return -1;
	}

	bytes_written = write(pool->envoys[envoy_id].pipe_to_envoy[PIPE_WRITE], mission, sizeof(MissionMessage));

	if (bytes_written != sizeof(MissionMessage)) {
		return -1;
	}

	pool->envoys[envoy_id].status = ENVOY_ON_MISSION;
	pool->envoys[envoy_id].mission_start_time = time(NULL);
    pool->envoys[envoy_id].current_mission = mission->type;
    copiarString(pool->envoys[envoy_id].mission_target, mission->realm, MAX_REALM_NAME);

    return 0;
}

/***********************************************
*
* @Finalitat: Llegeix de forma no bloquejant els pipes en busca de
*             resultats pendents. El primer que troba el retorna.
* @Parametres: in/out: pool, out: result
* @Retorn: index de l'Envoy que ha contestat o -1 si cap
*
************************************************/
int checkEnvoyResults(EnvoyPool *pool, MissionResult *result) {
	int i;
	ssize_t bytes_read;
	int flags;

	if (!pool || !pool->envoys || !result) {
		return -1;
	}

	for (i = 0; i < pool->num_envoys; i++) {
		if (pool->envoys[i].status == ENVOY_ON_MISSION) {
			//pipe no bloquejant temporalment
			flags = fcntl(pool->envoys[i].pipe_from_envoy[PIPE_READ], F_GETFL, 0);
			fcntl(pool->envoys[i].pipe_from_envoy[PIPE_READ], F_SETFL, flags | O_NONBLOCK);

			bytes_read = read(pool->envoys[i].pipe_from_envoy[PIPE_READ],
			                  result, sizeof(MissionResult));

			//restaurar mode bloquejant
			fcntl(pool->envoys[i].pipe_from_envoy[PIPE_READ], F_SETFL, flags);

			if (bytes_read == sizeof(MissionResult)) {
				return i;
			}
		}
	}

	return -1;
}

/***********************************************
*
* @Finalitat: Obte l'estat actual d'un Envoy
* @Parametres: in: pool, in: envoy_id
* @Retorn: EnvoyStatus
*
************************************************/
EnvoyStatus getEnvoyStatus(EnvoyPool *pool, int envoy_id) {
	if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
		return ENVOY_DEAD;
	}
	return pool->envoys[envoy_id].status;
}

/***********************************************
*
* @Finalitat: Obte el tipus de mision i el reino destinatari actuals
* @Parametres: in: pool, in: envoy_id, out: type, out: target
* @Retorn: ----
*
************************************************/
void getEnvoyMissionInfo(EnvoyPool *pool, int envoy_id, MissionType *type, char *target) {
	if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
		*type = MISSION_NONE;
		target[0] = '\0';
		return;
	}

	*type = pool->envoys[envoy_id].current_mission;
	copiarString(target, pool->envoys[envoy_id].mission_target, MAX_REALM_NAME);
}

/***********************************************
*
* @Finalitat: Reserva un Envoy sense enviar mision pel pipe
*             (per a TRADE on el Maester gestiona el submenu en local)
* @Parametres: in: pool, in: envoy_id, in: type, in: realm
* @Retorn: 0 ok, -1 error
*
************************************************/
int reserveEnvoy(EnvoyPool *pool, int envoy_id, MissionType type, const char *realm) {
	if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
		return -1;
	}

	if (pool->envoys[envoy_id].status != ENVOY_FREE) {
		return -1;
	}

	pool->envoys[envoy_id].status = ENVOY_ON_MISSION;
	pool->envoys[envoy_id].current_mission = type;
	copiarString(pool->envoys[envoy_id].mission_target, realm, MAX_REALM_NAME);

	return 0;
}

/***********************************************
*
* @Finalitat: Allibera un Envoy (qualsevol estat -> FREE)
* @Parametres: in: pool, in: envoy_id
* @Retorn: 0 ok, -1 error
*
************************************************/
int releaseEnvoy(EnvoyPool *pool, int envoy_id) {
	if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
		return -1;
	}

	pool->envoys[envoy_id].status = ENVOY_FREE;
	pool->envoys[envoy_id].current_mission = MISSION_NONE;
	pool->envoys[envoy_id].mission_target[0] = '\0';
	pool->envoys[envoy_id].mission_start_time = 0;

	return 0;
}

/***********************************************
*
* @Finalitat: Marca un Envoy com AWAITING_RESPONSE (sigil ja entregat,
*             esperem PLEDGE RESPOND del altre Maester fins a 2 min)
* @Parametres: in: pool, in: envoy_id, in: realm
* @Retorn: 0 ok, -1 error
*
	************************************************/
	int setEnvoyAwaitingResponse(EnvoyPool *pool, int envoy_id, const char *realm) {
		if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
			return -1;
		}

		pool->envoys[envoy_id].status = ENVOY_AWAITING_RESPONSE;
		pool->envoys[envoy_id].mission_start_time = time(NULL);
		pool->envoys[envoy_id].current_mission = MISSION_PLEDGE;
		copiarString(pool->envoys[envoy_id].mission_target, realm, MAX_REALM_NAME);
		pool->envoys[envoy_id].mission_start_time = time(NULL);

		return 0;
	}

/***********************************************
*
* @Finalitat: Cerca l'Envoy en AWAITING_RESPONSE per un reino concret
* @Parametres: in: pool, in: realm
* @Retorn: index de l'Envoy o -1 si cap
*
************************************************/
int findEnvoyAwaitingResponseFrom(EnvoyPool *pool, const char *realm) {
    int i;

    if (!pool || !pool->envoys || !realm) {
        return -1;
    }

    for (i = 0; i < pool->num_envoys; i++) {
        // 1. Ampliamos el filtro a los dos estados lógicos de misión activa
        if ((pool->envoys[i].status == ENVOY_AWAITING_RESPONSE || pool->envoys[i].status == ENVOY_ON_MISSION) &&
            pool->envoys[i].current_mission == MISSION_PLEDGE &&
            // 2. Usamos strcasecmp para que "maestera" y "MaesterA" se reconozcan perfectamente
            strcasecmp(pool->envoys[i].mission_target, realm) == 0) {
            
            return i;
        }
    }

    return -1;
}

/***********************************************
*
* @Finalitat: Notifica un Envoy AWAITING_RESPONSE de l'arribada de resposta
*             i el deixa FREE. No modifica l'estat d'alianç (ho fa el caller).
* @Parametres: in: pool, in: envoy_id, in: accepted, in: result_data
* @Retorn: 0 ok, -1 error
*
************************************************/
int notifyAllianceResult(EnvoyPool *pool, int envoy_id, int accepted, const char *result_data) {
	MissionResult result;

	if (!pool || !pool->envoys || envoy_id < 0 || envoy_id >= pool->num_envoys) {
		return -1;
	}

	if (pool->envoys[envoy_id].status != ENVOY_AWAITING_RESPONSE) {
		return -1;
	}

	result.envoy_id = envoy_id;
	result.type = MISSION_PLEDGE;
	result.status = accepted ? MISSION_SUCCESS : MISSION_FAILED;
	copiarString(result.realm, pool->envoys[envoy_id].mission_target, MAX_REALM_NAME);
	if (result_data) {
		copiarString(result.result_data, result_data, 256);
	} else {
		copiarString(result.result_data, accepted ? "Alliance accepted" : "Alliance rejected", 256);
	}

	pool->envoys[envoy_id].status = ENVOY_FREE;
	pool->envoys[envoy_id].current_mission = MISSION_NONE;
	pool->envoys[envoy_id].mission_target[0] = '\0';
	pool->envoys[envoy_id].mission_start_time = 0;

	return 0;
}

/***********************************************
*
* @Finalitat: Envia una trama TIMEOUT a l'altre Maester quan l'alianç venc
* @Parametres: in: route = ruta destí, in: config = configuracio local
* @Retorn: ----
*
************************************************/
static void notifyAllianceTimeout(Route *route, MaesterConfig *config) {
	char *origin;
	char *frame_data;
	Frame frame;
	char buffer[FRAME_SIZE];
	int fd;

	fd = openClientConn(route->ip, route->port);
	if (fd < 0) {
		return;
	}
	asprintf(&origin, "%s:%d", config->ip, config->port);
	asprintf(&frame_data, "TIMEOUT&%s", config->realm_name);
	createFrame(FRAME_TYPE_ALLIANCE_RESPONSE, origin, route->name, frame_data, &frame);
	serializeFrame(&frame, buffer);
	write(fd, buffer, FRAME_SIZE);
	close(fd);
	free(origin);
	free(frame_data);
}

/***********************************************
*
* @Finalitat: Marca l'alianç com FAILED i notifica al altre extrem
* @Parametres: in/out: envoy, in: config
* @Retorn: ----
*
************************************************/
static void handleEnvoyTimeout(EnvoyInfo *envoy, MaesterConfig *config, SharedData *shared) {
	Route *route;

	//notificacio consistent amb la resta (prefix >>> i Envoy 1-indexat),
	//via queueOrShowNotification perque respecti el mode trade
	queueOrShowNotification(shared,
	        ">>> Pledge to %s has failed (TIMEOUT). Envoy %d released.\n",
	        envoy->mission_target, envoy->id + 1);

	if (config) {
		route = findRouteByName(envoy->mission_target, config);
		if (route) {
			setAllianceStatus(route, ALLIANCE_FAILED);
			notifyAllianceTimeout(route, config);
		}
	}

	envoy->status = ENVOY_FREE;
	envoy->current_mission = MISSION_NONE;
	envoy->mission_target[0] = '\0';
	envoy->mission_start_time = 0;
}

/***********************************************
*
* @Finalitat: Comprova si algun Envoy AWAITING_RESPONSE ha expirat el
*             timeout de 2 minuts. Si si: marca l'alianç com FAILED,
*             envia un TIMEOUT al altre extrem i allibera l'Envoy.
* @Parametres: in/out: pool, in: config = configuracio (NULL = no actualitza alianç)
* @Retorn: nombre d'Envoys alliberats per timeout
*
************************************************/
int checkEnvoyTimeouts(EnvoyPool *pool, MaesterConfig *config, SharedData *shared) {
	int i;
	int count;
	time_t now;
	time_t elapsed;

	if (!pool || !pool->envoys) {
		return 0;
	}

	count = 0;
	now = time(NULL);

	for (i = 0; i < pool->num_envoys; i++) {
		if (pool->envoys[i].status == ENVOY_AWAITING_RESPONSE) {
			elapsed = now - pool->envoys[i].mission_start_time;
			if (elapsed >= PLEDGE_TIMEOUT_SECONDS) {
				handleEnvoyTimeout(&pool->envoys[i], config, shared);
				count++;
			}
		}
	}

	return count;
}
