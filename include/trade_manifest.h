/***********************************************
*
* @Proposit: Manifest de comerç - llista en memoria de productes a
*             comprar durant un START TRADE.
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* L'estructura TradeItem es publica perque el modul trade.c la fa servir
* per passar la llista entre funcions. Les operacions add/remove/list/build
* viuen aqui i no toquen ni xarxa ni Envoys.
*
************************************************/

#ifndef _TRADE_MANIFEST_H
#define _TRADE_MANIFEST_H

#include "inventory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
//entrada del manifest: producte + quantitat a comprar
typedef struct {
	char name[MAX_PRODUCT_NAME];
	int quantity;
} TradeItem;

//afegeix un producte al manifest validant que existeixi i hi hagi estoc
//retorna 0 ok (inclus quan no s'afegeix per estoc), -1 error de memoria
int addProductToTrade(const char *prod, int qty, const Inventory *inventory,
                      TradeItem **trade_list, int *trade_count, int *trade_capacity);

//treu un producte del manifest (o redueix la quantitat)
void removeProductFromTrade(const char *prod, int qty,
                            TradeItem *trade_list, int *trade_count);

//mostra el manifest actual per pantalla
void displayTradeList(const TradeItem *trade_list, int trade_count);

//construeix l'string serializat amb format "name|qty\n..." per enviar al aliat
//el caller ha d'alliberar el resultat
char* buildTradeOrderData(const TradeItem *trade_list, int trade_count);

#endif
