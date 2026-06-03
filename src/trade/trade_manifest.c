/***********************************************
*
* @Proposit: Implementacio del manifest de comerç (add/remove/list/build)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "trade_manifest.h"

/***********************************************
*
* @Finalitat: Cerca un producte en un inventari
* @Parametres: in: name, in: inv
* @Retorn: index dins inv->products o -1
*
************************************************/
static int findProductInInventory(const char *name, const Inventory *inv) {
	int i;
	for (i = 0; i < inv->num_products; i++) {
		if (strcasecmpCustom(inv->products[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

/***********************************************
*
* @Finalitat: Cerca un producte al manifest
* @Parametres: in: name, in: list, in: count
* @Retorn: index o -1
*
************************************************/
static int findProductInManifest(const char *name, TradeItem *list, int count) {
	int i;
	for (i = 0; i < count; i++) {
		if (strcasecmpCustom(list[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

/***********************************************
*
* @Finalitat: Emet un missatge d'error quan la quantitat demanada supera l'estoc
* @Parametres: in: prod, qty, available_stock, already_in_manifest
* @Retorn: ----
*
************************************************/
static void reportStockExceeded(const char *prod, int qty, int available_stock,
                                int already_in_manifest) {
	char *msg;

	if (already_in_manifest > 0) {
		asprintf(&msg, "Cannot add %d more %s. Only %d available (%d already in manifest).\n",
		         qty, prod, available_stock, already_in_manifest);
	} else {
		asprintf(&msg, "Cannot add %d %s. Only %d available in their stores.\n",
		         qty, prod, available_stock);
	}
	writeString(msg);
	free(msg);
}

/***********************************************
*
* @Finalitat: Afegeix una nova entrada al manifest, redimensionant si cal
* @Parametres: in/out: trade_list, in/out: trade_count, in/out: trade_capacity,
*              in: prod, in: qty
* @Retorn: 0 ok, -1 error d'allocacio
*
************************************************/
static int appendNewTradeItem(TradeItem **trade_list, int *trade_count,
                              int *trade_capacity, const char *prod, int qty) {
	char *msg;

	if (*trade_count >= *trade_capacity) {
		*trade_capacity *= 2;
		*trade_list = realloc(*trade_list, (*trade_capacity) * sizeof(TradeItem));
		if (!(*trade_list)) {
			writeString("Error: Could not expand trade list\n");
			return -1;
		}
	}
	copiarString((*trade_list)[*trade_count].name, prod, MAX_PRODUCT_NAME);
	(*trade_list)[*trade_count].quantity = qty;
	(*trade_count)++;
	asprintf(&msg, "%s (%d units) added to manifest.\n", prod, qty);
	writeString(msg);
	free(msg);
	return 0;
}

/***********************************************
*
* @Finalitat: Afegeix un producte al manifest validant estoc
* @Parametres: in: prod, qty, inventory (cacheado del aliat)
*              in/out: trade_list, trade_count, trade_capacity
* @Retorn: 0 ok, -1 error de memoria
*
************************************************/
int addProductToTrade(const char *prod, int qty, const Inventory *inventory,
                      TradeItem **trade_list, int *trade_count, int *trade_capacity) {
	int idx;
	int product_idx;
	int available_stock;
	int already_in_manifest;
	char *msg;

	product_idx = findProductInInventory(prod, inventory);
	if (product_idx < 0) {
		asprintf(&msg, "The product '%s' is not among their wares.\n", prod);
		writeString(msg);
		free(msg);
		return 0;
	}

	available_stock = inventory->products[product_idx].amount;
	idx = findProductInManifest(prod, *trade_list, *trade_count);
	already_in_manifest = (idx >= 0) ? (*trade_list)[idx].quantity : 0;

	if (already_in_manifest + qty > available_stock) {
		reportStockExceeded(prod, qty, available_stock, already_in_manifest);
		return 0;
	}

	if (idx >= 0) {
		(*trade_list)[idx].quantity += qty;
		asprintf(&msg, "%s quantity set to %d.\n", prod, (*trade_list)[idx].quantity);
		writeString(msg);
		free(msg);
		return 0;
	}
	return appendNewTradeItem(trade_list, trade_count, trade_capacity, prod, qty);
}

/***********************************************
*
* @Finalitat: Treu un producte del manifest o redueix la seva quantitat
* @Parametres: in: prod, qty, in/out: trade_list, trade_count
* @Retorn: ----
*
************************************************/
void removeProductFromTrade(const char *prod, int qty, TradeItem *trade_list, int *trade_count) {
	int idx, i;
	char *msg;

	idx = findProductInManifest(prod, trade_list, *trade_count);
	if (idx < 0) {
		asprintf(&msg, "'%s' is not in the manifest.\n", prod);
		writeString(msg);
		free(msg);
	} else {
		if (qty >= trade_list[idx].quantity) {
			//eliminar completament
			asprintf(&msg, "%s removed from manifest.\n", prod);
			writeString(msg);
			free(msg);
			for (i = idx; i < *trade_count - 1; i++) {
				trade_list[i] = trade_list[i + 1];
			}
			(*trade_count)--;
		} else {
			//reduir quantitat
			trade_list[idx].quantity -= qty;
			asprintf(&msg, "%s quantity reduced to %d.\n", prod, trade_list[idx].quantity);
			writeString(msg);
			free(msg);
		}
	}
}

/***********************************************
*
* @Finalitat: Mostra el manifest per pantalla
* @Parametres: in: trade_list, trade_count
* @Retorn: ----
*
************************************************/
void displayTradeList(const TradeItem *trade_list, int trade_count) {
	int i;
	char *msg;

	writeString("--- Trade Manifest ---\n");
	if (trade_count == 0) {
		writeString("(none)\n");
	} else {
		for (i = 0; i < trade_count; i++) {
			asprintf(&msg, "[%d] %s - %d units\n", i + 1, trade_list[i].name, trade_list[i].quantity);
			writeString(msg);
			free(msg);
		}
	}
}

/***********************************************
*
* @Finalitat: Construeix l'string que enviarem al aliat amb format
*             "name|qty\nname|qty\n..."
* @Parametres: in: trade_list, trade_count
* @Retorn: string a alliberar pel caller, NULL si error
*
************************************************/
char* buildTradeOrderData(const TradeItem *trade_list, int trade_count) {
	char *order_data;
	char *item_line;
	size_t order_size;
	int i;

	//estimar tamany necessari
	order_size = 0;
	for (i = 0; i < trade_count; i++) {
		order_size += strlen(trade_list[i].name) + 20;
	}

	order_data = malloc(order_size);
	if (!order_data) {
		return NULL;
	}
	order_data[0] = '\0';

	for (i = 0; i < trade_count; i++) {
		asprintf(&item_line, "%s|%d\n", trade_list[i].name, trade_list[i].quantity);
		strcat(order_data, item_line);
		free(item_line);
	}

	return order_data;
}
