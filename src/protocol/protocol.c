/***********************************************
*
* @Proposit: Implementación del protocolo de comunicación de tramas
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "protocol.h"

/***********************************************
*
* @Finalitat: Calcula el checksum de una trama
* @Parametres: in: frame = puntero a la trama
* @Retorn: Checksum calculado (suma de bytes % 65536)
*
************************************************/
uint16_t calculateChecksum(Frame *frame) {
	uint32_t suma;
	int i;
	uint8_t *puntero;

	suma = 0;

	//sumar type (1 byte)
	suma += frame->type;

	//sumar origin (20 bytes)
	for (i = 0; i < FRAME_ORIGIN_SIZE; i++) {
		suma += (uint8_t)frame->origin[i];
	}

	//sumar destination (20 bytes)
	for (i = 0; i < FRAME_DEST_SIZE; i++) {
		suma += (uint8_t)frame->destination[i];
	}

	//sumar data_length (2 bytes) - en network byte order
	puntero = (uint8_t *)&frame->data_length;
	suma += puntero[0];
	suma += puntero[1];

	//sumar data (275 bytes)
	for (i = 0; i < FRAME_DATA_SIZE; i++) {
		suma += (uint8_t)frame->data[i];
	}

	//retornar suma módulo 65536
	return (uint16_t)(suma % 65536);
}

/***********************************************
*
* @Finalitat: Valida el checksum de una trama
* @Parametres: in: frame = puntero a la trama
* @Retorn: 1 si el checksum es válido, 0 si no
*
************************************************/
int validateChecksum(Frame *frame) {
	uint16_t calculado;
	uint16_t almacenado;

	almacenado = frame->checksum;
	calculado = calculateChecksum(frame);

	return (calculado == almacenado) ? 1 : 0;
}

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
                const char *data, Frame *frame) {
	int i;
	int longitud_datos;

	if (!frame) {
		return -1;
	}

	//limpiar estructura
	memset(frame, 0, sizeof(Frame));

	//establecer tipo
	frame->type = type;

	//copiar origin (máximo 19 chars + '\0')
	if (origin) {
		for (i = 0; i < FRAME_ORIGIN_SIZE - 1 && origin[i] != '\0'; i++) {
			frame->origin[i] = origin[i];
		}
		frame->origin[i] = '\0';
	}

	//copiar destination (máximo 19 chars + '\0')
	if (destination) {
		for (i = 0; i < FRAME_DEST_SIZE - 1 && destination[i] != '\0'; i++) {
			frame->destination[i] = destination[i];
		}
		frame->destination[i] = '\0';
	}

	//copiar data (máximo 274 chars + '\0')
	if (data) {
		longitud_datos = 0;
		for (i = 0; i < FRAME_DATA_SIZE - 1 && data[i] != '\0'; i++) {
			frame->data[i] = data[i];
			longitud_datos++;
		}
		frame->data[i] = '\0';
		frame->data_length = (uint16_t)longitud_datos;
	} else {
		frame->data_length = 0;
	}

	//calcular checksum
	frame->checksum = calculateChecksum(frame);

	return 0;
}

