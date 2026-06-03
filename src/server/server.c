/***********************************************
*
* @Proposit: Servidor TCP multi-threaded
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Gestiona conexiones entrantes y notificaciones
*
************************************************/
#include "server.h"

//forward declarations
void* serverThread(void *arg);
void* clientThread(void *arg);

/***********************************************
*
* @Finalitat: Inicializa datos compartidos y mutex
* @Parametres: in/out: shared, in: config, inventory
* @Retorn: ----
*
************************************************/
void initSharedData(SharedData *shared, MaesterConfig *config, Inventory *inventory) {
	shared->config = config;
	shared->inventory = inventory;
	pthread_mutex_init(&shared->config_lock, NULL);
	pthread_mutex_init(&shared->inventory_lock, NULL);

	shared->envoy_pool = NULL;
	pthread_mutex_init(&shared->envoy_lock, NULL);

	shared->in_trade_mode = 0;
	shared->notifications.count = 0;
	pthread_mutex_init(&shared->notifications.lock, NULL);
}

/***********************************************
*
* @Finalitat: Destruye los mutex
* @Parametres: in: shared
* @Retorn: ----
*
************************************************/
void cleanupSharedData(SharedData *shared) {
	pthread_mutex_destroy(&shared->config_lock);
	pthread_mutex_destroy(&shared->inventory_lock);
	pthread_mutex_destroy(&shared->envoy_lock);
	pthread_mutex_destroy(&shared->notifications.lock);
}

/***********************************************
*
* @Finalitat: Muestra o encola notificacion segun el modo actual
* @Parametres: in: shared, format, ... (varargs)
* @Retorn: 1 si encolada, 0 si mostrada
*
************************************************/
int queueOrShowNotification(SharedData *shared, const char *format, ...) {
	va_list args;
	char *message;
	int queued;

	va_start(args, format);
	if (vasprintf(&message, format, args) < 0) {
		va_end(args);
		return 0;
	}
	va_end(args);

	queued = 0;

	pthread_mutex_lock(&shared->notifications.lock);
	if (shared->in_trade_mode) {
		//estamos en trade mode, encolar notificacion
		if (shared->notifications.count < MAX_NOTIFICATIONS) {
			strncpy(shared->notifications.messages[shared->notifications.count],
			        message, MAX_NOTIFICATION_LEN - 1);
			shared->notifications.messages[shared->notifications.count][MAX_NOTIFICATION_LEN - 1] = '\0';
			shared->notifications.count++;
			queued = 1;
		}
	}
	pthread_mutex_unlock(&shared->notifications.lock);

	if (!queued) {
		//mostrar directamente
		writeString(message);
	}

	free(message);
	return queued;
}

/***********************************************
*
* @Finalitat: Muestra notificaciones encoladas
* @Parametres: in: shared
* @Retorn: ----
*
************************************************/
void flushNotifications(SharedData *shared) {
	int i;

	pthread_mutex_lock(&shared->notifications.lock);
	if (shared->notifications.count > 0) {
		writeString("\n--- Ravens arrived while you were busy ---\n");
		for (i = 0; i < shared->notifications.count; i++) {
			writeString(shared->notifications.messages[i]);
		}
		writeString("---\n\n");
		shared->notifications.count = 0;
	}
	pthread_mutex_unlock(&shared->notifications.lock);
}

/***********************************************
* @Finalitat: Activa modo trade (encola notifs)
************************************************/
void enterTradeMode(SharedData *shared) {
	pthread_mutex_lock(&shared->notifications.lock);
	shared->in_trade_mode = 1;
	pthread_mutex_unlock(&shared->notifications.lock);
}

/***********************************************
* @Finalitat: Desactiva modo trade
************************************************/
void exitTradeMode(SharedData *shared) {
	pthread_mutex_lock(&shared->notifications.lock);
	shared->in_trade_mode = 0;
	pthread_mutex_unlock(&shared->notifications.lock);
}

