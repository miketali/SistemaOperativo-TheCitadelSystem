/***********************************************
*
* @Proposit: Comandos LIST PRODUCTS <regne> i START TRADE <regne>
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest modul implementa el flux d'alt nivell de comerç entre Maesters.
* Delega operacions auxiliars a moduls especialitzats:
*   - products_cache.c: per memoritzar els inventaris remots
*   - trade_manifest.c: per gestionar la llista de la compra
*   - protocol.c, filetransfer.c: per la comunicacio via xarxa
*   - envoy.h: per reservar i alliberar Envoys
*
************************************************/
#include "trade.h"

/***********************************************
* Helpers comuns
************************************************/

/***********************************************
*
* @Finalitat: Allibera un Envoy reservat (no-op si envoy_id < 0)
* @Parametres: in/out: shared, in: envoy_id
* @Retorn: ----
*
************************************************/
static void releaseEnvoyLocked(SharedData *shared, int envoy_id) {
	if (envoy_id < 0) return;
	pthread_mutex_lock(&shared->envoy_lock);
	releaseEnvoy(shared->envoy_pool, envoy_id);
	pthread_mutex_unlock(&shared->envoy_lock);
}

/***********************************************
*
* @Finalitat: Valida que el regne existeixi i estigui ALLIED
* @Parametres: in: realm, in/out: shared, out: out_route, in: forbidden_msg
* @Retorn: 0 ok, -1 error (ja s'ha imprimit missatge)
*
************************************************/
static int validateAlliedRoute(const char *realm, SharedData *shared,
                               Route **out_route, const char *forbidden_msg) {
	Route *route;
	char *msg;

	pthread_mutex_lock(&shared->config_lock);
	route = findRouteByName(realm, shared->config);
	if (!route) {
		pthread_mutex_unlock(&shared->config_lock);
		asprintf(&msg, "The realm of %s is unknown to us.\n", realm);
		writeString(msg);
		free(msg);
		return -1;
	}
	if (route->alliance_status != ALLIANCE_ALLIED) {
		pthread_mutex_unlock(&shared->config_lock);
		asprintf(&msg, forbidden_msg, realm);
		writeString(msg);
		free(msg);
		return -1;
	}
	pthread_mutex_unlock(&shared->config_lock);
	*out_route = route;
	return 0;
}

/***********************************************
*
* @Finalitat: Reserva un Envoy lliure per a una mision TRADE
* @Parametres: in/out: shared, in: realm
* @Retorn: envoy_id reservat, -1 si error (ja s'ha imprimit missatge)
*
************************************************/
static int reserveEnvoyForRealm(SharedData *shared, const char *realm) {
	int envoy_id;

	pthread_mutex_lock(&shared->envoy_lock);
	if (!shared->envoy_pool) {
		pthread_mutex_unlock(&shared->envoy_lock);
		writeString("Error: Envoy pool not initialized\n");
		return -1;
	}
	envoy_id = findFreeEnvoy(shared->envoy_pool);
	if (envoy_id < 0) {
		pthread_mutex_unlock(&shared->envoy_lock);
		writeString("All envoys are occupied. Your command must wait.\n");
		return -1;
	}
	if (reserveEnvoy(shared->envoy_pool, envoy_id, MISSION_TRADE, realm) != 0) {
		pthread_mutex_unlock(&shared->envoy_lock);
		writeString("Error: Could not reserve envoy\n");
		return -1;
	}
	pthread_mutex_unlock(&shared->envoy_lock);
	return envoy_id;
}

