/***********************************************
*
* @Proposit: Dispatcher del terminal interactiu del Maester.
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest fitxer nomes conte el parser/dispatcher executeCommand i els
* handlers locals que no necessiten Envoys ni xarxa (LIST REALMS,
* LIST PRODUCTS local, PLEDGE STATUS, EXIT). Els comandaments complexos
* viuen en moduls separats:
*   - pledge.c: PLEDGE / PLEDGE RESPOND
*   - trade.c:  LIST PRODUCTS <regne> / START TRADE
*   - envoy.c:  ENVOY STATUS i el monitor de resultats
*
************************************************/
#include "commands.h"

/***********************************************
*
* @Finalitat: Comando LIST REALMS - llista tots els regnes coneguts
* @Parametres: in: config = configuracio del Maester
* @Retorn: ----
*
************************************************/
void handleListRealms(const MaesterConfig *config) {
	int i;
	char *msg;

	for (i = 0; i < config->num_routes; i++) {
		if (strcmp(config->routes[i].name, "DEFAULT") == 0) {
			continue;
		}

		asprintf(&msg, "- %s\n", config->routes[i].name);
		writeString(msg);
		free(msg);
	}
}

/***********************************************
*
* @Finalitat: Comando LIST PRODUCTS - mostra l'inventari local
* @Parametres: in: inventory = inventari propi
* @Retorn: ----
*
************************************************/
void handleListProducts(const Inventory *inventory) {
	displayInventory(inventory);
}

/***********************************************
*
* @Finalitat: Comando PLEDGE STATUS - mostra l'estat de cada aliança
* @Parametres: in: config = configuracio (rutes amb alliance_status)
* @Retorn: ----
*
************************************************/
void handlePledgeStatus(const MaesterConfig *config) {
	int i;
	int found_any;
	const char *status_str;
	char *msg;

	writeString("Alliances of this realm:\n");

	found_any = 0;
	for (i = 0; i < config->num_routes; i++) {
		if (strcmp(config->routes[i].name, "DEFAULT") == 0) {
			continue;
		}

		switch (config->routes[i].alliance_status) {
			case ALLIANCE_NONE:    status_str = "NONE";    break;
			case ALLIANCE_PENDING: status_str = "PENDING"; break;
			case ALLIANCE_ALLIED:  status_str = "ALLIED";  break;
			case ALLIANCE_FAILED:  status_str = "FAILED";  break;
			default:               status_str = "UNKNOWN"; break;
		}

		asprintf(&msg, "- %s: %s\n", config->routes[i].name, status_str);
		writeString(msg);
		free(msg);
		found_any = 1;
	}

	if (!found_any) {
		writeString("No realms are known.\n");
	}
}

/***********************************************
*
* @Finalitat: Comando EXIT - notifica DISCONNECT (0x27) als aliats i surt
* @Parametres: in: config
* @Retorn: ----
*
************************************************/
void handleExit(const MaesterConfig *config) {
	int i;
	int fd;
	char *origin;
	char *msg;
	char *target_ip;
	int target_port;
	const Route *r;

	//crear string ORIGIN con formato IP:Port
	origin = createOriginString(config->ip, config->port);
	if (!origin) {
		writeString("Error: Could not create origin string\n");
		return;
	}

	//notificar desconexion a tots els regnes ALLIED. Preferim ally_ip
	//(apresa durant el PLEDGE) sobre route->ip, que pot ser *.*.*.* per
	//als regnes no veins directes.
	for (i = 0; i < config->num_routes; i++) {
		r = &config->routes[i];
		if (r->alliance_status != ALLIANCE_ALLIED) continue;

		if (r->ally_ip && r->ally_port > 0) {
			target_ip = r->ally_ip;
			target_port = r->ally_port;
		} else if (!isWildcardRoute(r)) {
			target_ip = r->ip;
			target_port = r->port;
		} else {
			//wildcard sense ally_ip apresa: no podem notificar directament
			continue;
		}

		fd = openClientConn(target_ip, target_port);
		if (fd < 0) continue;
		sendFrame(fd, FRAME_TYPE_DISCONNECT, origin, r->name, "DISCONNECT");
		close(fd);
	}

	free(origin);

	asprintf(&msg, "The Maester of %s signs off. The ravens rest.\n",
	         config->realm_name);
	writeString(msg);
	free(msg);
}

/***********************************************
*
* @Finalitat: Tokenitza el buffer del comando en args[]
* @Parametres: in/out: buf = string a tokenizar (modificada in-place)
*              out: args = array de punters a tokens
* @Retorn: nombre de tokens trobats (max MAX_ARGS)
*
************************************************/
static int tokenizeCommand(char *buf, char *args[]) {
	char *tok;
	char *next_pos;
	int n;

	n = 0;
	next_pos = NULL;
	tok = parseToken(buf, &next_pos);
	while (tok && n < MAX_ARGS) {
		args[n++] = tok;
		tok = parseToken(NULL, &next_pos);
	}
	return n;
}

