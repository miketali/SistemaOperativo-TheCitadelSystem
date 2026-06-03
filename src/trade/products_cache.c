/***********************************************
*
* @Proposit: Implementacio de la cache d'inventaris remots
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* La cache es un array dinamic d'entrades (realm, inventory). L'estat
* es totalment private al modul: nomes el header exposa les operacions
* findCachedInventory, cacheProducts i freeProductsCache.
*
************************************************/
#include "products_cache.h"

//entrada interna de la cache
typedef struct {
	char *realm;
	Inventory inventory;
} CachedProducts;

//estat private al modul
static CachedProducts *products_cache = NULL;
static int cache_count = 0;
static int cache_capacity = 0;

/***********************************************
*
* @Finalitat: Localitza l'entrada de la cache per un regne donat
* @Parametres: in: realm
* @Retorn: punter intern o NULL si no existeix
*
************************************************/
static CachedProducts* findEntry(const char *realm) {
	int i;
	for (i = 0; i < cache_count; i++) {
		if (strcasecmpCustom(products_cache[i].realm, realm) == 0) {
			return &products_cache[i];
		}
	}
	return NULL;
}

/***********************************************
*
* @Finalitat: Retorna l'inventari cacheado d'un regne
* @Parametres: in: realm
* @Retorn: punter de nomes lectura a l'inventari, o NULL
*
************************************************/
const Inventory* findCachedInventory(const char *realm) {
	CachedProducts *entry = findEntry(realm);
	return entry ? &entry->inventory : NULL;
}

/***********************************************
*
* @Finalitat: Guarda (o sobreescriu) l'inventari rebut d'un regne
* @Parametres: in: realm, in: inv
* @Retorn: 0 ok, -1 error de memoria
*
************************************************/
int cacheProducts(const char *realm, const Inventory *inv) {
	CachedProducts *slot;
	CachedProducts *new_cache;

	slot = findEntry(realm);

	if (slot) {
		//ya existe - alliberar dades anteriors
		if (slot->realm) {
			free(slot->realm);
		}
		if (slot->inventory.products) {
			free(slot->inventory.products);
		}
	} else {
		//nou slot - expandir array si cal
		if (cache_count >= cache_capacity) {
			if (cache_capacity == 0) {
				cache_capacity = 1;
				new_cache = malloc(sizeof(CachedProducts) * cache_capacity);
			} else {
				cache_capacity *= 2;
				new_cache = realloc(products_cache, sizeof(CachedProducts) * cache_capacity);
			}
			if (!new_cache) {
				return -1;
			}
			products_cache = new_cache;
		}
		slot = &products_cache[cache_count];
		cache_count++;
	}

	//copiar realm
	slot->realm = malloc(strlen(realm) + 1);
	if (!slot->realm) {
		return -1;
	}
	strcpy(slot->realm, realm);

	//copiar inventari (deep copy)
	slot->inventory.capacity = inv->capacity;
	slot->inventory.num_products = inv->num_products;
	slot->inventory.products = malloc(sizeof(Product) * (size_t)inv->capacity);
	if (!slot->inventory.products) {
		free(slot->realm);
		slot->realm = NULL;
		return -1;
	}
	memcpy(slot->inventory.products, inv->products, sizeof(Product) * (size_t)inv->num_products);

	return 0;
}

/***********************************************
*
* @Finalitat: Allibera tota la memoria de la cache
*
************************************************/
void freeProductsCache(void) {
	int i;
	if (products_cache) {
		for (i = 0; i < cache_count; i++) {
			if (products_cache[i].realm) {
				free(products_cache[i].realm);
			}
			if (products_cache[i].inventory.products) {
				free(products_cache[i].inventory.products);
			}
		}
		free(products_cache);
		products_cache = NULL;
		cache_count = 0;
		cache_capacity = 0;
	}
}
