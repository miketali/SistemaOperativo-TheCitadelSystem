/***********************************************
*
* @Proposit: Implementacion del modulo de peticiones de alianza (PLEDGE)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "pledge.h"

/***********************************************
*
* @Finalitat: Cerca route o crea ruta dinamica via DEFAULT si cal
* @Parametres: in: realm, in/out: config, must hold config_lock
* @Retorn: punter a route, NULL si error
*
************************************************/
static Route* resolvePledgeRoute(const char *realm, MaesterConfig *config) {
	Route *route;
	char *msg;

	route = findRouteByName(realm, config);
	if (!route) {
		//NO esta en la taula de rutes: el regne no existeix en el sistema
		//(PDF: nomes els regnes coneguts apareixen al .dat, amb IP directa
		//o amb *.*.*.* si no es vei directe)
		asprintf(&msg, "The realm of %s is unknown to us.\n", realm);
		writeString(msg);
		free(msg);
		return NULL;
	}

	if (isWildcardRoute(route)) {
		//Existeix pero sense ruta directa: cal el DEFAULT per arribar-hi
		if (!findDefaultRoute(config)) {
			asprintf(&msg, "No route to %s and no DEFAULT available.\n", realm);
			writeString(msg);
			free(msg);
			return NULL;
		}
		asprintf(&msg, "The realm of %s lies beyond our borders. Routing through the network.\n", realm);
		writeString(msg);
		free(msg);
	}

	return route;
}