/***********************************************
*
* @Finalitat: Obre connexio TCP al aliat i prepara l'origin
* @Parametres: in: route, in/out: shared
*              out: fd_socket, origin (a alliberar pel caller)
* @Retorn: 0 ok, -1 error
*
************************************************/
static int openAllyConnection(Route *route, SharedData *shared,
                              int *fd_socket, char **origin) {
	char *msg;

	pthread_mutex_lock(&shared->config_lock);
	if (route->ally_ip && route->ally_port > 0) {
		*fd_socket = openClientConn(route->ally_ip, route->ally_port);
	} else {
		*fd_socket = openClientConn(route->ip, route->port);
	}
	if (*fd_socket < 0) {
		pthread_mutex_unlock(&shared->config_lock);
		asprintf(&msg, "Our ravens cannot reach %s.\n", route->name);
		writeString(msg);
		free(msg);
		return -1;
	}
	*origin = createOriginString(shared->config->ip, shared->config->port);
	pthread_mutex_unlock(&shared->config_lock);
	if (!*origin) {
		writeString("Failed to prepare the message.\n");
		close(*fd_socket);
		return -1;
	}
	return 0;
}

/***********************************************
* Flux trade: envia comanda al aliat
************************************************/

/***********************************************
*
* @Finalitat: Escriu el contingut de la comanda a un fitxer temporal
* @Parametres: in: order_file, in: order_data
* @Retorn: 0 ok, -1 error
*
************************************************/
static int writeOrderFile(const char *order_file, const char *order_data) {
	int fd_order;

	fd_order = open(order_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_order < 0) {
		writeString("Error: Could not create order file\n");
		return -1;
	}
	write(fd_order, order_data, strlen(order_data));
	close(fd_order);
	return 0;
}

