/***********************************************
*
* @Proposit: Programa principal Maester - Fase 4
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Fase 4: gestiona Envoys (fork) y comunicacion por pipes para
* misiones simultaneas. Aquest fitxer nomes conte el main i les
* funcions estatiques d'inicialitzacio i neteja que el composen.
*
************************************************/
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "inventory.h"
#include "commands.h"
#include "handlers.h"
#include "server.h"
#include "utils.h"
#include "envoy.h"
#include "products_cache.h"

/***********************************************
*
* @Finalitat: Carrega config + inventari i mostra missatge de benvinguda
* @Parametres: in: config_file, stock_file = paths dels fitxers
*              out: config, inventory = estructures a omplir
* @Retorn: 0 ok, 1 error
*
************************************************/
static int loadConfigAndInventory(const char *config_file, const char *stock_file,
                                  MaesterConfig *config, Inventory *inventory) {
	char *msg;

	if (readConfig(config_file, config) != 0) {
		writeString("Error: Could not read configuration file\n");
		return 1;
	}

	if (loadInventory(stock_file, inventory) != 0) {
		writeString("Error: Could not load inventory file\n");
		freeConfig(config);
		return 1;
	}

	config->stock_file = copyStringDynamic(stock_file);

	asprintf(&msg, "\nMaester of %s initialized. The board is set.\n", config->realm_name);
	writeString(msg);
	free(msg);
	return 0;
}

/***********************************************
*
* @Finalitat: Inicialitza el pool d'Envoys i el thread monitor
* @Parametres: in: config = configuracio del Maester
*              in/out: shared = dades compartides (s'enllaça el pool)
*              out: pool = pool a inicialitzar
*              out: envoy_cfg = config compartit per als hijos
* @Retorn: 0 ok, 1 error
*
************************************************/
static int initEnvoys(MaesterConfig *config, SharedData *shared,
                      EnvoyPool *pool, EnvoySharedConfig *envoy_cfg) {
	char *msg;

	envoy_cfg->origin_port = config->port;
	copiarString(envoy_cfg->origin_ip, config->ip, 20);
	copiarString(envoy_cfg->realm_name, config->realm_name, MAX_REALM_NAME);
	copiarString(envoy_cfg->folder_path, config->folder_path, MAX_FILE_PATH);

	if (initEnvoyPool(pool, config->num_envoys, envoy_cfg) != 0) {
		writeString("Error: Could not initialize Envoy pool\n");
		return 1;
	}

	shared->envoy_pool = pool;
	setEnvoyPool(pool);

	asprintf(&msg, "Envoy pool initialized with %d envoys.\n\n", config->num_envoys);
	writeString(msg);
	free(msg);

	startEnvoyMonitor(shared);
	return 0;
}

/***********************************************
*
* @Finalitat: Bucle interactiu del terminal: llegeix comandes fins EXIT
* @Parametres: in: config, inventory, shared = estat compartit
* @Retorn: ----
*
************************************************/
static void runTerminalLoop(MaesterConfig *config, Inventory *inventory, SharedData *shared) {
	char *cmd;
	int exit_flag;

	exit_flag = 0;
	while (!exit_flag) {
		writeString("$ ");
		cmd = readUntil(0, '\n');
		if (!cmd) {
			exit_flag = 1;
		} else {
			exit_flag = executeCommand(cmd, config, inventory, shared);
			free(cmd);
		}
	}
}

/***********************************************
*
* @Finalitat: Allibera tots els recursos: monitor + pool + locks + memoria
* @Parametres: in/out: shared, config, inventory, pool = estructures a alliberar
* @Retorn: ----
*
************************************************/
static void shutdownMaester(SharedData *shared, MaesterConfig *config,
                            Inventory *inventory, EnvoyPool *pool) {
	stopEnvoyMonitor();
	setShutdownInProgress();
	destroyEnvoyPool(pool);
	cleanupSharedData(shared);
	freeProductsCache();
	freeConfig(config);
	freeInventory(inventory);
}

/***********************************************
*
* @Finalitat: Main del programa Maester
* @Parametres: in: argc, argv = arguments de linia de comandes
* @Retorn: 0 ok, 1 error
*
************************************************/
int main(int argc, char *argv[]) {
	MaesterConfig config;
	Inventory inventory;
	SharedData shared_data;
	EnvoyPool envoy_pool;
	EnvoySharedConfig envoy_config;

	if (argc != 3) {
		writeString("Usage: maester <config.dat> <stock.db>\n");
		return 1;
	}

	setupSignalHandlers();

	if (loadConfigAndInventory(argv[1], argv[2], &config, &inventory) != 0) {
		return 1;
	}

	initSharedData(&shared_data, &config, &inventory);

	setEnvoyParentRefs(&config, &inventory, &envoy_pool);
	if (initEnvoys(&config, &shared_data, &envoy_pool, &envoy_config) != 0) {
		cleanupSharedData(&shared_data);
		freeConfig(&config);
		freeInventory(&inventory);
		return 1;
	}

	setCleanupData(&shared_data, &config, &inventory);

	if (startServer(&shared_data, config.ip, config.port) != 0) {
		writeString("Error: Could not start server\n");
		shutdownMaester(&shared_data, &config, &inventory, &envoy_pool);
		return 1;
	}

	runTerminalLoop(&config, &inventory, &shared_data);

	shutdownMaester(&shared_data, &config, &inventory, &envoy_pool);
	return 0;
}
