/***********************************************
*
* @Proposit: Header para el modulo de networking (sockets)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _NETWORK_H
#define _NETWORK_H

#include <arpa/inet.h>
#include <sys/socket.h>

#include "utils.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
//constantes para networking
#define MAX_CONN 5
#define ERR_CODE_SOCKET -1

/***********************************************
* @Finalidad: Crear y conectar un socket cliente
* @Parametres:
*   in: ip = direccion IP del servidor (string)
*   in: port = puerto del servidor
* @Retorn: file descriptor del socket (>= 0) o -1 si error
************************************************/
int openClientConn(char *ip, int port);

/***********************************************
* @Finalidad: Crear y configurar un socket servidor
* @Parametres:
*   in: ip = direccion IP donde escuchar (string)
*   in: port = puerto donde escuchar
* @Retorn: file descriptor del socket (>= 0) o -1 si error
************************************************/
int openListenConn(char *ip, int port);

/***********************************************
* @Finalidad: Esperar datos en un socket con timeout usando select()
* @Parametres:
*   in: fd_socket = file descriptor del socket
*   in: timeout_seconds = timeout en segundos (ej: 120 para 2 minutos)
* @Retorn: 1 si hay datos listos, 0 si timeout, -1 si error
************************************************/
int waitForData(int fd_socket, int timeout_seconds);

#endif
