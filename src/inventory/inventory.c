/***********************************************
*
* @Proposit: Gestion del inventario de productos
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "inventory.h"

/***********************************************
*
* @Finalitat: Carga el inventario desde el archivo binario
* @Parametres: in: filename = ruta al archivo stock.db
*              out: inv = puntero a estructura Inventory
* @Retorn: 0 si todo ok, -1 si hay error
*
************************************************/
int loadInventory(const char *filename, Inventory *inv) {
	int fd;
	Product p;
	int n;
	int result;

	result = -1;
	inv->capacity = MAX_PRODUCTS;
	inv->num_products = 0;
	inv->products = calloc(inv->capacity, sizeof(Product));

	if (inv->products) {
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			free(inv->products);
		} else {
			//leer productos uno a uno del archivo binario
			n = read(fd, &p, sizeof(Product));
			while (n == sizeof(Product) && inv->num_products < inv->capacity) {
				inv->products[inv->num_products] = p;
				inv->num_products++;
				n = read(fd, &p, sizeof(Product));
			}

			close(fd);
			result = 0;
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Guarda el inventario en el archivo binario
* @Parametres: in: filename = ruta al archivo stock.db
*              in: inv = puntero a estructura Inventory
* @Retorn: 0 si todo ok, -1 si hay error
*
************************************************/
int saveInventory(const char *filename, const Inventory *inv) {
	int fd;
	int i;
	int result;

	result = -1;
	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		for (i = 0; i < inv->num_products; i++) {
			write(fd, &inv->products[i], sizeof(Product));
		}

		close(fd);
		result = 0;
	}
	return result;
}

/***********************************************
*
* @Finalitat: Muestra el inventario en formato de tabla
* @Parametres: in: inv = puntero a estructura Inventory
* @Retorn: ----
*
************************************************/
void displayInventory(const Inventory *inv) {
	int i;
	char *msg;
	int displayed_entries = 0;

	writeString("--- Trade Ledger ---\n");
	writeString("Item                  | Value (Gold) | Weight (Stone)\n");
	writeString("--------------------------------------------------------\n");

	for (i = 0; i < inv->num_products; i++) {
		if (inv->products[i].amount > 0) {
            asprintf(&msg, "%-20s | %-12d | %-14.1f\n",
                     inv->products[i].name,
                     inv->products[i].amount,
                     inv->products[i].weight);
            writeString(msg);
            free(msg);
            
            displayed_entries++; // >>> SOLO SE SUMA SI SE MUESTRA <<<
        }
	}

	writeString("--------------------------------------------------------\n");
	asprintf(&msg, "Total Entries: %d\n", displayed_entries);
    writeString(msg);
    free(msg);
}

/***********************************************
*
* @Finalitat: Libera toda la memoria reservada para el inventario
* @Parametres: in/out: inv = puntero a estructura Inventory
* @Retorn: ----
*
************************************************/
void freeInventory(Inventory *inv) {
	if (inv->products) {
		free(inv->products);
		inv->products = NULL;
	}
}

/***********************************************
*
* @Finalitat: Exporta el inventario a un archivo de texto
* @Parametres: in: inv = puntero a estructura Inventory
*              in: filepath = ruta donde guardar el archivo
* @Retorn: 0 si ok, -1 si error
* @Formato: Cada línea: "nombre|cantidad|peso\n"
*
************************************************/
int exportInventoryToFile(const Inventory *inv, const char *filepath) {
	int fd;
	char *line;
	int i;
	ssize_t written;
	size_t line_len;
	int result;
	int error;

	result = -1;
	if (inv != NULL && filepath != NULL) {
		fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			error = 0;
			for (i = 0; i < inv->num_products && !error; i++) {
				if (asprintf(&line, "%s|%d|%.1f\n",
				             inv->products[i].name,
				             inv->products[i].amount,
				             inv->products[i].weight) < 0) {
					error = 1;
				} else {
					line_len = strlen(line);
					written = write(fd, line, line_len);
					free(line);

					if (written != (ssize_t)line_len) {
						error = 1;
					}
				}
			}

			close(fd);
			if (!error) {
				result = 0;
			}
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Inicialitza el camp products del inventari si encara no esta
*             reservat, o el buida si ja existeix
* @Parametres: in/out: inv
* @Retorn: 0 ok, -1 error d'allocacio
*
************************************************/
static int ensureInventoryStorage(Inventory *inv) {
	if (inv->products == NULL) {
		inv->capacity = MAX_PRODUCTS;
		inv->num_products = 0;
		inv->products = calloc(inv->capacity, sizeof(Product));
		if (!inv->products) {
			return -1;
		}
	} else {
		inv->num_products = 0;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Parseja una linia "nom|quantitat|pes" i la afegeix al inventari
*             si encara hi ha capacitat
* @Parametres: in/out: inv, in: line = linia ja sense '\n'
* @Retorn: ----
*
************************************************/
static void parseInventoryLine(Inventory *inv, char *line) {
	char *sep1;
	char *sep2;
	int name_len;

	sep1 = strchr(line, '|');
	if (!sep1) {
		return;
	}
	sep2 = strchr(sep1 + 1, '|');
	if (!sep2 || inv->num_products >= inv->capacity) {
		return;
	}
	*sep1 = '\0';
	*sep2 = '\0';

	name_len = strlen(line);
	if (name_len >= MAX_PRODUCT_NAME) {
		name_len = MAX_PRODUCT_NAME - 1;
	}
	memcpy(inv->products[inv->num_products].name, line, name_len);
	inv->products[inv->num_products].name[name_len] = '\0';
	inv->products[inv->num_products].amount = atoi(sep1 + 1);
	inv->products[inv->num_products].weight = (float)atof(sep2 + 1);
	inv->num_products++;
}

/***********************************************
*
* @Finalitat: Processa el buffer extraient totes les linies completes i
*             retorna el bytes que queden sense processar al principi
* @Parametres: in/out: inv, in/out: buffer
* @Retorn: nous bytes restants al buffer (no processats)
*
************************************************/
static ssize_t processInventoryBuffer(Inventory *inv, char *buffer) {
	char *line_start;
	char *line_end;
	ssize_t remaining;

	line_start = buffer;
	while ((line_end = strchr(line_start, '\n')) != NULL) {
		*line_end = '\0';
		parseInventoryLine(inv, line_start);
		line_start = line_end + 1;
	}
	if (line_start != buffer && *line_start != '\0') {
		remaining = strlen(line_start);
		memmove(buffer, line_start, remaining);
	} else if (line_start != buffer) {
		remaining = 0;
	} else {
		remaining = strlen(buffer);
	}
	return remaining;
}

/***********************************************
*
* @Finalitat: Bucle de lectura del descriptor i parseig progressiu del fitxer
* @Parametres: in: fd, in/out: inv
* @Retorn: ----
*
************************************************/
static void readInventoryStream(int fd, Inventory *inv) {
	char buffer[256];
	ssize_t bytes_read;
	ssize_t total_in_buffer;

	total_in_buffer = 0;
	bytes_read = 1;
	while (bytes_read > 0 || total_in_buffer > 0) {
		bytes_read = read(fd, buffer + total_in_buffer,
		                  sizeof(buffer) - 1 - (size_t)total_in_buffer);
		if (bytes_read > 0) {
			total_in_buffer += bytes_read;
		}
		buffer[total_in_buffer] = '\0';
		total_in_buffer = processInventoryBuffer(inv, buffer);
	}
}

/***********************************************
*
* @Finalitat: Importa inventario desde archivo de texto
* @Parametres: in: filepath = ruta del archivo
*              out: inv = puntero a estructura Inventory (debe estar inicializada)
* @Retorn: 0 si ok, -1 si error
* @Formato esperado: "nombre|cantidad|peso\n"
*
************************************************/
int importInventoryFromFile(const char *filepath, Inventory *inv) {
	int fd;

	if (filepath == NULL || inv == NULL) {
		return -1;
	}
	if (ensureInventoryStorage(inv) < 0) {
		return -1;
	}

	fd = open(filepath, O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	readInventoryStream(fd, inv);
	close(fd);
	return 0;
}

/***********************************************
*
* @Finalitat: Busca un producto por nombre en el inventario
* @Parametres: in: inv = puntero a estructura Inventory
*              in: product_name = nombre del producto
* @Retorn: puntero al Product si existe, NULL si no existe
*
************************************************/
Product* findProduct(const Inventory *inv, const char *product_name) {
	int i;
	Product *result;
	int found;

	result = NULL;
	if (inv != NULL && product_name != NULL) {
		found = 0;
		for (i = 0; i < inv->num_products && !found; i++) {
			if (strcasecmpCustom(inv->products[i].name, product_name) == 0) {
				result = &inv->products[i];
				found = 1;
			}
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Actualiza el stock de un producto
* @Parametres: in/out: inv = puntero a estructura Inventory
*              in: product_name = nombre del producto
*              in: quantity_change = cambio de cantidad (+/-)
* @Retorn: 0 si ok, -1 si producto no existe, -2 si stock insuficiente
*
************************************************/
int updateProductStock(Inventory *inv, const char *product_name, int quantity_change) {
	Product *product;
	int new_amount;
	int result;

	result = -1;
	product = findProduct(inv, product_name);
	if (product != NULL) {
		new_amount = product->amount + quantity_change;
		if (new_amount < 0) {
			result = -2;  //stock insuficiente
		} else {
			product->amount = new_amount;
			result = 0;
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Añade un producto al inventario o actualiza cantidad si ya existe
* @Parametres: in/out: inv = puntero a estructura Inventory
*              in: product_name = nombre del producto
*              in: quantity = cantidad a añadir
*              in: weight = peso del producto (usado si es nuevo)
* @Retorn: 0 si ok, -1 si error
*
************************************************/
int addOrUpdateProduct(Inventory *inv, const char *product_name, int quantity, float weight) {
	Product *product;
	int name_len;
	int result;

	result = -1;
	if (inv != NULL && product_name != NULL && quantity > 0) {
		product = findProduct(inv, product_name);
		if (product != NULL) {
			//producto existe, actualizar cantidad
			product->amount += quantity;
			result = 0;
		} else if (inv->num_products < inv->capacity) {
			//producto nuevo, añadir al inventario
			name_len = strlen(product_name);
			if (name_len >= MAX_PRODUCT_NAME) {
				name_len = MAX_PRODUCT_NAME - 1;
			}
			memcpy(inv->products[inv->num_products].name, product_name, name_len);
			inv->products[inv->num_products].name[name_len] = '\0';
			inv->products[inv->num_products].amount = quantity;
			inv->products[inv->num_products].weight = weight;
			inv->num_products++;
			result = 0;
		}
	}
	return result;
}
