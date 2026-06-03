/***********************************************
*
* @Proposit: Header del modulo de transferencia de archivos
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Funcionalidades principales para Fase 3:
* - Fragmentacion de archivos en tramas de 275 bytes
* - Reensamblaje de archivos recibidos
* - Calculo de MD5SUM usando comando del sistema
* - Verificacion de integridad con MD5SUM
* - Envio/recepcion de sigils, listas de productos, ordenes
*
************************************************/

#ifndef _FILETRANSFER_H
#define _FILETRANSFER_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "protocol.h"

#include "network.h"
#include "utils.h"
#include <fcntl.h>
//constantes de transferencia
#define MAX_FRAGMENT_SIZE 275    //tamaño maximo de data en Frame
#define MD5_HASH_LENGTH 32       //longitud del hash MD5 en hexadecimal

/***********************************************
* Funciones de fragmentacion y envio
************************************************/

//fragmenta y envia un archivo por socket
//retorna 0 si exito, -1 si error
int sendFile(int fd_socket, const char *filepath, uint8_t frame_type,
             const char *origin, const char *destination);

//recibe fragmentos y reensambla un archivo
//retorna 0 si exito, -1 si error
int receiveFile(int fd_socket, const char *output_path, uint8_t expected_type,
                const char *my_realm_name);

/***********************************************
* Funciones de MD5SUM
************************************************/

//calcula el MD5SUM de un archivo usando comando del sistema
//retorna string con hash (caller debe liberar con free), NULL si error
char* calculateMD5(const char *filepath);

//verifica si el MD5 recibido coincide con el archivo local
//retorna 1 si coincide, 0 si no coincide, -1 si error
int verifyMD5(const char *filepath, const char *expected_md5);

//extrae el MD5 de un header con formato "FileName&FileSize&MD5SUM"
//retorna string con MD5 (caller debe liberar con free), NULL si error
char* extractMD5FromHeaderData(const char *header_data);

//envia trama ACK_MD5SUM (0x32) con resultado de verificacion
//check_ok: 1 para CHECK_OK, 0 para CHECK_KO
int sendMD5Response(int fd_socket, int check_ok, const char *my_realm_name);

/***********************************************
* Funciones de transferencia especificas
************************************************/

//envia un archivo de sigil para alianza
int sendSigilFile(int fd_socket, const char *sigil_path,
                  const char *origin, const char *destination);

//recibe y guarda un archivo de sigil
int receiveSigilFile(int fd_socket, const char *output_path, const char *my_realm_name);

//envia lista de productos como archivo
int sendProductsList(int fd_socket, const char *products_file,
                     const char *origin, const char *destination);

//recibe lista de productos
int receiveProductsList(int fd_socket, const char *output_path, const char *my_realm_name);

//envia orden de compra (trade request)
int sendTradeOrder(int fd_socket, const char *order_data, size_t order_size,
                   const char *origin, const char *destination);

#endif
