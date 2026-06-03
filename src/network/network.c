/***********************************************
*
* @Proposit: Implementacion del modulo de networking (sockets)
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "network.h"

/***********************************************
* @Finalidad: Crear socket cliente y conectar a servidor
* @Parametres:
*   in: ip = IP del servidor
*   in: port = puerto
* @Retorn: fd del socket o -1 si error
************************************************/
int openClientConn(char *ip, int port) {
    struct sockaddr_in s_addr;
    int fd_socket;

    //crear socket TCP
    fd_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_socket < 0) {
        return -1;
    }

    //config direccion
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &s_addr.sin_addr) < 0) {
        close(fd_socket);
        return -1;
    }

    //conectar
    if (connect(fd_socket, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
        close(fd_socket);
        return -1;
    }

    return fd_socket;
}

/***********************************************
* @Finalidad: Crear socket servidor TCP en modo escucha
* @Parametres:
*   in: ip = IP donde escuchar
*   in: port = puerto
* @Retorn: fd del socket o -1 si error
************************************************/
int openListenConn(char *ip, int port) {
    struct sockaddr_in s_addr;
    int fd_socket;
    int opt = 1;

    fd_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_socket < 0) {
        return ERR_CODE_SOCKET;
    }

    //para poder reusar el puerto sin esperar TIME_WAIT
    setsockopt(fd_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &s_addr.sin_addr) < 0) {
        close(fd_socket);
        return ERR_CODE_SOCKET;
    }

    if (bind(fd_socket, (void *)&s_addr, sizeof(s_addr)) < 0) {
        close(fd_socket);
        return ERR_CODE_SOCKET;
    }

    listen(fd_socket, MAX_CONN);
    return fd_socket;
}

/***********************************************
* @Finalidad: Esperar datos con timeout (select)
* @Parametres:
*   in: fd_socket = fd del socket
*   in: timeout_seconds = segundos de espera
* @Retorn: 1=datos listos, 0=timeout, -1=error
************************************************/
int waitForData(int fd_socket, int timeout_seconds) {
    struct timeval timeout;
    fd_set read_fds;
    int result;

    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    FD_ZERO(&read_fds);
    FD_SET(fd_socket, &read_fds);

    result = select(fd_socket + 1, &read_fds, NULL, NULL, &timeout);

    if (result == 0) return 0;       //timeout
    if (result < 0) return -1;       //error

    return 1;  //hay datos
}