/***********************************************
*
* @Finalitat: Serializa una trama a un buffer de 320 bytes
* @Parametres: in: frame = puntero a la trama
*              out: buffer = buffer de 320 bytes donde serializar
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int serializeFrame(Frame *frame, char *buffer) {
	int desplazamiento;
	uint16_t longitud_datos_net;
	uint16_t checksum_net;

	if (!frame || !buffer) {
		return -1;
	}

	desplazamiento = 0;

	//serializar type (1 byte)
	buffer[desplazamiento] = (char)frame->type;
	desplazamiento += FRAME_TYPE_SIZE;

	//serializar origin (20 bytes)
	memcpy(buffer + desplazamiento, frame->origin, FRAME_ORIGIN_SIZE);
	desplazamiento += FRAME_ORIGIN_SIZE;

	//serializar destination (20 bytes)
	memcpy(buffer + desplazamiento, frame->destination, FRAME_DEST_SIZE);
	desplazamiento += FRAME_DEST_SIZE;

	//serializar data_length (2 bytes) - network byte order
	longitud_datos_net = htons(frame->data_length);
	memcpy(buffer + desplazamiento, &longitud_datos_net, FRAME_DATA_LENGTH_SIZE);
	desplazamiento += FRAME_DATA_LENGTH_SIZE;

	//serializar data (275 bytes)
	memcpy(buffer + desplazamiento, frame->data, FRAME_DATA_SIZE);
	desplazamiento += FRAME_DATA_SIZE;

	//serializar checksum (2 bytes) - network byte order
	checksum_net = htons(frame->checksum);
	memcpy(buffer + desplazamiento, &checksum_net, FRAME_CHECKSUM_SIZE);
	desplazamiento += FRAME_CHECKSUM_SIZE;

	return 0;
}

/***********************************************
*
* @Finalitat: Deserializa un buffer de 320 bytes a una trama
* @Parametres: in: buffer = buffer de 320 bytes
*              out: frame = puntero a la trama donde deserializar
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int deserializeFrame(const char *buffer, Frame *frame) {
	int desplazamiento;
	uint16_t longitud_datos_net;
	uint16_t checksum_net;

	if (!buffer || !frame) {
		return -1;
	}

	desplazamiento = 0;

	//deserializar type (1 byte)
	frame->type = (uint8_t)buffer[desplazamiento];
	desplazamiento += FRAME_TYPE_SIZE;

	//deserializar origin (20 bytes)
	memcpy(frame->origin, buffer + desplazamiento, FRAME_ORIGIN_SIZE);
	desplazamiento += FRAME_ORIGIN_SIZE;

	//deserializar destination (20 bytes)
	memcpy(frame->destination, buffer + desplazamiento, FRAME_DEST_SIZE);
	desplazamiento += FRAME_DEST_SIZE;

	//deserializar data_length (2 bytes) - network byte order
	memcpy(&longitud_datos_net, buffer + desplazamiento, FRAME_DATA_LENGTH_SIZE);
	frame->data_length = ntohs(longitud_datos_net);
	desplazamiento += FRAME_DATA_LENGTH_SIZE;

	//deserializar data (275 bytes)
	memcpy(frame->data, buffer + desplazamiento, FRAME_DATA_SIZE);
	desplazamiento += FRAME_DATA_SIZE;

	//deserializar checksum (2 bytes) - network byte order
	memcpy(&checksum_net, buffer + desplazamiento, FRAME_CHECKSUM_SIZE);
	frame->checksum = ntohs(checksum_net);
	desplazamiento += FRAME_CHECKSUM_SIZE;

	return 0;
}

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
              const char *destination, const char *data) {
	Frame frame;
	char buffer[FRAME_SIZE];

	if (createFrame(type, origin, destination, data, &frame) != 0) {
		return -1;
	}

	if (serializeFrame(&frame, buffer) != 0) {
		return -1;
	}

	if (write(fd, buffer, FRAME_SIZE) != FRAME_SIZE) {
		return -1;
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Recibe y valida una trama con timeout opcional
* @Parametres: in: fd = file descriptor del socket
*              out: frame = puntero donde guardar la trama recibida
*              in: expected_type = tipo de trama esperado (0 para cualquiera)
*              in: timeout_seconds = timeout en segundos (0 para sin timeout)
* @Retorn: 0 si éxito, -1 si error/timeout, -2 si tipo incorrecto
*
************************************************/
int receiveAndValidateFrame(int fd, Frame *frame, uint8_t expected_type,
                            int timeout_seconds) {
	char buffer[FRAME_SIZE];
	ssize_t bytes_read;

	if (!frame) {
		return -1;
	}

	//aplicar timeout si se especifica
	if (timeout_seconds > 0) {
		fd_set read_fds;
		struct timeval tv;
		int result;

		FD_ZERO(&read_fds);
		FD_SET(fd, &read_fds);
		tv.tv_sec = timeout_seconds;
		tv.tv_usec = 0;

		result = select(fd + 1, &read_fds, NULL, NULL, &tv);
		if (result <= 0) {
			return -1;  //timeout o error
		}
	}

	//leer trama
	bytes_read = read(fd, buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE) {
		return -1;
	}

	//deserializar
	if (deserializeFrame(buffer, frame) != 0) {
		return -1;
	}

	//validar checksum
	if (!validateChecksum(frame)) {
		return -1;
	}

	//verificar tipo si se especifica
	if (expected_type != 0 && frame->type != expected_type) {
		return -2;  //tipo incorrecto
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Envia una trama ACK con el nombre del reino
* @Parametres: in: fd = file descriptor del socket
*              in: realm_name = nombre del reino a incluir en OK&RealmName
* @Retorn: 0 si éxito, -1 si error
*
************************************************/
int sendACKFrame(int fd, const char *realm_name) {
	char ack_data[100];

	snprintf(ack_data, sizeof(ack_data), "OK&%s", realm_name ? realm_name : "");
	return sendFrame(fd, FRAME_TYPE_ACK, "", "", ack_data);
}