/***********************************************
*
* @Finalitat: Comprova precondicions de PLEDGE (estat + sigil existeix)
* @Parametres: in: realm, sigil_path, in: route
* @Retorn: 0 ok, -1 abort (missatge ja imprimit)
*
************************************************/
static int checkPledgePreconditions(const char *realm, const char *sigil_path,
                                    const Route *route) {
	char *msg;

	if (route->alliance_status == ALLIANCE_ALLIED) {
		asprintf(&msg, "We already hold alliance with %s.\n", realm);
		writeString(msg); free(msg);
		return -1;
	}
	if (route->alliance_status == ALLIANCE_PENDING) {
		asprintf(&msg, "A pledge to %s already awaits response.\n", realm);
		writeString(msg); free(msg);
		return -1;
	}
	if (access(sigil_path, F_OK) != 0) {
		asprintf(&msg, "The sigil '%s' cannot be found in our archives.\n", sigil_path);
		writeString(msg); free(msg);
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Construeix el MissionMessage per assignar a un Envoy
* @Parametres: in: realm, sigil_path, route, config, out: mission
* @Retorn: ----
*
************************************************/
static void buildPledgeMission(const char *realm, const char *sigil_path,
                               const Route *route, const MaesterConfig *config,
                               MissionMessage *mission) {
	const char *target_ip;
	int target_port;
	Route *default_route;

	target_ip = route->ip;
	target_port = route->port;

	//Si la ruta es wildcard (*.*.*.* 0), enviar via DEFAULT
	if (isWildcardRoute(route)) {
		default_route = findDefaultRoute((MaesterConfig *)config);
		if (default_route) {
			target_ip = default_route->ip;
			target_port = default_route->port;
		}
	}

	memset(mission, 0, sizeof(MissionMessage));
	mission->type = MISSION_PLEDGE;
	copiarString(mission->realm, realm, MAX_REALM_NAME);
	copiarString(mission->sigil_file, sigil_path, MAX_FILE_PATH);
	copiarString(mission->target_ip, target_ip, 20);
	mission->target_port = target_port;
	copiarString(mission->origin_ip, config->ip, 20);
	mission->origin_port = config->port;
	copiarString(mission->realm_name, config->realm_name, MAX_REALM_NAME);
}

/***********************************************
*
* @Finalitat: Marca la ruta com PENDING amb pending_origin
* @Parametres: in/out: route
* @Retorn: ----
*
************************************************/
static void markRoutePending(Route *route) {
	setAllianceStatus(route, ALLIANCE_PENDING);
	if (route->pending_origin) free(route->pending_origin);
	route->pending_origin = createOriginString(route->ip, route->port);
}

/***********************************************
*
* @Finalitat: Comando PLEDGE - delega l'enviament a un Envoy lliure
* @Parametres: in: realm, sigil_file, in/out: config, shared
* @Retorn: ----
*
************************************************/
void handlePledge(const char *realm, const char *sigil_file,
                  MaesterConfig *config, SharedData *shared) {
	Route *route;
	int envoy_id;
	MissionMessage mission;
	char *full_sigil_path;
	char *msg;

	asprintf(&full_sigil_path, "%s/%s", config->folder_path, sigil_file);

	pthread_mutex_lock(&shared->config_lock);
	route = resolvePledgeRoute(realm, config);
	if (!route || checkPledgePreconditions(realm, full_sigil_path, route) != 0) {
		pthread_mutex_unlock(&shared->config_lock);
		free(full_sigil_path);
		return;
	}
	pthread_mutex_unlock(&shared->config_lock);

	pthread_mutex_lock(&shared->envoy_lock);
	envoy_id = findFreeEnvoy(shared->envoy_pool);
	if (envoy_id < 0) {
		pthread_mutex_unlock(&shared->envoy_lock);
		writeString("All envoys are occupied. Your command must wait.\n");
		free(full_sigil_path);
		return;
	}

	pthread_mutex_lock(&shared->config_lock);
	buildPledgeMission(realm, full_sigil_path, route, config, &mission);
	markRoutePending(route);
	pthread_mutex_unlock(&shared->config_lock);
	free(full_sigil_path);

	if (assignMission(shared->envoy_pool, envoy_id, &mission) != 0) {
		pthread_mutex_unlock(&shared->envoy_lock);
		writeString("Error: Could not assign mission to Envoy\n");
		return;
	}
	pthread_mutex_unlock(&shared->envoy_lock);

	asprintf(&msg, "Pledge sent to %s.\n", realm);
	writeString(msg);
	free(msg);
}

/***********************************************
*
* @Finalitat: Valida que hi hagi una peticio PENDING i extreu IP:Port
* @Parametres: in: realm, in: config, out: ip_origen, out: origin_port,
*              out: out_route
* @Retorn: 0 ok, -1 error (missatge ja imprimit)
*
************************************************/
static int validatePledgeRespond(const char *realm, MaesterConfig *config,
                                 char **ip_origen, int *origin_port, Route **out_route) {
	Route *route;
	char *msg;

	route = findRouteByName(realm, config);
	if (!route) {
		asprintf(&msg, "The realm of %s is unknown to us.\n", realm);
		writeString(msg); free(msg);
		return -1;
	}
	if (route->alliance_status != ALLIANCE_PENDING) {
		asprintf(&msg, "No pledge from %s awaits our response.\n", realm);
		writeString(msg); free(msg);
		return -1;
	}
	if (!route->pending_origin) {
		writeString("Error: No origin stored for pending request\n");
		return -1;
	}
	if (parseOriginString(route->pending_origin, ip_origen, origin_port) != 0) {
		writeString("Error: Invalid origin format\n");
		return -1;
	}
	*out_route = route;
	return 0;
}

/***********************************************
*
* @Finalitat: Envia la trama 0x03 ACCEPT/REJECT al regne origen
* @Parametres: in: fd, response_data, realm_name, my_origin, dest_realm
* @Retorn: 0 ok, -1 error
*
************************************************/
static int sendAllianceResponseFrame(int fd, const char *response_data,
                                     const char *my_origin, const char *dest_realm,
                                     const char *realm_name) {
	char data_buffer[256];
	snprintf(data_buffer, sizeof(data_buffer), "%s&%s", response_data, realm_name);
	return sendFrame(fd, FRAME_TYPE_ALLIANCE_RESPONSE, my_origin, dest_realm, data_buffer);
}

/***********************************************
*
* @Finalitat: Espera un ACK (0x31) amb timeout. Tot el resto compta com NACK
* @Parametres: in: fd
* @Retorn: 1 si ACK rebut, 0 si NACK/timeout
*
************************************************/
static int waitAllianceAck(int fd) {
	Frame ack_frame;
	if (receiveAndValidateFrame(fd, &ack_frame, 0, 30) == 0 &&
	    ack_frame.type == FRAME_TYPE_ACK) {
		return 1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Aplica el resultat final al route i imprimeix missatge
* @Parametres: in/out: route, in: realm, response, ack_received
* @Retorn: ----
*
************************************************/
static void applyPledgeRespondResult(Route *route, const char *realm,
                                     const char *response, int ack_received) {
	char *msg;
	int is_accept = (strcasecmpCustom(response, "accept") == 0);

	if (ack_received && is_accept) {
		setAllianceStatus(route, ALLIANCE_ALLIED);
		setAllyDirectConnection(route, route->pending_origin);
		asprintf(&msg, "Alliance with %s forged successfully!\n", realm);
	} else if (!is_accept) {
		setAllianceStatus(route, ALLIANCE_FAILED);
		asprintf(&msg, "Alliance with %s has been refused.\n", realm);
	} else {
		setAllianceStatus(route, ALLIANCE_FAILED);
		asprintf(&msg, "Alliance with %s could not be confirmed.\n", realm);
	}
	writeString(msg);
	free(msg);
}

/***********************************************
*
* @Finalitat: Comando PLEDGE RESPOND <regne> ACCEPT/REJECT
* @Parametres: in: realm, response, in/out: config
* @Retorn: ----
*
************************************************/
void handlePledgeRespond(const char *realm, const char *response, MaesterConfig *config) {
	Route *route;
	int fd;
	char *ip_origen;
	int origin_port;
	const char *response_data;
	char *my_origin;
	char *msg;
	int ack_received;

	if (validatePledgeRespond(realm, config, &ip_origen, &origin_port, &route) != 0) return;

	response_data = (strcasecmpCustom(response, "accept") == 0) ? "ACCEPT" : "REJECT";

	asprintf(&msg, "Sending our response to %s...\n", realm);
	writeString(msg); free(msg);

	fd = openClientConn(ip_origen, origin_port);
	if (fd < 0) {
		writeString("The realm could not be reached. They may have departed.\n");
		setAllianceStatus(route, ALLIANCE_FAILED);
		free(ip_origen);
		free(route->pending_origin);
		route->pending_origin = NULL;
		return;
	}

	my_origin = createOriginString(config->ip, config->port);
	if (!my_origin ||
	    sendAllianceResponseFrame(fd, response_data, my_origin, realm,
	                              config->realm_name) != 0) {
		writeString(my_origin ? "Error: Could not send response\n"
		                      : "Error: Could not create origin string\n");
		close(fd);
		setAllianceStatus(route, ALLIANCE_FAILED);
		if (my_origin) free(my_origin);
		free(ip_origen);
		free(route->pending_origin);
		route->pending_origin = NULL;
		return;
	}
	free(my_origin);

	ack_received = waitAllianceAck(fd);
	close(fd);

	applyPledgeRespondResult(route, realm, response, ack_received);

	free(ip_origen);
	free(route->pending_origin);
	route->pending_origin = NULL;
}
