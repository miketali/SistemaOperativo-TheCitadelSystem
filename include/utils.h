/***********************************************
*
* @Proposit: Header de funciones de utilidad
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/

#ifndef _UTILS_H
#define _UTILS_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

//funciones de I/O basicas
int writeString(const char *str);
char* readUntil(int fd, char cEnd);
char* copyStringDynamic(const char *src);

//funciones de string
void toLower(char *str);
void trim(char *str);
void copiarString(char *dest, const char *src, int max);
char* parseToken(char *str, char **next_pos);
int strcasecmpCustom(const char *s1, const char *s2);
int parseProductAndQtyDynamic(const char *str, char **product, int *qty);

//funciones de parsing IP:Port
char* createOriginString(const char *ip, int port);
int parseOriginString(const char *origin, char **out_ip, int *out_port);

#endif