/***********************************************
*
* @Finalitat: Llegeix una trama del socket i comprova que es un ACK
* @Parametres: in: fd
* @Retorn: 0 si ACK valid, -1 en cas contrari
*
************************************************/
static int readTradeAck(int fd) {
	char ack_buffer[FRAME_SIZE];
	Frame ack_frame;
	ssize_t ack_read;

	ack_read = read(fd, ack_buffer, FRAME_SIZE);
	if (ack_read != FRAME_SIZE ||
	    deserializeFrame(ack_buffer, &ack_frame) != 0 ||
	    ack_frame.type != FRAME_TYPE_ACK) {
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Envia el header 0x14 i les dades 0x15 de la comanda al aliat
* @Parametres: in: fd, order_data, origin, realm
* @Retorn: 0 ok, -1 error
*
************************************************/
static int sendTradeToServer(int fd, const char *order_data, const char *origin,
                             const char *realm) {
	char *order_file;
	char *md5sum;
	char header_data[256];
	long file_size;
	int result;

	asprintf(&order_file, "/tmp/order_%s.txt", realm);

	if (writeOrderFile(order_file, order_data) < 0) {
		free(order_file);
		return -1;
	}

	file_size = (long)strlen(order_data);
	md5sum = calculateMD5(order_file);
	if (!md5sum) {
		writeString("Error: Could not calculate MD5\n");
		unlink(order_file);
		free(order_file);
		return -1;
	}

	snprintf(header_data, sizeof(header_data), "order.txt&%ld&%s", file_size, md5sum);
	result = 0;
	if (sendFrame(fd, FRAME_TYPE_TRADE_REQUEST, origin, realm, header_data) != 0) {
		writeString("Error: Failed to send trade header\n");
		result = -1;
	} else if (readTradeAck(fd) != 0) {
		writeString("Error: Invalid ACK for trade header\n");
		result = -1;
	} else if (sendTradeOrder(fd, order_data, strlen(order_data), origin, realm) != 0) {
		writeString("Error: Failed to send trade order\n");
		result = -1;
	}

	free(md5sum);
	unlink(order_file);
	free(order_file);
	return result;
}

/***********************************************
*
* @Finalitat: Rep i valida la trama 0x32 (MD5 response) del aliat
* @Parametres: in: fd, out: response (omplert amb la trama)
* @Retorn: 0 ok, -1 error o CHECK_KO
*
************************************************/
static int receiveMD5Response(int fd, Frame *response) {
	char frame_buffer[FRAME_SIZE];
	int wait_result;
	ssize_t bytes_read;
	char *msg;

	wait_result = waitForData(fd, 30);
	if (wait_result <= 0) {
		writeString("Error: Timeout waiting for MD5 response\n");
		return -1;
	}
	bytes_read = read(fd, frame_buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE || deserializeFrame(frame_buffer, response) != 0) {
		writeString("Error: Invalid MD5 response\n");
		return -1;
	}
	if (response->type != FRAME_TYPE_ACK_MD5SUM) {
		asprintf(&msg, "Error: Expected MD5 ACK (0x32), got 0x%02x\n", response->type);
		writeString(msg);
		free(msg);
		return -1;
	}
	if (strncmp(response->data, "CHECK_KO", 8) == 0) {
		writeString("Error: Server reported MD5 mismatch\n");
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Rep la resposta final del aliat (0x32 + 0x16)
* @Parametres: in: fd, out: response
* @Retorn: 0 ok, -1 error
*
************************************************/
static int receiveTradeResponse(int fd, Frame *response) {
	char frame_buffer[FRAME_SIZE];
	int wait_result;
	ssize_t bytes_read;

	if (receiveMD5Response(fd, response) != 0) return -1;

	wait_result = waitForData(fd, 60);
	if (wait_result <= 0) {
		writeString("Error: Timeout waiting for trade response\n");
		return -1;
	}
	bytes_read = read(fd, frame_buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE ||
	    deserializeFrame(frame_buffer, response) != 0 ||
	    !validateChecksum(response)) {
		writeString("Error: Invalid trade response frame\n");
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Actualitza l'inventari local quan el aliat ha acceptat la comanda
* @Parametres: in/out: shared, in: trade_list, trade_count, cached_inv
* @Retorn: ----
*
************************************************/
static void processSuccessfulTrade(SharedData *shared, const TradeItem *trade_list,
                                   int trade_count, const Inventory *cached_inv) {
	int i;
	Product *cached_prod;
	float weight;

	pthread_mutex_lock(&shared->inventory_lock);
	for (i = 0; i < trade_count; i++) {
		cached_prod = findProduct(cached_inv, trade_list[i].name);
		weight = (cached_prod != NULL) ? cached_prod->weight : 1.0f;
		addOrUpdateProduct(shared->inventory, trade_list[i].name,
		                   trade_list[i].quantity, weight);
	}
	saveInventory(shared->config->stock_file, shared->inventory);
	pthread_mutex_unlock(&shared->inventory_lock);
}

/***********************************************
*
* @Finalitat: Despatxa la resposta del aliat (OK -> processSuccessfulTrade)
* @Parametres: in/out: shared, in: realm, response_frame
*              in: trade_list, trade_count, cached_inventory
* @Retorn: ----
*
************************************************/
static void dispatchTradeResponse(SharedData *shared, const char *realm, const Frame *response_frame, const TradeItem *trade_list, int trade_count,const Inventory *cached_inventory) {
    char *msg;

    if (response_frame->type != FRAME_TYPE_TRADE_RESPONSE) {
        asprintf(&msg, "Unexpected response type: 0x%02x\n", response_frame->type);
        writeString(msg);
        free(msg);
        return;
    }
    if (strncmp(response_frame->data, "OK", 2) == 0) {
        processSuccessfulTrade(shared, trade_list, trade_count, cached_inventory);
        asprintf(&msg, ">>> Order accepted by %s.\n\n", realm);
        writeString(msg);
        free(msg);
    } else {
        asprintf(&msg, "The trade was rejected: %s\n", response_frame->data);
        writeString(msg);
        free(msg);
    }
}

/***********************************************
*
* @Finalitat: Gestiona el comando SEND dins del submenu de trade
* @Parametres: in/out: shared, in: realm/dest_ip/dest_port
*              in: trade_list/trade_count/cached_inventory
*              out: fd_socket, origin, response_frame
* @Retorn: 1 (sempre - acaba el bucle del submenu)
*
************************************************/
static int processSendCommand(SharedData *shared, const char *realm,
                              const char *dest_ip, int dest_port,
                              const TradeItem *trade_list, int trade_count,
                              const Inventory *cached_inventory,
                              int *fd_socket, char **origin, Frame *response_frame) {
	char *order_data;
	char *msg;

	*fd_socket = openClientConn((char *)dest_ip, dest_port);
	if (*fd_socket < 0) {
		asprintf(&msg, "Our ravens cannot reach %s.\n", realm);
		writeString(msg);
		free(msg);
		return 1;
	}
	*origin = createOriginString(shared->config->ip, shared->config->port);
	order_data = buildTradeOrderData(trade_list, trade_count);
	if (!order_data) {
		writeString("Error: Could not build order data\n");
		return 1;
	}
	if (sendTradeToServer(*fd_socket, order_data, *origin, realm) != 0) {
		free(order_data);
		return 1;
	}
	free(order_data);

	if (receiveTradeResponse(*fd_socket, response_frame) == 0) {
		dispatchTradeResponse(shared, realm, response_frame,
		                      trade_list, trade_count, cached_inventory);
	}
	return 1;
}

/***********************************************
* Flux LIST PRODUCTS <regne>: descarrega l'inventari del aliat
************************************************/

/***********************************************
*
* @Finalitat: Envia 0x11 i rep 0x12 amb el header (FileName&Size&MD5)
* @Parametres: in: fd_socket, origin, realm, in/out: shared
*              out: response_frame (omplert), out: received_md5 (a alliberar)
* @Retorn: 0 ok, -1 error
*
************************************************/
static int sendListRequestAndGetMD5(int fd_socket, const char *origin,
                                    const char *realm, SharedData *shared,
                                    Frame *response_frame, char **received_md5) {
	if (sendFrame(fd_socket, FRAME_TYPE_PRODUCT_LIST, origin, realm,
	              shared->config->realm_name) != 0) {
		writeString("Error: Could not send products list request\n");
		return -1;
	}
	if (receiveAndValidateFrame(fd_socket, response_frame,
	                            FRAME_TYPE_PRODUCTS_LIST_REQUEST, 30) != 0) {
		writeString("Error: Failed to receive products header (0x12)\n");
		return -1;
	}
	if (sendACKFrame(fd_socket, shared->config->realm_name) != 0) {
		writeString("Error: Could not send ACK\n");
		return -1;
	}
	*received_md5 = extractMD5FromHeaderData(response_frame->data);
	if (!*received_md5) {
		writeString("Error: Could not extract MD5 from header\n");
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Rep el fitxer fragmentat (0x13) i verifica el seu MD5
* @Parametres: in: fd_socket, products_file, realm, in/out: shared, in: md5
* @Retorn: 0 ok, -1 error (ja respost 0x32)
*
************************************************/
static int downloadAndVerifyProductsFile(int fd_socket, const char *products_file,
                                         SharedData *shared, const char *md5) {
	if (receiveProductsList(fd_socket, products_file, shared->config->realm_name) != 0) {
		writeString("Error: Failed to receive products list\n");
		return -1;
	}
	if (verifyMD5(products_file, md5) == 1) {
		sendMD5Response(fd_socket, 1, shared->config->realm_name);
		return 0;
	}
	sendMD5Response(fd_socket, 0, shared->config->realm_name);
	writeString("Error: MD5 verification failed for products list\n");
	return -1;
}

/***********************************************
*
* @Finalitat: Mostra l'inventari en format 'Listing products from X:'
* @Parametres: in: realm, in: inv
* @Retorn: ----
*
************************************************/
static void displayProductsList(const char *realm, const Inventory *inv) {
    int i;
    char *msg;
    int list_index = 1; 

    asprintf(&msg, "Listing products from %s:\n", realm);
    writeString(msg);
    free(msg);

    for (i = 0; i < inv->num_products; i++) {
        if (inv->products[i].amount > 0) {            
            asprintf(&msg, "%d. %s (%d units)\n", 
                     list_index,
                     inv->products[i].name, 
                     inv->products[i].amount);
                     
            writeString(msg);
            free(msg);
            
            list_index++;
        }
    }
}

/***********************************************
*
* @Finalitat: Comando LIST PRODUCTS <regne> - demana la llista al aliat
*             reservant un Envoy del pool i guarda el resultat en la cache.
* @Parametres: in: realm, in/out: shared
* @Retorn: ----
*
************************************************/
void handleListProductsRemote(const char *realm, SharedData *shared) {
	Route *route;
	int fd_socket;
	char *origin;
	int envoy_id;
	char *received_md5 = NULL;
	char *products_file = NULL;
	Frame response_frame;
	Inventory server_inv;

	if (validateAlliedRoute(realm, shared, &route,
	    "We share no alliance with %s. Their wares remain hidden.\n") != 0) return;

	envoy_id = reserveEnvoyForRealm(shared, realm);
	if (envoy_id < 0) return;

	if (openAllyConnection(route, shared, &fd_socket, &origin) != 0) {
		releaseEnvoyLocked(shared, envoy_id);
		return;
	}

	if (sendListRequestAndGetMD5(fd_socket, origin, realm, shared,
	                             &response_frame, &received_md5) != 0) {
		goto cleanup;
	}

	asprintf(&products_file, "/tmp/received_products_%s.txt", realm);
	if (downloadAndVerifyProductsFile(fd_socket, products_file, shared, received_md5) != 0) {
		goto cleanup;
	}

	server_inv.products = NULL;
	server_inv.num_products = 0;
	server_inv.capacity = 0;
	if (importInventoryFromFile(products_file, &server_inv) == 0) {
		displayProductsList(realm, &server_inv);
		cacheProducts(realm, &server_inv);
		freeInventory(&server_inv);
	} else {
		writeString("Error: Could not parse products list\n");
	}

cleanup:
	if (received_md5) free(received_md5);
	if (products_file) free(products_file);
	free(origin);
	close(fd_socket);
	releaseEnvoyLocked(shared, envoy_id);
}

/***********************************************
* Flux START TRADE <regne>: submenu interactiu
************************************************/

/***********************************************
*
* @Finalitat: Resol IP i port del destinatari aliat (directa o per ruta)
* @Parametres: in: route, in/out: shared (manté config_lock invariant)
*              out: dest_ip (mida 20), dest_port
* @Retorn: ----
*
************************************************/
static void resolveAllyDestination(Route *route, SharedData *shared,
                                   char *dest_ip, int *dest_port) {
	pthread_mutex_lock(&shared->config_lock);
	if (route->ally_ip && route->ally_port > 0) {
		copiarString(dest_ip, route->ally_ip, 20);
		*dest_port = route->ally_port;
	} else {
		copiarString(dest_ip, route->ip, 20);
		*dest_port = route->port;
	}
	pthread_mutex_unlock(&shared->config_lock);
}

/***********************************************
* Context del submenu de trade per simplificar firma del bucle
************************************************/
typedef struct {
	SharedData *shared;
	const char *realm;
	const Inventory *cached_inventory;
	const char *dest_ip;
	int dest_port;
	TradeItem *trade_list;
	int trade_count;
	int trade_capacity;
	int *fd_socket;
	char **origin;
	Frame *response_frame;
} TradeSubmenuCtx;

/***********************************************
*
* @Finalitat: Despatxa una linia introduida pel jugador al submenu de trade
* @Parametres: in: cmd (ja en minuscules i trimmed), in/out: ctx
* @Retorn: 1 si el bucle ha d'acabar, 0 si continua
*
************************************************/
static int dispatchTradeSubcommand(char *cmd, TradeSubmenuCtx *ctx) {
	char *prod;
	int qty;

	if (strncmp(cmd, "add ", 4) == 0) {
		if (parseProductAndQtyDynamic(cmd + 4, &prod, &qty)) {
			int err = addProductToTrade(prod, qty, ctx->cached_inventory,
			                            &ctx->trade_list, &ctx->trade_count,
			                            &ctx->trade_capacity);
			free(prod);
			return (err < 0) ? 1 : 0;
		}
		writeString("Invalid syntax. Usage: add <product> <quantity>\n");
		return 0;
	}
	if (strncmp(cmd, "remove ", 7) == 0) {
		if (parseProductAndQtyDynamic(cmd + 7, &prod, &qty)) {
			removeProductFromTrade(prod, qty, ctx->trade_list, &ctx->trade_count);
			free(prod);
		} else {
			writeString("Invalid syntax. Usage: remove <product> <quantity>\n");
		}
		return 0;
	}
	if (strcmp(cmd, "list") == 0) {
		displayTradeList(ctx->trade_list, ctx->trade_count);
		return 0;
	}
	if (strcmp(cmd, "send") == 0) {
		if (ctx->trade_count == 0) {
			writeString("The manifest is empty. Add wares first.\n");
			return 0;
		}
		return processSendCommand(ctx->shared, ctx->realm, ctx->dest_ip, ctx->dest_port,
		                          ctx->trade_list, ctx->trade_count, ctx->cached_inventory,
		                          ctx->fd_socket, ctx->origin, ctx->response_frame);
	}
	if (strcmp(cmd, "cancel") == 0 || strcmp(cmd, "exit") == 0) {
		writeString("The trade has been abandoned.\n");
		return 1;
	}
	writeString("Unknown command. Commands: add, remove, list, send, cancel\n");
	return 0;
}

/***********************************************
*
* @Finalitat: Bucle del submenu de trade fins que el jugador acaba
* @Parametres: in/out: ctx
* @Retorn: ----
*
************************************************/
static void runTradeSubmenu(TradeSubmenuCtx *ctx) {
	char *cmd;
	int done = 0;

	writeString("\nTrade counsel. Commands: add <product> <qty>, remove <product> <qty>, list, send, cancel\n");
	enterTradeMode(ctx->shared);

	while (!done) {
		writeString("(trade)> ");
		cmd = readUntil(0, '\n');
		if (!cmd) {
			done = 1;
		} else {
			trim(cmd);
			toLower(cmd);
			done = dispatchTradeSubcommand(cmd, ctx);
			free(cmd);
		}
	}

	exitTradeMode(ctx->shared);
	flushNotifications(ctx->shared);
}

/***********************************************
*
* @Finalitat: Comando START TRADE <regne> - submenu interactiu de comerç
* @Parametres: in: realm, in/out: shared
* @Retorn: ----
*
************************************************/
void handleStartTrade(const char *realm, SharedData *shared) {
	Route *route;
	const Inventory *cached_inventory;
	TradeItem *trade_list;
	int envoy_id;
	char dest_ip[20];
	int dest_port;
	int fd_socket = -1;
	char *origin = NULL;
	Frame response_frame;
	TradeSubmenuCtx ctx;
	char *msg;

	if (validateAlliedRoute(realm, shared, &route,
	    "We share no alliance with %s. Trade is forbidden.\n") != 0) return;

	resolveAllyDestination(route, shared, dest_ip, &dest_port);

	cached_inventory = findCachedInventory(realm);
	if (!cached_inventory) {
		asprintf(&msg, "We know nothing of %s's wares. Send ravens to inquire first.\n", realm);
		writeString(msg);
		free(msg);
		return;
	}

	envoy_id = reserveEnvoyForRealm(shared, realm);
	if (envoy_id < 0) return;

	trade_list = malloc(sizeof(TradeItem) * 50);
	if (!trade_list) {
		writeString("Error: Could not allocate trade list\n");
		releaseEnvoyLocked(shared, envoy_id);
		return;
	}

	ctx.shared = shared;
	ctx.realm = realm;
	ctx.cached_inventory = cached_inventory;
	ctx.dest_ip = dest_ip;
	ctx.dest_port = dest_port;
	ctx.trade_list = trade_list;
	ctx.trade_count = 0;
	ctx.trade_capacity = 50;
	ctx.fd_socket = &fd_socket;
	ctx.origin = &origin;
	ctx.response_frame = &response_frame;

	runTradeSubmenu(&ctx);

	free(ctx.trade_list);
	if (origin) free(origin);
	if (fd_socket >= 0) close(fd_socket);
	releaseEnvoyLocked(shared, envoy_id);
}
