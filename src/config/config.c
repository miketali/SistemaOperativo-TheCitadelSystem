/***********************************************
*
* @Proposit: Gestion de configuracion del Maester
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "config.h"
/***********************************************
*
* @Finalitat: Reemplaza ampersands por espacios en un string
* @Parametres: in/out: str = string a modificar
* @Retorn: ----
*
************************************************/
void replaceAmpersands(char *str) {
	int i;

	//eliminar '&' al final
	int len = strlen(str);
	if (len > 0 && str[len - 1] == '&') {
		str[len - 1] = '\0';
	}

	//reemplazar '&' en medio por espacio
	for (i = 0; str[i] != '\0'; i++) {
		if (str[i] == '&') {
			str[i] = ' ';
		}
	}
}

/***********************************************
*
* @Finalitat: Llegeix els camps name/ip/port d'una linia de ruta i els guarda
*             en la Route (sense inicialitzar l'estat d'alianç)
* @Parametres: in/out: line_copy = copia editable de la linia (es modifica),
*              out: route
* @Retorn: 0 si exit, -1 si error (route->name/ip lliurats si calia)
*
************************************************/
static int parseRouteFields(char *line_copy, Route *route) {
	char *tok;
	char *next_pos = NULL;

	tok = parseToken(line_copy, &next_pos);
	if (!tok) {
		return -1;
	}
	replaceAmpersands(tok);
	route->name = copyStringDynamic(tok);
	if (!route->name) {
		return -1;
	}

	tok = parseToken(NULL, &next_pos);
	if (!tok) {
		free(route->name);
		return -1;
	}
	route->ip = copyStringDynamic(tok);
	if (!route->ip) {
		free(route->name);
		return -1;
	}

	tok = parseToken(NULL, &next_pos);
	if (!tok) {
		free(route->name);
		free(route->ip);
		return -1;
	}
	route->port = atoi(tok);
	return 0;
}

/***********************************************
*
* @Finalitat: Lee y parsea una ruta individual del archivo de configuracion
* @Parametres: in: line = linea con datos de la ruta
*              out: route = puntero a Route donde guardar datos
* @Retorn: 0 si ok, -1 si error
*
************************************************/
int parseRoute(const char *line, Route *route) {
	char *line_copy;

	line_copy = copyStringDynamic(line);
	if (!line_copy) {
		return -1;
	}

	if (parseRouteFields(line_copy, route) < 0) {
		free(line_copy);
		return -1;
	}

	//inicializar estado de alianza a NONE
	route->alliance_status = ALLIANCE_NONE;
	route->pending_origin = NULL;
	route->ally_ip = NULL;
	route->ally_port = 0;

	free(line_copy);
	return 0;
}

