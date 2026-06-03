/***********************************************
*
* @Proposit: Manejadores de señales (SIGINT, SIGCHLD, etc)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "handlers.h"

//datos globales para cleanup
static SharedData *g_shared = NULL;
static MaesterConfig *g_config = NULL;
static Inventory *g_inventory = NULL;
static EnvoyPool *g_envoy_pool = NULL;
static volatile sig_atomic_t g_shutdown_in_progress = 0;

//registra datos para cleanup en SIGINT
void setCleanupData(SharedData *shared, MaesterConfig *config, Inventory *inventory) {
	g_shared = shared;
	g_config = config;
	g_inventory = inventory;
}

void setEnvoyPool(EnvoyPool *pool) {
	g_envoy_pool = pool;
}

void setShutdownInProgress(void) {
	g_shutdown_in_progress = 1;
}

/***********************************************
*
* @Finalitat: Handler SIGCHLD - detecta Envoys caidos
* @Parametres: in: sig
* @Retorn: ----
*
************************************************/
void sigchldHandler(int sig) {
	pid_t pid;
	int status;
	int i;
	int found;
	char num;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (g_shutdown_in_progress) continue;

		if (g_envoy_pool != NULL) {
			found = 0;
			for (i = 0; i < g_envoy_pool->num_envoys && !found; i++) {
				if (g_envoy_pool->envoys[i].pid == pid) {
					g_envoy_pool->envoys[i].status = ENVOY_DEAD;
					g_envoy_pool->envoys[i].pid = -1;
					close(g_envoy_pool->envoys[i].pipe_to_envoy[PIPE_WRITE]);
					close(g_envoy_pool->envoys[i].pipe_from_envoy[PIPE_READ]);

					//async-signal-safe output
					write(STDOUT_FILENO, "\n[ERROR] Envoy ", 15);
					num = '0' + (char)(i + 1);
					write(STDOUT_FILENO, &num, 1);
					write(STDOUT_FILENO, " crashed unexpectedly!\n", 23);
					found = 1;
				}
			}
		}
	}

	//re-registrar el handler (alguns sistemes el reseten a SIG_DFL despres d'invocar-lo)
	signal(sig, sigchldHandler);
}

/***********************************************
*
* @Finalitat: Handler SIGINT (CTRL+C)
* @Parametres: in: sig
* @Retorn: ----
*
************************************************/
void signalHandler(int sig) {
	int i;
	int fd;
	char datos[FRAME_SIZE];
	Frame disconnect_frame;
	char origin[50];

	if (sig == SIGINT) {
		g_shutdown_in_progress = 1;
		writeString("\nClosing Maester...\n");

		//notificar a aliados si tenemos los datos
		if (g_config != NULL) {
			//crear origin string
			sprintf(origin, "%s:%d", g_config->ip, g_config->port);

			//enviar DISCONNECT a aliados
			for (i = 0; i < g_config->num_routes; i++) {
				if (g_config->routes[i].alliance_status == ALLIANCE_ALLIED) {
					fd = openClientConn(g_config->routes[i].ip, g_config->routes[i].port);
					if (fd >= 0) {
						createFrame(FRAME_TYPE_DISCONNECT, origin,
						           g_config->routes[i].name, "DISCONNECT",
						           &disconnect_frame);
						serializeFrame(&disconnect_frame, datos);
						write(fd, datos, FRAME_SIZE);
						close(fd);
					}
				}
			}
		}

		//liberar recursos
		if (g_shared != NULL) {
			cleanupSharedData(g_shared);
		}
		if (g_config != NULL) {
			freeConfig(g_config);
		}
		if (g_inventory != NULL) {
			freeInventory(g_inventory);
		}

		writeString("Resources freed. Goodbye.\n");
		exit(0);
	}
}

/***********************************************
* @Finalitat: Configura handlers de senyals usant signal()
************************************************/
void setupSignalHandlers(void) {
	signal(SIGINT, signalHandler);
	signal(SIGPIPE, SIG_IGN);  //ignorar SIGPIPE (pipes trencats)
	signal(SIGCHLD, sigchldHandler);
}
