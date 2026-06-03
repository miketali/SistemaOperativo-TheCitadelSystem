/***********************************************
*
* @Proposit: Header de gestion de inventario
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _INVENTORY_H
#define _INVENTORY_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"

#include <string.h>
//constantes
#define MAX_PRODUCT_NAME 100
#define MAX_PRODUCTS 1000

//tipos de datos
typedef struct {
	char name[MAX_PRODUCT_NAME];
	int amount;
	float weight;
} Product;

typedef struct {
	Product *products;
	int num_products;
	int capacity;
} Inventory;

//funciones de inventario
int loadInventory(const char *filename, Inventory *inv);
int saveInventory(const char *filename, const Inventory *inv);
void displayInventory(const Inventory *inv);
void freeInventory(Inventory *inv);

//funciones Fase 3: exportar/importar inventario como texto
int exportInventoryToFile(const Inventory *inv, const char *filepath);
int importInventoryFromFile(const char *filepath, Inventory *inv);

//funciones Fase 3: modificar stock
int updateProductStock(Inventory *inv, const char *product_name, int quantity_change);
Product* findProduct(const Inventory *inv, const char *product_name);
int addOrUpdateProduct(Inventory *inv, const char *product_name, int quantity, float weight);

#endif