/***********************************************
*
* @Finalitat: Llegeix una linia i la guarda dinamicament en el destinatari.
*             Si la linia ha de patir replaceAmpersands abans de copiar, ho fa
* @Parametres: in: fd, out: dest = punter on guardar la cadena reservada,
*              in: do_replace = 1 per aplicar replaceAmpersands
* @Retorn: 0 si exit, -1 si error d'allocacio (linia buida no es error)
*
************************************************/
static int readStringField(int fd, char **dest, int do_replace) {
	char *line;

	line = readUntil(fd, '\n');
	if (!line) {
		return 0;
	}
	if (do_replace) {
		replaceAmpersands(line);
	}
	*dest = copyStringDynamic(line);
	free(line);
	if (!*dest) {
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Llegeix una linia i la converteix a enter (atoi)
* @Parametres: in: fd, out: dest
* @Retorn: ----
*
************************************************/
static void readIntField(int fd, int *dest) {
	char *line;

	line = readUntil(fd, '\n');
	if (line) {
		*dest = atoi(line);
		free(line);
	}
}

/***********************************************
*
* @Finalitat: Llegeix els 5 camps de capçalera del fitxer de configuracio
*             (realm_name, folder_path, num_envoys, ip, port)
* @Parametres: in: fd, in/out: config
* @Retorn: 0 ok, -1 error
*
************************************************/
static int readConfigHeader(int fd, MaesterConfig *config) {
	if (readStringField(fd, &config->realm_name, 1) < 0) {
		return -1;
	}
	if (readStringField(fd, &config->folder_path, 0) < 0) {
		return -1;
	}
	readIntField(fd, &config->num_envoys);
	if (readStringField(fd, &config->ip, 0) < 0) {
		return -1;
	}
	readIntField(fd, &config->port);
	return 0;
}

/***********************************************
*
* @Finalitat: Llegeix totes les rutes des de fd, redimensionant config->routes
*             quan cal. La capacitat inicial ja ha de estar reservada.
* @Parametres: in: fd, in/out: config
* @Retorn: 0 ok, -1 error
*
************************************************/
static int readConfigRoutes(int fd, MaesterConfig *config) {
	char *line;
	int i;

	i = 0;
	while ((line = readUntil(fd, '\n')) != NULL) {
		if (i >= config->routes_capacity) {
			config->routes_capacity *= 2;
			config->routes = realloc(config->routes, config->routes_capacity * sizeof(Route));
			if (!config->routes) {
				free(line);
				return -1;
			}
		}
		if (parseRoute(line, &config->routes[i]) == 0) {
			i++;
		} else {
			config->num_routes = i;
			free(line);
			return -1;
		}
		free(line);
	}
	config->num_routes = i;
	return 0;
}

/***********************************************
*
* @Finalitat: Inicialitza els camps de MaesterConfig a valors per defecte
* @Parametres: out: config
* @Retorn: ----
*
************************************************/
static void initConfigDefaults(MaesterConfig *config) {
	config->routes = NULL;
	config->num_routes = 0;
	config->realm_name = NULL;
	config->folder_path = NULL;
	config->ip = NULL;
}

/***********************************************
*
* @Finalitat: Lee y parsea el archivo de configuracion del maester
* @Parametres: in: filename = ruta al archivo de configuracion
*              out: config = puntero a estructura MaesterConfig
* @Retorn: 0 si todo ok, -1 si hay error
*
************************************************/
int readConfig(const char *filename, MaesterConfig *config) {
	int fd;
	char *line;

	initConfigDefaults(config);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	if (readConfigHeader(fd, config) < 0) {
		close(fd);
		freeConfig(config);
		return -1;
	}

	config->routes_capacity = 10;
	config->routes = calloc(config->routes_capacity, sizeof(Route));
	if (!config->routes) {
		close(fd);
		freeConfig(config);
		return -1;
	}

	//saltar la linea "--- ROUTES ---"
	line = readUntil(fd, '\n');
	if (line) {
		free(line);
	}

	if (readConfigRoutes(fd, config) < 0) {
		close(fd);
		freeConfig(config);
		return -1;
	}

	close(fd);
	return 0;
}

/***********************************************
*
* @Finalitat: Libera toda la memoria reservada para la configuracion
* @Parametres: in/out: config = puntero a estructura MaesterConfig
* @Retorn: ----
*
************************************************/
void freeConfig(MaesterConfig *config) {
	int i;

	//liberar strings de MaesterConfig
	if (config->realm_name) {
		free(config->realm_name);
		config->realm_name = NULL;
	}

	if (config->folder_path) {
		free(config->folder_path);
		config->folder_path = NULL;
	}

	if (config->ip) {
		free(config->ip);
		config->ip = NULL;
	}

	if (config->stock_file) {
		free(config->stock_file);
		config->stock_file = NULL;
	}

	//liberar strings de cada Route
	if (config->routes) {
		for (i = 0; i < config->num_routes; i++) {
			if (config->routes[i].name) {
				free(config->routes[i].name);
			}
			if (config->routes[i].ip) {
				free(config->routes[i].ip);
			}
			if (config->routes[i].pending_origin) {
				free(config->routes[i].pending_origin);
			}
			if (config->routes[i].ally_ip) {
				free(config->routes[i].ally_ip);
			}
		}
		free(config->routes);
		config->routes = NULL;
	}
}

/***********************************************
*
* @Finalitat: Establece el estado de alianza de una ruta
* @Parametres: in/out: route = puntero a la ruta
*              in: status = nuevo estado (ALLIANCE_NONE, ALLIANCE_PENDING, etc)
* @Retorn: ----
*
************************************************/
void setAllianceStatus(Route *route, int status) {
	if (route) {
		route->alliance_status = status;
	}
}

/***********************************************
*
* @Finalitat: Comprueba si la ruta tiene IP wildcard ("*.*.*.*"),
*             indicando que el regne existeix pero la ruta directa no
*             es coneguda (PDF pagina 9, Figura 3). Estes entrades es
*             tracten com "no existeix ruta directa" -> cal usar DEFAULT.
* @Parametres: in: route
* @Retorn: 1 si la IP es wildcard, 0 si es directa o si route es NULL
*
************************************************/
int isWildcardRoute(const Route *route) {
	if (!route || !route->ip) return 0;
	return route->ip[0] == '*';
}

/***********************************************
*
* @Finalitat: Busca una ruta por nombre de reino
* @Parametres: in: realm_name = nombre del reino a buscar
*              in: config = configuracion del Maester
* @Retorn: Puntero a la ruta, o NULL si no se encuentra
*
************************************************/
Route* findRouteByName(const char *realm_name, MaesterConfig *config) {
	int i;

	for (i = 0; i < config->num_routes; i++) {
		if (strcasecmpCustom(config->routes[i].name, realm_name) == 0) {
			return &config->routes[i];
		}
	}

	return NULL;
}

/***********************************************
*
* @Finalitat: Busca la ruta DEFAULT
* @Parametres: in: config = configuracion del Maester
* @Retorn: Puntero a la ruta DEFAULT, o NULL si no existe
*
************************************************/
Route* findDefaultRoute(MaesterConfig *config) {
	int i;

	for (i = 0; i < config->num_routes; i++) {
		if (strcasecmpCustom(config->routes[i].name, "DEFAULT") == 0) {
			return &config->routes[i];
		}
	}

	return NULL;
}

/***********************************************
*
* @Finalitat: Añade una ruta dinamica para un reino desconocido
* @Parametres: in: realm_name = nombre del reino
*              in: ip = IP del siguiente hop (normalmente DEFAULT)
*              in: port = puerto del siguiente hop
*              in/out: config = configuracion del Maester
* @Retorn: Puntero a la nueva ruta, o NULL si error
*
************************************************/
Route* addDynamicRoute(const char *realm_name, const char *ip, int port, MaesterConfig *config) {
	Route *new_route;

	//expandir capacidad si es necesario
	if (config->num_routes >= config->routes_capacity) {
		config->routes_capacity *= 2;
		config->routes = realloc(config->routes, config->routes_capacity * sizeof(Route));
		if (!config->routes) {
			return NULL;
		}
	}

	new_route = &config->routes[config->num_routes];

	new_route->name = copyStringDynamic(realm_name);
	if (!new_route->name) {
		return NULL;
	}

	new_route->ip = copyStringDynamic(ip);
	if (!new_route->ip) {
		free(new_route->name);
		return NULL;
	}

	new_route->port = port;
	new_route->alliance_status = ALLIANCE_NONE;
	new_route->pending_origin = NULL;
	new_route->ally_ip = NULL;
	new_route->ally_port = 0;

	config->num_routes++;

	return new_route;
}

/***********************************************
*
* @Finalitat: Guarda la IP:Port directa del aliado desde el ORIGIN
* @Parametres: in/out: route = ruta del aliado
*              in: origin = string con formato "IP:Port"
* @Retorn: ----
*
************************************************/
void setAllyDirectConnection(Route *route, const char *origin) {
	char *origin_copy;
	char *colon;

	if (!route || !origin) {
		return;
	}

	origin_copy = copyStringDynamic(origin);
	if (!origin_copy) {
		return;
	}

	colon = strchr(origin_copy, ':');
	if (colon) {
		*colon = '\0';

		//liberar ally_ip anterior si existe
		if (route->ally_ip) {
			free(route->ally_ip);
		}

		route->ally_ip = copyStringDynamic(origin_copy);
		route->ally_port = atoi(colon + 1);
	}

	free(origin_copy);
}
