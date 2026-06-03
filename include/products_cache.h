/***********************************************
*
* @Proposit: Cache d'inventaris remots dels regnes aliats
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Quan executem LIST PRODUCTS <regne>, guardem l'inventari rebut a la
* cache per poder consultar-lo despres a START TRADE sense haver de
* tornar a demanar-lo. El modul amaga l'estructura interna; els altres
* moduls nomes accedeixen via les funcions publiques.
*
************************************************/

#ifndef _PRODUCTS_CACHE_H
#define _PRODUCTS_CACHE_H

#include "inventory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
//cerca l'inventari cacheado d'un regne (o NULL si no esta a la cache)
const Inventory* findCachedInventory(const char *realm);

//guarda (o sobreescriu) l'inventari rebut d'un regne
int cacheProducts(const char *realm, const Inventory *inv);

//allibera tota la cache (cridada al EXIT si cal)
void freeProductsCache(void);

#endif
