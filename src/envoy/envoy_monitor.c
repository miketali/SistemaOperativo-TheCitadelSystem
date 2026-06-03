/***********************************************
*
* @Proposit: Thread monitor del pool d'Envoys + handler ENVOY STATUS
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest fitxer es l'unic punt d'entrada cap a la maquinaria del pool
* des del Maester corrent. Sondea cada 500 ms els pipes de resposta,
* converteix els MissionResult en accions (canviar estat d'alianç,
* alliberar Envoy, notificar usuari) i ofereix la comanda ENVOY STATUS.
*
************************************************/
#include "envoy.h"

//Estat del thread monitor (private a aquest modul)
static pthread_t envoy_monitor_tid;
static volatile int monitor_running = 0;
static SharedData *monitor_shared = NULL;

/***********************************************
*
* @Finalitat: Despatxa un MissionResult: canvia l'estat d'alianç i
*             l'estat del Envoy segons el resultat de la mision.
* @Parametres: in: result, in/out: shared, in: envoy_id
* @Retorn: ----
*
************************************************/
static void processEnvoyResult(MissionResult *result, SharedData *shared, int envoy_id) {
	Route *route;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByName(result->realm, shared->config);

	if (route) {
		if (result->status == MISSION_SUCCESS) {
			if (result->type == MISSION_PLEDGE) {
				//PLEDGE: el sigil s'ha enviat correctament.
				//L'Envoy passa a AWAITING_RESPONSE (espera fins a 2 min)
				pthread_mutex_lock(&shared->envoy_lock);
				setEnvoyAwaitingResponse(shared->envoy_pool, envoy_id, result->realm);
				pthread_mutex_unlock(&shared->envoy_lock);

				pthread_mutex_unlock(&shared->config_lock);
				queueOrShowNotification(shared, ">>> Sigil delivered to %s. Envoy %d awaits their response.\n",
				                        result->realm, envoy_id + 1);
				//NO canviem estat d'alianç - es mante PENDING
				return;
			} else {
				//TRADE o altres: alliberar Envoy
				pthread_mutex_lock(&shared->envoy_lock);
				releaseEnvoy(shared->envoy_pool, envoy_id);
				pthread_mutex_unlock(&shared->envoy_lock);

				pthread_mutex_unlock(&shared->config_lock);
				queueOrShowNotification(shared, ">>> Trade with %s has been sealed!\n", result->realm);
				return;
			}
		} else if (result->status == MISSION_TIMEOUT) {
			pthread_mutex_lock(&shared->envoy_lock);
			releaseEnvoy(shared->envoy_pool, envoy_id);
			pthread_mutex_unlock(&shared->envoy_lock);

			setAllianceStatus(route, ALLIANCE_FAILED);
			pthread_mutex_unlock(&shared->config_lock);
			queueOrShowNotification(shared, ">>> The envoy to %s has not returned in time.\n", result->realm);
			return;
		} else {
			pthread_mutex_lock(&shared->envoy_lock);
			releaseEnvoy(shared->envoy_pool, envoy_id);
			pthread_mutex_unlock(&shared->envoy_lock);

			setAllianceStatus(route, ALLIANCE_FAILED);
			pthread_mutex_unlock(&shared->config_lock);
			queueOrShowNotification(shared, ">>> The mission to %s has failed: %s\n", result->realm, result->result_data);
			return;
		}
	} else {
		pthread_mutex_lock(&shared->envoy_lock);
		releaseEnvoy(shared->envoy_pool, envoy_id);
		pthread_mutex_unlock(&shared->envoy_lock);

		pthread_mutex_unlock(&shared->config_lock);
		queueOrShowNotification(shared, ">>> An envoy returned from unknown realm: %s\n", result->realm);
	}
}

/***********************************************
*
* @Finalitat: Comprova timeouts i processa tots els resultats pendents.
*             Es crida tant des del thread monitor com des de
*             executeCommand al thread principal.
* @Parametres: in/out: shared
* @Retorn: ----
*
************************************************/
void checkAndProcessEnvoyResults(SharedData *shared) {
	MissionResult result;
	int envoy_id;

	if (!shared->envoy_pool) {
		return;
	}

	//primer verificar timeouts dels Envoys AWAITING_RESPONSE
	pthread_mutex_lock(&shared->envoy_lock);
	checkEnvoyTimeouts(shared->envoy_pool, shared->config, shared);
	pthread_mutex_unlock(&shared->envoy_lock);

	//despres processar resultats dels Envoys que han completat misions
	pthread_mutex_lock(&shared->envoy_lock);
	envoy_id = checkEnvoyResults(shared->envoy_pool, &result);
	pthread_mutex_unlock(&shared->envoy_lock);

	while (envoy_id >= 0) {
		processEnvoyResult(&result, shared, envoy_id);

		pthread_mutex_lock(&shared->envoy_lock);
		envoy_id = checkEnvoyResults(shared->envoy_pool, &result);
		pthread_mutex_unlock(&shared->envoy_lock);
	}
}