/***********************************************
*
* @Finalitat: Inicia thread servidor
* @Parametres: in: shared, ip, port
* @Retorn: 0 ok, -1 error
*
************************************************/
int startServer(SharedData *shared, char *ip, int port) {
	pthread_t server_tid;
	ServerThreadData *server_data;

	//crear estructura de datos para el thread servidor
	server_data = malloc(sizeof(ServerThreadData));
	if (!server_data) {
		writeString("Error: malloc failed for server_data\n");
		return -1;
	}

	server_data->ip = ip;
	server_data->port = port;
	server_data->shared = shared;

	//crear thread servidor (NO bloqueante)
	if (pthread_create(&server_tid, NULL, serverThread, server_data) != 0) {
		writeString("Error: Could not create server thread\n");
		free(server_data);
		return -1;
	}

	//detach thread para liberar recursos automaticamente
	pthread_detach(server_tid);

	return 0;
}

/***********************************************
*
* @Finalitat: Thread que atiende a un cliente
* @Parametres: in: arg = ClientThreadData*
* @Retorn: NULL
*
************************************************/
void* clientThread(void *arg) {
	ClientThreadData *data;
	char buffer[FRAME_SIZE];
	Frame frame;
	int bytes_read;

	data = (ClientThreadData *)arg;

	//leer trama (320 bytes)
	bytes_read = read(data->fd_client, buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE) {
		close(data->fd_client);
		free(data);
		return NULL;
	}

	//deserializar trama
	if (deserializeFrame(buffer, &frame) != 0) {
		sendNACK(data->fd_client, data->shared->config->realm_name);
		close(data->fd_client);
		free(data);
		return NULL;
	}

	//validar checksum
	if (!validateChecksum(&frame)) {
		sendNACK(data->fd_client, data->shared->config->realm_name);
		close(data->fd_client);
		free(data);
		return NULL;
	}

	//verificar si es para mi o hay que enrutar
	pthread_mutex_lock(&data->shared->config_lock);
	int for_me = isForMe(&frame, data->shared->config);
	pthread_mutex_unlock(&data->shared->config_lock);

	if (for_me) {
		processFrame(&frame, data->shared, data->fd_client);
		close(data->fd_client);
	} else {
		int client_closed;
		pthread_mutex_lock(&data->shared->config_lock);
		client_closed = forwardFrame(&frame, data->shared->config, data->fd_client);
		pthread_mutex_unlock(&data->shared->config_lock);
		//solo cerrar si forwardFrame no lo cerro
		if (!client_closed) {
			close(data->fd_client);
		}
	}

	free(data);
	return NULL;
}

/***********************************************
*
* @Finalitat: Thread servidor (accept loop)
* @Parametres: in: arg = ServerThreadData*
* @Retorn: NULL
*
************************************************/
void* serverThread(void *arg) {
	ServerThreadData *data;
	int fd_socket;
	int fd_client;
	pthread_t client_tid;
	ClientThreadData *client_data;

	data = (ServerThreadData *)arg;

	//abrir socket servidor
	fd_socket = openListenConn(data->ip, data->port);
	if (fd_socket < 0) {
		writeString("Error: Could not open server socket\n");
		free(data);
		return NULL;
	}

	//loop infinito aceptando conexiones
	while (1) {
		fd_client = accept(fd_socket, NULL, NULL);
		if (fd_client < 0) {
			continue;
		}

		//crear estructura de datos para el thread de cliente
		client_data = malloc(sizeof(ClientThreadData));
		if (!client_data) {
			close(fd_client);
			continue;
		}

		client_data->fd_client = fd_client;
		client_data->shared = data->shared;

		//crear thread para procesar este cliente
		if (pthread_create(&client_tid, NULL, clientThread, client_data) != 0) {
			close(fd_client);
			free(client_data);
			continue;
		}

		//detach thread para liberar recursos automaticamente
		pthread_detach(client_tid);
	}

	close(fd_socket);
	free(data);
	return NULL;
}
