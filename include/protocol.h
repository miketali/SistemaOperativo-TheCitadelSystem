/***********************************************
*
* @Proposit: Header del protocolo de comunicación de tramas
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>
//tamaño total de la trama
#define FRAME_SIZE 320

//tamaños de los campos
#define FRAME_TYPE_SIZE 1
#define FRAME_ORIGIN_SIZE 20
#define FRAME_DEST_SIZE 20
#define FRAME_DATA_LENGTH_SIZE 2
#define FRAME_DATA_SIZE 275
#define FRAME_CHECKSUM_SIZE 2

//tipos de trama (Fase 2)
#define FRAME_TYPE_ALLIANCE_REQUEST 0x01
#define FRAME_TYPE_ALLIANCE_RESPONSE 0x03
#define FRAME_TYPE_PRODUCT_LIST 0x11
#define FRAME_TYPE_TRADE_REQUEST 0x14
#define FRAME_TYPE_ERROR_UNKNOWN_REALM 0x21
#define FRAME_TYPE_ERROR_UNAUTHORIZED 0x25
#define FRAME_TYPE_DISCONNECT 0x27
#define FRAME_TYPE_ACK 0x31
#define FRAME_TYPE_NACK 0x69

//tipos de trama adicionales (Fase 3)
#define FRAME_TYPE_ALLIANCE_SIGIL_DATA 0x02     //datos del sigil en alianza
#define FRAME_TYPE_PRODUCTS_LIST_REQUEST 0x12  //peticion de lista de productos
#define FRAME_TYPE_PRODUCTS_LIST_DATA 0x13     //datos de lista de productos
#define FRAME_TYPE_TRADE_ORDER 0x15            //orden de compra
#define FRAME_TYPE_TRADE_RESPONSE 0x16         //respuesta de compra
#define FRAME_TYPE_ACK_MD5SUM 0x32             //ACK con checksum MD5

//tipos de trama adicionales (Fase 4)
//0x03 ALLIANCE_RESPONSE se usa con DATA: ACCEPT&RealmName o REJECT&RealmName

/***********************************************
*
* @Finalitat: Estructura de una trama de 320 bytes
* @Campos:
*   - type: Tipo de trama (1 byte)
*   - origin: IP:Port del origen (20 bytes)
*   - destination: Nombre del reino destino (20 bytes)
*   - data_length: Longitud de los datos (2 bytes)
*   - data: Datos de la trama (275 bytes)
*   - checksum: Checksum de validación (2 bytes)
*
************************************************/
typedef struct __attribute__((packed)) {
	uint8_t type;
	char origin[FRAME_ORIGIN_SIZE];
	char destination[FRAME_DEST_SIZE];
	uint16_t data_length;
	char data[FRAME_DATA_SIZE];
	uint16_t checksum;
} Frame;

/***********************************************
*
* @Finalitat: Calcula el checksum de una trama
* @Parametres: in: frame = puntero a la trama
* @Retorn: Checksum calculado (suma de bytes % 65536)
*
************************************************/
uint16_t calculateChecksum(Frame *frame);

/***********************************************
*
* @Finalitat: Valida el checksum de una trama
* @Parametres: in: frame = puntero a la trama
* @Retorn: 1 si el checksum es válido, 0 si no
*
************************************************/
int validateChecksum(Frame *frame);

/***********************************************
*
* @Finalitat: Crea una trama con los datos proporcionados
* @Parametres: in: type = tipo de trama
*              in: origin = IP:Port del origen
*              in: destination = nombre del reino destino
*              in: data = datos a incluir en la trama
*              out: frame = puntero a la trama a llenar
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int createFrame(uint8_t type, const char *origin, const char *destination,
                const char *data, Frame *frame);

/***********************************************
*
* @Finalitat: Serializa una trama a un buffer de 320 bytes
* @Parametres: in: frame = puntero a la trama
*              out: buffer = buffer de 320 bytes donde serializar
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int serializeFrame(Frame *frame, char *buffer);

/***********************************************
*
* @Finalitat: Deserializa un buffer de 320 bytes a una trama
* @Parametres: in: buffer = buffer de 320 bytes
*              out: frame = puntero a la trama donde deserializar
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int deserializeFrame(const char *buffer, Frame *frame);

/***********************************************
*
* @Finalitat: Crea, serializa y envia una trama en un solo paso
* @Parametres: in: fd = file descriptor del socket
*              in: type = tipo de trama
*              in: origin = IP:Port del origen
*              in: destination = nombre del reino destino
*              in: data = datos a incluir en la trama
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int sendFrame(int fd, uint8_t type, const char *origin,
              const char *destination, const char *data);

/***********************************************
*
* @Finalitat: Recibe y valida una trama con timeout
* @Parametres: in: fd = file descriptor del socket
*              out: frame = puntero donde guardar la trama recibida
*              in: expected_type = tipo de trama esperado (0 para cualquiera)
*              in: timeout_seconds = timeout en segundos (0 para sin timeout)
* @Retorn: 0 si éxito, -1 si error/timeout, -2 si tipo incorrecto
*
************************************************/
int receiveAndValidateFrame(int fd, Frame *frame, uint8_t expected_type,
                            int timeout_seconds);

/***********************************************
*
* @Finalitat: Envia una trama ACK con el nombre del reino
* @Parametres: in: fd = file descriptor del socket
*              in: realm_name = nombre del reino a incluir en OK&RealmName
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int sendACKFrame(int fd, const char *realm_name);

#endif
