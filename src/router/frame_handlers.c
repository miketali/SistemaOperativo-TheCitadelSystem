/***********************************************
*
* @Proposit: Handlers per tipus de trama entrant (un per cada TYPE)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest fitxer conte la logica especifica de cada TYPE del protocol,
* separada del dispatcher (processFrame en router.c). Cada handler te
* els seus propis recursos i no comparteix estat mes enlla del que
* viatja per SharedData.
*
************************************************/
#include "frame_handlers.h"

/***********************************************
* Helpers compartits per tots els handlers
************************************************/

/***********************************************
*
* @Finalitat: Cerca una ruta a partir del seu IP:Port de origen
* @Parametres: in: shared (cal tenir config_lock pres pel caller),
*              in: origin = "IP:Port"
* @Retorn: punter a Route o NULL si no es troba
*
************************************************/
static Route* findRouteByOriginIP(SharedData *shared, const char *origin) {
	int i;
	char *search_origin;
	Route *result = NULL;
	Route *r;

	for (i = 0; i < shared->config->num_routes && !result; i++) {
		r = &shared->config->routes[i];
		//1) comparar amb la ip:port directa de la ruta
		asprintf(&search_origin, "%s:%d", r->ip, r->port);
		if (strcmp(search_origin, origin) == 0) {
			result = r;
		}
		free(search_origin);
		//2) si no coincideix, provar amb ally_ip:ally_port. Cal per als
		//   aliats no-veins (ruta directa *.*.*.*): l'ally_ip s'apren al
		//   PLEDGE i es l'unica adreca real que tenim del regne.
		if (!result && r->ally_ip && r->ally_port > 0) {
			asprintf(&search_origin, "%s:%d", r->ally_ip, r->ally_port);
			if (strcmp(search_origin, origin) == 0) {
				result = r;
			}
			free(search_origin);
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Envia una trama ACK (0x31) amb "OK&<realm>" al client
* @Parametres: in: fd_client, in: realm_name
* @Retorn: ----
*
************************************************/
static void sendAckOk(int fd_client, const char *realm_name) {
	Frame response;
	char datos[FRAME_SIZE];
	char ack_data[100];

	snprintf(ack_data, sizeof(ack_data), "OK&%s", realm_name);
	createFrame(FRAME_TYPE_ACK, "", "", ack_data, &response);
	serializeFrame(&response, datos);
	write(fd_client, datos, FRAME_SIZE);
}

/***********************************************
*
* @Finalitat: Envia una resposta de comerç (0x16) amb cos custom
* @Parametres: in: fd_client, origin, destination, body
* @Retorn: ----
*
************************************************/
static void sendTradeResponse(int fd_client, const char *origin,
                              const char *destination, const char *body) {
	Frame response;
	char datos[FRAME_SIZE];

	createFrame(FRAME_TYPE_TRADE_RESPONSE, origin, destination, body, &response);
	serializeFrame(&response, datos);
	write(fd_client, datos, FRAME_SIZE);
}

/***********************************************
* Handler 0x01: peticio d'aliança entrant
************************************************/

/***********************************************
*
* @Finalitat: Crea una ruta dinamica a partir d'una peticio d'aliança
*             d'un regne fins ara desconegut.
* @Parametres: in: sender_realm, frame_origin, in/out: shared
* @Retorn: punter a la ruta creada o NULL si error
*
************************************************/
static Route* createDynamicRouteFromOrigin(const char *sender_realm,
                                           const char *frame_origin,
                                           SharedData *shared) {
	char origin_ip[50];
	int origin_port = 0;
	char *colon_pos;
	int ip_len;
	Route *route;

	colon_pos = strchr(frame_origin, ':');
	if (!colon_pos) return NULL;

	ip_len = (int)(colon_pos - frame_origin);
	if (ip_len > 49) ip_len = 49;
	strncpy(origin_ip, frame_origin, ip_len);
	origin_ip[ip_len] = '\0';
	origin_port = atoi(colon_pos + 1);
	if (origin_port <= 0) return NULL;

	route = addDynamicRoute(sender_realm, origin_ip, origin_port, shared->config);
	if (route) {
		queueOrShowNotification(shared,
		    "A raven arrived from unknown realm '%s' (ORIGIN %s)\n",
		    sender_realm, frame_origin);
	}
	return route;
}

/***********************************************
*
* @Finalitat: Localitza o crea la ruta del regne origen i marca PENDING
* @Parametres: in: frame, sender_realm, in/out: shared
* @Retorn: punter a Route o NULL si error
*
************************************************/
static Route* resolveAllianceSenderRoute(Frame *frame, const char *sender_realm,
                                         SharedData *shared) {
	Route *route;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByName(sender_realm, shared->config);
	if (!route) {
		route = createDynamicRouteFromOrigin(sender_realm, frame->origin, shared);
		if (!route) {
			pthread_mutex_unlock(&shared->config_lock);
			return NULL;
		}
	}

	if (route->pending_origin) {
		free(route->pending_origin);
	}
	route->pending_origin = copyStringDynamic(frame->origin);
	setAllianceStatus(route, ALLIANCE_PENDING);
	pthread_mutex_unlock(&shared->config_lock);
	return route;
}

/***********************************************
*
* @Finalitat: Llegeix la trama 0x32 amb el MD5 del sigil rebut
* @Parametres: in: fd_client, out: md5 (mida MD5_HASH_LENGTH + 1)
* @Retorn: 0 ok, -1 error
*
************************************************/
static int readSigilMD5Frame(int fd_client, char *md5) {
	char md5_buffer[FRAME_SIZE];
	Frame md5_frame;
	ssize_t bytes_read;

	bytes_read = read(fd_client, md5_buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE) return -1;
	if (deserializeFrame(md5_buffer, &md5_frame) != 0 ||
	    md5_frame.type != FRAME_TYPE_ACK_MD5SUM) return -1;
	memcpy(md5, md5_frame.data, MD5_HASH_LENGTH);
	md5[MD5_HASH_LENGTH] = '\0';
	return 0;
}

/***********************************************
*
* @Finalitat: Marca l'alianç com FAILED de forma segura amb lock
* @Parametres: in/out: shared, in/out: route
* @Retorn: ----
*
************************************************/
static void markAllianceFailed(SharedData *shared, Route *route) {
	pthread_mutex_lock(&shared->config_lock);
	setAllianceStatus(route, ALLIANCE_FAILED);
	pthread_mutex_unlock(&shared->config_lock);
}

/***********************************************
*
* @Finalitat: Rep el sigil + verifica MD5 (PASOS 2-4 del handler 0x01)
* @Parametres: in: fd_client, sender_realm, in/out: shared, in: route
* @Retorn: 0 ok, -1 error (alianç ja marcada FAILED)
*
************************************************/
static int receiveAndVerifySigil(int fd_client, const char *sender_realm,
                                 SharedData *shared, Route *route) {
	char *sigil_path;
	char received_md5[MD5_HASH_LENGTH + 1];
	int ok;

	asprintf(&sigil_path, "/tmp/sigil_%s.received", sender_realm);

	if (receiveSigilFile(fd_client, sigil_path, shared->config->realm_name) != 0) {
		markAllianceFailed(shared, route);
		free(sigil_path);
		return -1;
	}
	if (readSigilMD5Frame(fd_client, received_md5) != 0) {
		markAllianceFailed(shared, route);
		free(sigil_path);
		return -1;
	}
	ok = (verifyMD5(sigil_path, received_md5) == 1);
	sendMD5Response(fd_client, ok, shared->config->realm_name);
	if (!ok) markAllianceFailed(shared, route);
	free(sigil_path);
	return ok ? 0 : -1;
}

/***********************************************
*
* @Finalitat: Handler 0x01 - peticio d'aliança entrant
*             Rep el header, envia ACK, rep el sigil + MD5 i guarda
*             l'estat com ALLIANCE_PENDING fins que l'usuari respongui.
* @Parametres: in: frame = trama 0x01 rebuda
*              in/out: shared = dades compartides
*              in: fd_client = socket del client
* @Retorn: ----
*
************************************************/
void handleAllianceRequest(Frame *frame, SharedData *shared, int fd_client) {
	Route *route;
	char sender_realm[MAX_REALM_NAME];
	int idx = 0;

	//extraer nom del regne (abans del primer '&')
	while (frame->data[idx] != '\0' && frame->data[idx] != '&' &&
	       idx < MAX_REALM_NAME - 1) {
		sender_realm[idx] = frame->data[idx];
		idx++;
	}
	sender_realm[idx] = '\0';

	route = resolveAllianceSenderRoute(frame, sender_realm, shared);
	if (!route) {
		sendNACK(fd_client, shared->config->realm_name);
		return;
	}

	sendAckOk(fd_client, shared->config->realm_name);

	if (receiveAndVerifySigil(fd_client, sender_realm, shared, route) != 0) return;

	queueOrShowNotification(shared,
	    "\nAlliance request from %s received. Use 'PLEDGE RESPOND %s ACCEPT/REJECT'\n",
	    sender_realm, sender_realm);
}

/***********************************************
* Handler 0x11/0x12: peticio de llista de productes entrant
************************************************/

/***********************************************
*
* @Finalitat: Exporta inventari a fitxer i obte size + MD5
* @Parametres: in/out: shared, out: file_size, out: md5sum (a alliberar)
* @Retorn: products_file (a alliberar) o NULL si error
*
************************************************/
static char* prepareProductsListFile(SharedData *shared, long *file_size, char **md5sum) {
	char *products_file;
	int fd_file;

	asprintf(&products_file, "/tmp/products_%s.txt", shared->config->realm_name);

	pthread_mutex_lock(&shared->inventory_lock);
	if (exportInventoryToFile(shared->inventory, products_file) != 0) {
		pthread_mutex_unlock(&shared->inventory_lock);
		free(products_file);
		return NULL;
	}
	pthread_mutex_unlock(&shared->inventory_lock);

	fd_file = open(products_file, O_RDONLY);
	if (fd_file < 0) {
		free(products_file);
		return NULL;
	}
	*file_size = lseek(fd_file, 0, SEEK_END);
	close(fd_file);

	*md5sum = calculateMD5(products_file);
	if (!*md5sum) {
		free(products_file);
		return NULL;
	}
	return products_file;
}

/***********************************************
*
* @Finalitat: Envia header 0x12 i espera ACK del receptor
* @Parametres: in: fd_client, products_file, md5sum, file_size
*              in: dest_realm, in/out: shared
* @Retorn: 0 ok, -1 error
*
************************************************/
static int sendProductsHeaderAndAwaitAck(int fd_client, const char *origin_str,
                                         const char *dest_realm, long file_size,
                                         const char *md5sum) {
	Frame header_frame, ack_frame;
	char header_buffer[FRAME_SIZE], ack_buffer[FRAME_SIZE];
	char header_data[256];
	ssize_t ack_read;

	snprintf(header_data, sizeof(header_data), "products.txt&%ld&%s", file_size, md5sum);
	createFrame(FRAME_TYPE_PRODUCTS_LIST_REQUEST, origin_str, dest_realm,
	            header_data, &header_frame);
	serializeFrame(&header_frame, header_buffer);
	if (write(fd_client, header_buffer, FRAME_SIZE) != FRAME_SIZE) return -1;

	ack_read = read(fd_client, ack_buffer, FRAME_SIZE);
	if (ack_read != FRAME_SIZE) return -1;
	if (deserializeFrame(ack_buffer, &ack_frame) != 0 ||
	    ack_frame.type != FRAME_TYPE_ACK) return -1;
	return 0;
}

/***********************************************
*
* @Finalitat: Rep la resposta MD5 (0x32) del receptor
* @Parametres: in: fd_client
* @Retorn: 0 ok, -1 error o CHECK_KO
*
************************************************/
static int awaitProductsListMD5Response(int fd_client) {
	char md5_resp_buffer[FRAME_SIZE];
	Frame md5_resp_frame;
	ssize_t md5_read;

	md5_read = read(fd_client, md5_resp_buffer, FRAME_SIZE);
	if (md5_read != FRAME_SIZE) return -1;
	if (deserializeFrame(md5_resp_buffer, &md5_resp_frame) != 0 ||
	    md5_resp_frame.type != FRAME_TYPE_ACK_MD5SUM) return -1;
	if (strncmp(md5_resp_frame.data, "CHECK_KO", 8) == 0) return -1;
	return 0;
}

/***********************************************
*
* @Finalitat: Mira si arriba una TRADE_ORDER (0x15) per la mateixa connexio
* @Parametres: in: fd_client, in/out: shared
* @Retorn: ----
*
************************************************/
static void awaitInlineTradeOrder(int fd_client, SharedData *shared) {
	char order_buffer[FRAME_SIZE];
	Frame order_frame;
	ssize_t order_read;

	order_read = read(fd_client, order_buffer, FRAME_SIZE);
	if (order_read != FRAME_SIZE) return;
	if (deserializeFrame(order_buffer, &order_frame) == 0 &&
	    validateChecksum(&order_frame)) {
		processFrame(&order_frame, shared, fd_client);
	} else {
		sendNACK(fd_client, shared->config->realm_name);
	}
}

/***********************************************
*
* @Finalitat: Comprova que el sol·licitant tingui alianç ACTIVA
* @Parametres: in: requester_name, in/out: shared, in: frame_origin, fd_client
* @Retorn: 0 ok, -1 error (auth error ja enviat)
*
************************************************/
static int checkRequesterAuth(const char *requester_name, SharedData *shared,
                              const char *frame_origin) {
	Route *route;
	char *auth_error;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByName(requester_name, shared->config);
	if (!route || route->alliance_status != ALLIANCE_ALLIED) {
		asprintf(&auth_error, "AUTH&%s", requester_name);
		pthread_mutex_unlock(&shared->config_lock);
		sendErrorToOrigin(frame_origin, FRAME_TYPE_ERROR_UNAUTHORIZED,
		                  auth_error, shared->config);
		free(auth_error);
		return -1;
	}
	pthread_mutex_unlock(&shared->config_lock);
	return 0;
}

/***********************************************
*
* @Finalitat: Handler 0x11/0x12 - peticio de llista de productes
* @Parametres: in: frame, in/out: shared, in: fd_client
* @Retorn: ----
*
************************************************/
void handleProductListRequest(Frame *frame, SharedData *shared, int fd_client) {
    char *products_file;
    char *md5sum = NULL;
    char *origin_str;
    long file_size;
    char *msg;
    Route *route;
    const char *requester_name;

    if (checkRequesterAuth(frame->data, shared, frame->origin) != 0) return;
    pthread_mutex_lock(&shared->config_lock);
    route = findRouteByOriginIP(shared, frame->origin);
    requester_name = route ? route->name : findRealmNameByOrigin(frame->origin, shared->config);
    
    if (!requester_name) {
        requester_name = frame->origin;
    }

    asprintf(&msg, ">>> LIST PRODUCTS request from %s.\n", requester_name);
    pthread_mutex_unlock(&shared->config_lock); 
    
    writeString(msg);
    free(msg);

    writeString("Sending product list.\n");
    products_file = prepareProductsListFile(shared, &file_size, &md5sum);
    if (!products_file) {
        sendNACK(fd_client, shared->config->realm_name);
        return;
    }
    asprintf(&origin_str, "%s:%d", shared->config->ip, shared->config->port);
    if (sendProductsHeaderAndAwaitAck(fd_client, origin_str, frame->data,
                                      file_size, md5sum) != 0) {
        free(products_file); free(origin_str); free(md5sum);
        return;
    }
    free(md5sum);
    if (sendProductsList(fd_client, products_file, origin_str, frame->data) != 0) {
        free(products_file); free(origin_str);
        return;
    }
    if (awaitProductsListMD5Response(fd_client) != 0) {
        unlink(products_file); free(products_file); free(origin_str);
        return;
    }
    unlink(products_file); free(products_file); free(origin_str);
    writeString("Products delivered.\n\n");
    awaitInlineTradeOrder(fd_client, shared);
}

/***********************************************
* Handler 0x14: header de comanda de comerç
************************************************/

/***********************************************
*
* @Finalitat: Comprova auth d'una comanda 0x14 (recerca per IP:Port)
* @Parametres: in: frame, in/out: shared
* @Retorn: 0 ok, -1 error (auth error ja enviat)
*
************************************************/
static int checkTradeRequestAuth(Frame *frame, SharedData *shared) {
	Route *route;
	const char *requester_name;
	char *auth_error;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByOriginIP(shared, frame->origin);
	if (!route || route->alliance_status != ALLIANCE_ALLIED) {
		requester_name = route ? route->name :
		    findRealmNameByOrigin(frame->origin, shared->config);
		asprintf(&auth_error, "AUTH&%s", requester_name ? requester_name : "UNKNOWN");
		pthread_mutex_unlock(&shared->config_lock);
		sendErrorToOrigin(frame->origin, FRAME_TYPE_ERROR_UNAUTHORIZED,
		                  auth_error, shared->config);
		free(auth_error);
		return -1;
	}
	pthread_mutex_unlock(&shared->config_lock);
	return 0;
}

/***********************************************
*
* @Finalitat: Extreu el MD5 del DATA del header (format: name&size&md5)
* @Parametres: in: data
* @Retorn: string MD5 a alliberar, NULL si format invalid
*
************************************************/
static char* extractOrderHeaderMD5(const char *data) {
	char *first_amp, *second_amp, *md5;

	first_amp = strchr(data, '&');
	if (!first_amp) return NULL;
	second_amp = strchr(first_amp + 1, '&');
	if (!second_amp) return NULL;
	md5 = malloc(MD5_HASH_LENGTH + 1);
	if (!md5) return NULL;
	strncpy(md5, second_amp + 1, MD5_HASH_LENGTH);
	md5[MD5_HASH_LENGTH] = '\0';
	return md5;
}

/***********************************************
*
* @Finalitat: Llegeix la trama 0x15 esperada despres del header 0x14
* @Parametres: in: fd_client, out: order_frame
* @Retorn: 0 ok, -1 error (NACK ja enviat)
*
************************************************/
static int readTradeOrderFrame(int fd_client, Frame *order_frame, const char *realm_name) {
	char order_buffer[FRAME_SIZE];
	ssize_t order_read;

	order_read = read(fd_client, order_buffer, FRAME_SIZE);
	if (order_read != FRAME_SIZE) return -1;

	if (deserializeFrame(order_buffer, order_frame) != 0 ||
	    !validateChecksum(order_frame) ||
	    order_frame->type != FRAME_TYPE_TRADE_ORDER) {
		sendNACK(fd_client, realm_name);
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Verifica el MD5 d'una comanda rebuda escribint-la a /tmp
* @Parametres: in: fd_client, order_frame, header_md5, my_origin, in/out: shared
* @Retorn: 0 ok, -1 mismatch (REJECT ja enviat)
*
************************************************/
static int verifyOrderMD5(int fd_client, Frame *order_frame, const char *header_md5,
                          const char *my_origin, SharedData *shared) {
	char *order_file;
	int fd_order, ok;

	asprintf(&order_file, "/tmp/received_order_%d.txt", fd_client);
	fd_order = open(order_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_order >= 0) {
		write(fd_order, order_frame->data, order_frame->data_length);
		close(fd_order);
	}

	ok = (header_md5 && verifyMD5(order_file, header_md5) == 1);
	sendMD5Response(fd_client, ok, shared->config->realm_name);
	if (!ok) {
		sendTradeResponse(fd_client, my_origin, order_frame->destination,
		                  "REJECT&MD5_MISMATCH");
	}
	unlink(order_file);
	free(order_file);
	return ok ? 0 : -1;
}

/***********************************************
*
* @Finalitat: Handler 0x14 - header de comanda + dades + verificacio MD5
* @Parametres: in: frame, in/out: shared, in: fd_client
* @Retorn: ----
*
************************************************/
void handleTradeRequestHeader(Frame *frame, SharedData *shared, int fd_client) {
    char *header_md5;
    char *my_origin;
    Frame order_frame;
    char *msg;
    Route *route;
    const char *requester_name;

    if (checkTradeRequestAuth(frame, shared) != 0) return;
    sendAckOk(fd_client, shared->config->realm_name);
    header_md5 = extractOrderHeaderMD5(frame->data);
    asprintf(&my_origin, "%s:%d", shared->config->ip, shared->config->port);
    if (readTradeOrderFrame(fd_client, &order_frame, shared->config->realm_name) != 0) {
        if (header_md5) free(header_md5);
        free(my_origin);
        return;
    }

    if (verifyOrderMD5(fd_client, &order_frame, header_md5, my_origin, shared) == 0) {
        pthread_mutex_lock(&shared->config_lock);
        route = findRouteByOriginIP(shared, frame->origin);
        requester_name = route ? route->name : findRealmNameByOrigin(frame->origin, shared->config);
        
        if (!requester_name) {
            requester_name = frame->origin;
        }
        asprintf(&msg, ">>> Trade request received from %s.\n", requester_name);
        pthread_mutex_unlock(&shared->config_lock);
        writeString(msg);
        free(msg);
        processFrame(&order_frame, shared, fd_client);
        writeString("Order processed successfully. Stock updated.\n\n");
    }

    if (header_md5) free(header_md5);
    free(my_origin);
}

/***********************************************
* Handler 0x15: dades de la comanda de comerç
************************************************/

/***********************************************
*
* @Finalitat: Parseja una linia "name|qty" treient nom i quantitat
* @Parametres: in: line_start, line_end (apunta a '\n' o '\0')
*              out: product_name (mida MAX_PRODUCT_NAME), out: quantity
* @Retorn: 0 ok, -1 format invalid
*
************************************************/
static int parseOrderLine(char *line_start, char *line_end,
                          char *product_name, int *quantity) {
	char *sep;
	int name_len;
	char saved;

	if (line_end <= line_start) return -1;
	saved = *line_end;
	*line_end = '\0';

	sep = line_start;
	while (*sep != '|' && *sep != '\0') sep++;
	if (*sep != '|') {
		*line_end = saved;
		return -1;
	}
	*sep = '\0';
	name_len = strlen(line_start);
	if (name_len >= MAX_PRODUCT_NAME) name_len = MAX_PRODUCT_NAME - 1;
	memcpy(product_name, line_start, name_len);
	product_name[name_len] = '\0';
	*quantity = atoi(sep + 1);
	*sep = '|';
	*line_end = saved;
	return 0;
}

/***********************************************
*
* @Finalitat: Primer pass de la comanda: verifica stock i producte
* @Parametres: in: order_copy, in: inventory, out: error_type
*              (0=ok, 1=unknown, 2=out_of_stock)
* @Retorn: 1 si tot ok, 0 si error
*
************************************************/
static int validateOrderStock(char *order_copy, const Inventory *inventory,
                              int *error_type) {
	char *line_start = order_copy;
	char *line_end;
	char product_name[MAX_PRODUCT_NAME];
	int quantity;
	Product *product;

	*error_type = 0;
	while (*line_start != '\0') {
		line_end = line_start;
		while (*line_end != '\n' && *line_end != '\0') line_end++;
		if (parseOrderLine(line_start, line_end, product_name, &quantity) == 0) {
			product = findProduct(inventory, product_name);
			if (!product) { *error_type = 1; return 0; }
			if (product->amount < quantity) { *error_type = 2; return 0; }
		}
		if (*line_end != '\n') break;
		line_start = line_end + 1;
	}
	return 1;
}

/***********************************************
*
* @Finalitat: Segon pass de la comanda: descompta stock
* @Parametres: in: order_copy, in/out: inventory
* @Retorn: ----
*
************************************************/
static void applyOrderToStock(char *order_copy, Inventory *inventory) {
	char *line_start = order_copy;
	char *line_end;
	char product_name[MAX_PRODUCT_NAME];
	int quantity;

	while (*line_start != '\0') {
		line_end = line_start;
		while (*line_end != '\n' && *line_end != '\0') line_end++;
		if (parseOrderLine(line_start, line_end, product_name, &quantity) == 0) {
			updateProductStock(inventory, product_name, -quantity);
		}
		if (*line_end != '\n') break;
		line_start = line_end + 1;
	}
}

/***********************************************
*
* @Finalitat: Cerca el route del sol·licitant per la trama 0x15
* @Parametres: in: frame, in/out: shared, in: fd_client
* @Retorn: 0 si ALLIED, -1 si no (REJECT ja enviat)
*
************************************************/
static int checkTradeOrderAuth(Frame *frame, SharedData *shared, int fd_client) {
	Route *route;
	char *err_origin;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByOriginIP(shared, frame->origin);
	if (!route || route->alliance_status != ALLIANCE_ALLIED) {
		asprintf(&err_origin, "%s:%d", shared->config->ip, shared->config->port);
		pthread_mutex_unlock(&shared->config_lock);
		sendTradeResponse(fd_client, err_origin, frame->destination, "REJECT&NOT_ALLIED");
		free(err_origin);
		return -1;
	}
	pthread_mutex_unlock(&shared->config_lock);
	return 0;
}

/***********************************************
*
* @Finalitat: Handler 0x15 - dades de la comanda de comerç
* @Parametres: in: frame, in/out: shared, in: fd_client
* @Retorn: ----
*
************************************************/
void handleTradeOrder(Frame *frame, SharedData *shared, int fd_client) {
	char *order_copy;
	char *my_origin;
	int error_type;
	int all_ok;
	const char *reject_body;

	if (checkTradeOrderAuth(frame, shared, fd_client) != 0) return;

	asprintf(&my_origin, "%s:%d", shared->config->ip, shared->config->port);
	order_copy = copyStringDynamic(frame->data);
	if (!order_copy) {
		sendTradeResponse(fd_client, my_origin, frame->destination, "REJECT&SERVER_ERROR");
		free(my_origin);
		return;
	}

	pthread_mutex_lock(&shared->inventory_lock);
	all_ok = validateOrderStock(order_copy, shared->inventory, &error_type);
	if (!all_ok) {
		pthread_mutex_unlock(&shared->inventory_lock);
		reject_body = (error_type == 1) ? "REJECT&UNKNOWN_PRODUCT" : "REJECT&OUT_OF_STOCK";
		sendTradeResponse(fd_client, my_origin, frame->destination, reject_body);
		free(order_copy);
		free(my_origin);
		return;
	}

	applyOrderToStock(order_copy, shared->inventory);
	saveInventory(shared->config->stock_file, shared->inventory);
	pthread_mutex_unlock(&shared->inventory_lock);

	sendTradeResponse(fd_client, my_origin, frame->destination, "OK");
	free(order_copy);
	free(my_origin);
}

/***********************************************
* Handler 0x27: desconnexio
************************************************/

/***********************************************
*
* @Finalitat: Handler 0x27 - notificacio de desconnexio d'un aliat
* @Parametres: in: frame, in/out: shared
* @Retorn: ----
*
************************************************/
void handleDisconnect(Frame *frame, SharedData *shared) {
	Route *route;
	char *route_name = NULL;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByOriginIP(shared, frame->origin);
	if (route) {
		setAllianceStatus(route, ALLIANCE_NONE);
		if (route->pending_origin) {
			free(route->pending_origin);
			route->pending_origin = NULL;
		}
		route_name = route->name;
	}
	pthread_mutex_unlock(&shared->config_lock);

	if (route_name) {
		queueOrShowNotification(shared, "%s has departed.\n", route_name);
	}
}

/***********************************************
* Handler 0x03: resposta a una nostra peticio d'alianç
************************************************/

/***********************************************
*
* @Finalitat: Parseja DATA "ACCEPT&realm" / "REJECT&realm" / "TIMEOUT&realm"
* @Parametres: in: data, out: response_type (mida 16), out: sender_realm
* @Retorn: ----
*
************************************************/
static void parseAllianceResponseData(const char *data, char *response_type,
                                      char *sender_realm) {
	char *separator;
	int response_len;

	separator = strchr(data, '&');
	if (separator) {
		response_len = (int)(separator - data);
		if (response_len > 15) response_len = 15;
		strncpy(response_type, data, response_len);
		response_type[response_len] = '\0';
		copiarString(sender_realm, separator + 1, MAX_REALM_NAME);
	} else {
		copiarString(sender_realm, data, MAX_REALM_NAME);
		response_type[0] = '\0';
	}
}

/***********************************************
*
* @Finalitat: Gestiona el cas especial TIMEOUT (cancel·la PENDING local)
* @Parametres: in: sender_realm, in/out: shared
* @Retorn: 1 si processat (caller ha de tornar), 0 si no es timeout
*
************************************************/
static int handleTimeoutResponse(const char *sender_realm, SharedData *shared) {
	Route *route;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByName(sender_realm, shared->config);
	if (route && route->alliance_status == ALLIANCE_PENDING) {
		setAllianceStatus(route, ALLIANCE_NONE);
		if (route->pending_origin) {
			free(route->pending_origin);
			route->pending_origin = NULL;
		}
		pthread_mutex_unlock(&shared->config_lock);
		queueOrShowNotification(shared,
		    ">>> Alliance request from %s was cancelled.\n", sender_realm);
		return 1;
	}
	pthread_mutex_unlock(&shared->config_lock);
	return 1;  //tambe processat: timeout sense PENDING local
}

/***********************************************
*
* @Finalitat: Aplica el resultat ACCEPT/REJECT a l'estat d'alianç
* @Parametres: in: sender_realm, frame_origin, is_accept, in/out: shared
* @Retorn: punter a route (o NULL) per a missatge final
*
************************************************/
static Route* applyAllianceResponse(const char *sender_realm, const char *frame_origin,
                                    int is_accept, SharedData *shared) {
	Route *route;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByName(sender_realm, shared->config);
	if (route) {
		if (is_accept) {
			setAllianceStatus(route, ALLIANCE_ALLIED);
			setAllyDirectConnection(route, frame_origin);
		} else {
			setAllianceStatus(route, ALLIANCE_FAILED);
		}
	}
	pthread_mutex_unlock(&shared->config_lock);
	return route;
}

/***********************************************
*
* @Finalitat: Mostra la notificacio final segons resultat
* @Parametres: in: route, sender_realm, is_accept, in/out: shared
* @Retorn: ----
*
************************************************/
static void notifyAllianceOutcome(Route *route, const char *sender_realm,
                                  int is_accept, SharedData *shared) {
	if (!route) {
		queueOrShowNotification(shared,
		    "\n>>> Response from unknown realm: %s\n", sender_realm);
		return;
	}
	if (is_accept) {
		queueOrShowNotification(shared,
		    "\n>>> Alliance with %s forged successfully!\n", sender_realm);
	} else {
		queueOrShowNotification(shared,
		    "\n>>> %s has rejected our pledge.\n", sender_realm);
	}
}

/***********************************************
*
* @Finalitat: Handler 0x03 - resposta a una nostra peticio d'alianç
* @Parametres: in: frame, in/out: shared, in: fd_client
* @Retorn: ----
*
************************************************/
void handleAllianceResponse(Frame *frame, SharedData *shared, int fd_client) {
	char sender_realm[MAX_REALM_NAME];
	char response_type[16];
	int envoy_id;
	int is_accept;
	Route *route;

	parseAllianceResponseData(frame->data, response_type, sender_realm);

	if (strcmp(response_type, "TIMEOUT") == 0) {
		handleTimeoutResponse(sender_realm, shared);
		return;
	}

	is_accept = (strcmp(response_type, "ACCEPT") == 0);

	pthread_mutex_lock(&shared->envoy_lock);
	envoy_id = findEnvoyAwaitingResponseFrom(shared->envoy_pool, sender_realm);
	pthread_mutex_unlock(&shared->envoy_lock);

	if (envoy_id < 0) {
		queueOrShowNotification(shared, "Response from %s arrived too late.\n", sender_realm);
		sendNACK(fd_client, shared->config->realm_name);
		return;
	}

	sendAckOk(fd_client, shared->config->realm_name);

	route = applyAllianceResponse(sender_realm, frame->origin, is_accept, shared);

	pthread_mutex_lock(&shared->envoy_lock);
	notifyAllianceResult(shared->envoy_pool, envoy_id, is_accept,
	                     is_accept ? "Alliance accepted" : "Alliance rejected");
	pthread_mutex_unlock(&shared->envoy_lock);

	notifyAllianceOutcome(route, sender_realm, is_accept, shared);
}