/***********************************************
*
* @Finalitat: Bucle del thread monitor amb select() sobre els N pipes de
*             resposta dels Envoys. select() bloqueja al kernel fins que:
*               (a) un Envoy escriu al seu pipe -> despertar immediat, o
*               (b) venç el timeout d'1 segon -> revisar timeouts de PLEDGE.
*             Substitueix el polling de 500 ms per una espera dirigida
*             per esdeveniment (mes eficient i amb menys latencia).
*             La sincronitzacio amb el thread principal es manté amb
*             els mutex envoy_lock / config_lock (semàfors binaris POSIX).
*
************************************************/
static void* envoyMonitorThread(void *arg) {
	(void)arg;
	fd_set read_fds;
	int max_fd;
	int i;
	int fd;
	int ret;
	struct timeval timeout;
	EnvoyPool *pool;

	while (monitor_running) {
		//esperar a que el pool estigui inicialitzat
		if (!monitor_shared || !monitor_shared->envoy_pool) {
			usleep(100000);  //100 ms només mentre arrenca el sistema
			continue;
		}

		pool = monitor_shared->envoy_pool;

		//construir el fd_set amb tots els pipes de resposta dels Envoys
		FD_ZERO(&read_fds);
		max_fd = -1;

		pthread_mutex_lock(&monitor_shared->envoy_lock);
		for (i = 0; i < pool->num_envoys; i++) {
			fd = pool->envoys[i].pipe_from_envoy[PIPE_READ];
			if (fd >= 0) {
				FD_SET(fd, &read_fds);
				if (fd > max_fd) max_fd = fd;
			}
		}
		pthread_mutex_unlock(&monitor_shared->envoy_lock);

		if (max_fd < 0) {
			usleep(100000);
			continue;
		}

		//timeout d'1 s perquè es revisin periodicament els timeouts dels PLEDGE
		//(els PLEDGE caduquen als 2 min, 1 s de granularitat es de sobres)
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

		if (ret < 0) {
			if (errno == EINTR) continue;  //interromput per signal: tornar a esperar
			break;                         //error real: sortir
		}

		//sigui per dades en algun pipe (ret > 0) o per timeout (ret == 0),
		//processar tots els resultats pendents i comprovar timeouts
		checkAndProcessEnvoyResults(monitor_shared);
	}

	return NULL;
}

/***********************************************
*
* @Finalitat: Arranca el thread monitor (idempotent)
* @Parametres: in/out: shared
* @Retorn: 0 ok, -1 error
*
************************************************/
int startEnvoyMonitor(SharedData *shared) {
	if (monitor_running) {
		return 0;
	}

	monitor_shared = shared;
	monitor_running = 1;

	if (pthread_create(&envoy_monitor_tid, NULL, envoyMonitorThread, NULL) != 0) {
		monitor_running = 0;
		return -1;
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Atura el thread monitor i fa join
*
************************************************/
void stopEnvoyMonitor(void) {
	if (!monitor_running) {
		return;
	}

	monitor_running = 0;
	pthread_join(envoy_monitor_tid, NULL);
	monitor_shared = NULL;
}

/***********************************************
*
* @Finalitat: Comando ENVOY STATUS - mostra l'estat actual de cada Envoy
*             Unifica ON_MISSION i AWAITING_RESPONSE sota
*             "ON MISSION (PLEDGE to X)" segons el Testing PDF.
* @Parametres: in: shared
* @Retorn: ----
*
************************************************/
void handleEnvoyStatus(SharedData *shared) {
	int i;
	EnvoyStatus status;
	MissionType mission_type;
	char target[MAX_REALM_NAME];
	char *msg;

	if (!shared->envoy_pool) {
		writeString("Error: Envoy pool not initialized\n");
		return;
	}

	pthread_mutex_lock(&shared->envoy_lock);
	for (i = 0; i < shared->envoy_pool->num_envoys; i++) {
		status = getEnvoyStatus(shared->envoy_pool, i);
		getEnvoyMissionInfo(shared->envoy_pool, i, &mission_type, target);

		if ((status == ENVOY_ON_MISSION || status == ENVOY_AWAITING_RESPONSE) && target[0] != '\0') {
			if (mission_type == MISSION_PLEDGE) {
				asprintf(&msg, "- Envoy %d: ON MISSION (PLEDGE to %s)\n", i + 1, target);
				writeString(msg);
				free(msg);
			} else if (mission_type == MISSION_TRADE) {
				asprintf(&msg, "- Envoy %d: ON MISSION (TRADE to %s)\n", i + 1, target);
				writeString(msg);
				free(msg);
			} else {
				asprintf(&msg, "- Envoy %d: ON MISSION\n", i + 1);
				writeString(msg);
				free(msg);
			}
		} else if (status == ENVOY_FREE) {
			asprintf(&msg, "- Envoy %d: FREE\n", i + 1);
			writeString(msg);
			free(msg);
		} else if (status == ENVOY_DEAD) {
			asprintf(&msg, "- Envoy %d: DEAD\n", i + 1);
			writeString(msg);
			free(msg);
		} else {
			asprintf(&msg, "- Envoy %d: UNKNOWN STATE\n", i + 1);
			writeString(msg);
			free(msg);
		}
	}
	pthread_mutex_unlock(&shared->envoy_lock);
}