/***********************************************
*
* @Finalitat: Dispatcher del comando LIST (REALMS o PRODUCTS)
* @Parametres: in: args, n, config, inventory, shared
* @Retorn: ----
*
************************************************/
static void dispatchListCommand(char *args[], int n, const MaesterConfig *config,
                                const Inventory *inventory, SharedData *shared) {
	if (n < 2) {
		writeString("That command is unknown to the Maester.\n");
		return;
	}
	toLower(args[1]);
	if (strcmp(args[1], "realms") == 0 && n == 2) {
		handleListRealms(config);
	} else if (strcmp(args[1], "products") == 0 && n == 3) {
		handleListProductsRemote(args[2], shared);
	} else if (strcmp(args[1], "products") == 0 && n == 2) {
		handleListProducts(inventory);
	} else {
		writeString("That command is unknown to the Maester.\n");
	}
}

/***********************************************
*
* @Finalitat: Dispatcher del comando PLEDGE (STATUS, RESPOND, normal)
* @Parametres: in: args, n, config, shared
* @Retorn: ----
*
************************************************/
static void dispatchPledgeCommand(char *args[], int n, const MaesterConfig *config, SharedData *shared) {
	if (n < 2) {
		writeString("To send a pledge: PLEDGE <realm> <sigil.jpg>\n");
		return;
	}
	if (strcasecmpCustom(args[1], "status") == 0 && n == 2) {
		handlePledgeStatus(config);
	} else if (strcasecmpCustom(args[1], "respond") == 0 && n == 4) {
		handlePledgeRespond(args[2], args[3], (MaesterConfig *)config);
	} else if (n == 3) {
		handlePledge(args[1], args[2], (MaesterConfig *)config, shared);
	} else {
		writeString("To send a pledge: PLEDGE <realm> <sigil.jpg>\n");
	}
}

/***********************************************
*
* @Finalitat: Dispatcher dels comandos START i ENVOY
* @Parametres: in: args, n, shared
* @Retorn: ----
*
************************************************/
static void dispatchStartOrEnvoy(char *args[], int n, SharedData *shared) {
	if (n < 2) {
		writeString("That command is unknown to the Maester.\n");
		return;
	}
	toLower(args[1]);
	if (strcmp(args[0], "start") == 0) {
		if (strcmp(args[1], "trade") == 0 && n == 3) {
			handleStartTrade(args[2], shared);
		} else if (strcmp(args[1], "trade") == 0 && n == 2) {
			writeString("To start a trade: START TRADE <realm>\n");
		} else {
			writeString("That command is unknown to the Maester.\n");
		}
	} else {  //envoy
		if (strcmp(args[1], "status") == 0 && n == 2) {
			handleEnvoyStatus(shared);
		} else {
			writeString("That command is unknown to the Maester.\n");
		}
	}
}

/***********************************************
*
* @Finalitat: Parser i dispatcher del terminal interactiu
* @Parametres: in: command = linia introduida per l'usuari
*              in: config, inventory, shared = estat del Maester
* @Retorn: 0 continuar bucle, 1 sortir
*
************************************************/
int executeCommand(const char *command, const MaesterConfig *config,
                   const Inventory *inventory, SharedData *shared) {
	char *buf;
	char *args[MAX_ARGS];
	int n;
	//Fase 4: processar resultats pendents d'Envoys abans de cada comando
	checkAndProcessEnvoyResults(shared);

	buf = copyStringDynamic(command);
	if (!buf) {
		writeString("Error: Could not allocate memory for command buffer\n");
		return 0;
	}

	trim(buf);

	if (strlen(buf) == 0) {
		writeString("The Maester awaits your command.\n");
		free(buf);
		return 0;
	}

	n = tokenizeCommand(buf, args);
	if (n == 0) {
		free(buf);
		return 0;
	}

	toLower(args[0]);
	if (strcmp(args[0], "list") == 0) {
		dispatchListCommand(args, n, config, inventory, shared);
	} else if (strcmp(args[0], "pledge") == 0) {
		dispatchPledgeCommand(args, n, config, shared);
	} else if (strcmp(args[0], "start") == 0 || strcmp(args[0], "envoy") == 0) {
		dispatchStartOrEnvoy(args, n, shared);
	} else if (strcmp(args[0], "exit") == 0) {
		handleExit(config);
		free(buf);
		return 1;
	} else {
		writeString("That command is unknown to the Maester.\n");
	}

	free(buf);
	return 0;
}
