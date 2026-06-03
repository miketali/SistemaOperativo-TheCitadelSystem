/***********************************************
*
* @Proposit: Modul de routing - identifica el destinatari d'una trama
*             i decideix entre processar-la localment o reenviar-la a
*             traves d'un hop. El processat per tipus de trama esta en
*             frame_handlers.c.
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "router.h"

/***********************************************
*
* @Finalitat: Verifica si una trama es per a aquest regne (case insensitive)
* @Parametres: in: frame = punter a la trama
*              in: config = configuracio del Maester
* @Retorn: 1 si es per a aquest regne, 0 si no
*
************************************************/
int isForMe(Frame *frame, MaesterConfig *config) {
	int i;
	char *dest;

	dest = frame->destination;

	//comparar destination con realm_name (case insensitive)
	for (i = 0; dest[i] != '\0' && config->realm_name[i] != '\0'; i++) {
		char c1;
		char c2;

		c1 = dest[i];
		c2 = config->realm_name[i];

		if (c1 >= 'A' && c1 <= 'Z') {
			c1 = c1 + 32;
		}
		if (c2 >= 'A' && c2 <= 'Z') {
			c2 = c2 + 32;
		}

		if (c1 != c2) {
			return 0;
		}
	}

	if (dest[i] == '\0' && config->realm_name[i] == '\0') {
		return 1;
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Envia una trama NACK (0x69) al client
* @Parametres: in: fd_client = socket del client
*              in: realm_name = nom del propi regne
* @Retorn: 0 ok, -1 error
*
************************************************/
int sendNACK(int fd_client, const char *realm_name) {
	Frame nack_frame;
	char datos[FRAME_SIZE];

	if (createFrame(FRAME_TYPE_NACK, "", "", realm_name ? realm_name : "", &nack_frame) != 0) {
		return -1;
	}

	if (serializeFrame(&nack_frame, datos) != 0) {
		return -1;
	}

	if (write(fd_client, datos, FRAME_SIZE) != FRAME_SIZE) {
		return -1;
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Dispatcher de tramas rebudes que son per aquest regne.
*             Selecciona el handler segons el TYPE; cada cas viu a
*             frame_handlers.c. La unica logica aqui es el switch.
* @Parametres: in: frame = trama rebuda (ja validada)
*              in/out: shared = dades compartides
*              in: fd_client = socket del client
* @Retorn: ----
*
************************************************/
void processFrame(Frame *frame, SharedData *shared, int fd_client) {
	switch (frame->type) {
		case FRAME_TYPE_ALLIANCE_REQUEST:        //0x01
			handleAllianceRequest(frame, shared, fd_client);
			break;

		case FRAME_TYPE_PRODUCT_LIST:            //0x11 (compat. Fase 2)
		case FRAME_TYPE_PRODUCTS_LIST_REQUEST:   //0x12 (Fase 3)
			handleProductListRequest(frame, shared, fd_client);
			break;

		case FRAME_TYPE_TRADE_REQUEST:           //0x14
			handleTradeRequestHeader(frame, shared, fd_client);
			break;

		case FRAME_TYPE_TRADE_ORDER:             //0x15
			handleTradeOrder(frame, shared, fd_client);
			break;

		case FRAME_TYPE_DISCONNECT:              //0x27
			handleDisconnect(frame, shared);
			break;

		case FRAME_TYPE_NACK:                    //0x69
			//cap accio - el remitent ja sap que la peticio fallida
			break;

		case FRAME_TYPE_ALLIANCE_RESPONSE:       //0x03
			handleAllianceResponse(frame, shared, fd_client);
			break;

		default:
			sendNACK(fd_client, shared->config->realm_name);
			break;
	}
}

/***********************************************
*
* @Finalitat: Cerca una ruta en la taula. Si no troba el regne especific,
*             retorna la ruta DEFAULT si existeix.
* @Parametres: in: realm_name = nom del regne
*              in: config = configuracio
* @Retorn: punter a la Route o NULL si no es troba ni hi ha DEFAULT
*
************************************************/
Route* findRoute(const char *realm_name, MaesterConfig *config) {
	int i;
	Route *default_route;
	Route *exact_match;

	default_route = NULL;
	exact_match = NULL;

	for (i = 0; i < config->num_routes; i++) {
		if (strcasecmpCustom(config->routes[i].name, realm_name) == 0) {
			exact_match = &config->routes[i];
		}
		if (strcasecmpCustom(config->routes[i].name, "DEFAULT") == 0) {
			default_route = &config->routes[i];
		}
	}

	//Si hi ha match exacte i NO es wildcard, usar la ruta directa
	if (exact_match && !isWildcardRoute(exact_match)) {
		return exact_match;
	}

	//En altre cas (no trobat o wildcard), usar DEFAULT (NULL si no hi ha)
	return default_route;
}

/***********************************************
*
* @Finalitat: Cerca el nom del regne a partir del seu IP:Port
* @Parametres: in: origin = "IP:Port"
*              in: config = configuracio
* @Retorn: nom del regne o NULL si no es troba
*
************************************************/
const char* findRealmNameByOrigin(const char *origin, MaesterConfig *config) {
	int i;
	char route_origin[50];

	for (i = 0; i < config->num_routes; i++) {
		snprintf(route_origin, sizeof(route_origin), "%s:%d",
		         config->routes[i].ip, config->routes[i].port);
		if (strcmp(route_origin, origin) == 0) {
			return config->routes[i].name;
		}
	}

	return NULL;
}

/***********************************************
*
* @Finalitat: Envia una trama d'error (0x21, 0x25, ...) al origen
* @Parametres: in: origin = "IP:Port" del receptor
*              in: error_type = tipus de trama d'error
*              in: error_msg = missatge a posar al DATA
*              in: config = configuracio (per el my_origin)
* @Retorn: ----
*
************************************************/
void sendErrorToOrigin(const char *origin, uint8_t error_type, const char *error_msg, MaesterConfig *config) {
	char *origin_ip;
	int origin_port;
	int fd;
	Frame error_frame;
	char buffer[FRAME_SIZE];
	int i;
	int j;
	int ip_len;
	char my_origin[50];
	const char *dest_realm;

	//parsear origin "IP:Port" - encontrar posición del ':'
	i = 0;
	while (origin[i] != ':' && origin[i] != '\0') {
		i++;
	}

	if (origin[i] != ':') {
		return;  //formato invalido
	}

	ip_len = i;
	origin_ip = malloc((ip_len + 1) * sizeof(char));
	if (!origin_ip) {
		return;
	}

	for (j = 0; j < ip_len; j++) {
		origin_ip[j] = origin[j];
	}
	origin_ip[ip_len] = '\0';

	//parsear puerto
	i++;  //saltar ':'
	origin_port = 0;
	j = i;
	while (origin[j] != '\0' && origin[j] >= '0' && origin[j] <= '9') {
		origin_port = origin_port * 10 + (origin[j] - '0');
		j++;
	}

	fd = openClientConn(origin_ip, origin_port);
	if (fd < 0) {
		free(origin_ip);
		return;
	}

	snprintf(my_origin, sizeof(my_origin), "%s:%d", config->ip, config->port);

	dest_realm = findRealmNameByOrigin(origin, config);

	createFrame(error_type, my_origin, dest_realm ? dest_realm : "", error_msg, &error_frame);
	serializeFrame(&error_frame, buffer);

	write(fd, buffer, FRAME_SIZE);
	close(fd);

	free(origin_ip);
}

/***********************************************
*
* @Finalitat: Retorna el nom textual associat al tipus de trama
* @Parametres: in: type = identificador del tipus de trama
* @Retorn: cadena constant amb el nom del tipus
*
************************************************/
static const char* frameTypeName(uint8_t type) {
	switch (type) {
		case FRAME_TYPE_ALLIANCE_REQUEST:  return "PLEDGE";
		case FRAME_TYPE_ALLIANCE_RESPONSE: return "PLEDGE_RESPONSE";
		case FRAME_TYPE_PRODUCT_LIST:      return "LIST_PRODUCTS";
		case FRAME_TYPE_TRADE_REQUEST:     return "TRADE";
		case FRAME_TYPE_DISCONNECT:        return "DISCONNECT";
		default:                           return "FRAME";
	}
}

/***********************************************
*
* @Finalitat: Envia el missatge d'error UNKNOWN_REALM a l'origen de la trama
* @Parametres: in: frame = trama original (per llegir origin/destination)
*              in: config = configuracio (per enviar des d'aquest regne)
* @Retorn: ----
*
************************************************/
static void notifyUnknownRealm(Frame *frame, MaesterConfig *config) {
	char *error_data = NULL;

	asprintf(&error_data, "UNKNOWN_REALM&%s", frame->destination);
	sendErrorToOrigin(frame->origin, FRAME_TYPE_ERROR_UNKNOWN_REALM, error_data, config);
	free(error_data);
}

/***********************************************
*
* @Finalitat: Serialitza la trama i la envia pel socket del hop seguent
* @Parametres: in: frame, in: fd_next
* @Retorn: 0 si exit, -1 si error
*
************************************************/
static int forwardInitialFrame(Frame *frame, int fd_next) {
	char buffer[FRAME_SIZE];

	if (serializeFrame(frame, buffer) != 0) {
		return -1;
	}
	if (write(fd_next, buffer, FRAME_SIZE) != FRAME_SIZE) {
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Bucle de relay TCP bidireccional entre dos sockets
* @Parametres: in: fd_client, in: fd_next
* @Retorn: ----
*
************************************************/
static void runTcpRelay(int fd_client, int fd_next) {
	fd_set read_fds;
	int max_fd;
	int bytes_read;
	char relay_buffer[4096];
	int done;

	max_fd = (fd_client > fd_next) ? fd_client : fd_next;
	done = 0;
	while (!done) {
		FD_ZERO(&read_fds);
		FD_SET(fd_client, &read_fds);
		FD_SET(fd_next, &read_fds);

		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
			done = 1;
		} else {
			if (FD_ISSET(fd_client, &read_fds)) {
				bytes_read = read(fd_client, relay_buffer, sizeof(relay_buffer));
				if (bytes_read <= 0) {
					done = 1;
				} else if (write(fd_next, relay_buffer, bytes_read) != bytes_read) {
					done = 1;
				}
			}
			if (!done && FD_ISSET(fd_next, &read_fds)) {
				bytes_read = read(fd_next, relay_buffer, sizeof(relay_buffer));
				if (bytes_read <= 0) {
					done = 1;
				} else if (write(fd_client, relay_buffer, bytes_read) != bytes_read) {
					done = 1;
				}
			}
		}
	}
}

/***********************************************
*
* @Finalitat: Reenvia una trama a traves d'un hop intermedi.
*             Despres del primer write, actua com a relay TCP bidireccional
*             entre el client original i el seguent hop fins que un dels
*             dos extrems tanqui la connexio.
* @Parametres: in: frame = trama a reenviar
*              in: config = configuracio (taula de rutes)
*              in: fd_client = socket del client original
* @Retorn: 1 si fd_client ja ha sigut tancat pel relay, 0 si no
*
************************************************/
int forwardFrame(Frame *frame, MaesterConfig *config, int fd_client) {
	Route *route;
	int fd_next;
	char *msg;

	asprintf(&msg, "Raven relay: %s -> %s (%s)\n", frame->origin, frame->destination,
	         frameTypeName(frame->type));
	writeString(msg);
	free(msg);

	route = findRoute(frame->destination, config);
	if (!route) {
		notifyUnknownRealm(frame, config);
		return 0;
	}

	asprintf(&msg, "Forwarding to %s via %s:%d\n", frame->destination, route->ip, route->port);
	writeString(msg);
	free(msg);

	//simular tiempo de viaje del hop (5 segundos)
	sleep(5);

	fd_next = openClientConn(route->ip, route->port);
	if (fd_next < 0) {
		notifyUnknownRealm(frame, config);
		return 0;
	}

	if (forwardInitialFrame(frame, fd_next) < 0) {
		close(fd_next);
		return 0;
	}

	runTcpRelay(fd_client, fd_next);

	close(fd_next);
	close(fd_client);
	return 1;  //fd_client fue cerrado por el relay
}
