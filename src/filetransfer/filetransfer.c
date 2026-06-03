/***********************************************
*
* @Proposit: Modulo de transferencia de archivos para Fase 3
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Implementa:
* - Fragmentacion de archivos en tramas de 275 bytes
* - Reensamblaje de archivos recibidos
* - Calculo de MD5SUM usando comando del sistema (md5sum)
* - Verificacion de integridad
*
* Nota: El MD5 DEBE calcularse usando el comando del sistema,
* NO se permite implementacion propia del algoritmo MD5.
*
************************************************/
#include "filetransfer.h"

#include <stdio.h>  //para sprintf (permitido)

/***********************************************
*
* @Finalitat: Calcula el MD5SUM de un archivo usando comando del sistema
* @Parametres: in: filepath = ruta al archivo
* @Retorn: String con hash MD5 (32 chars hex), NULL si error
*           El caller debe liberar la memoria con free()
*
************************************************/
/***********************************************
*
* @Finalitat: Construeix la comanda md5sum / md5 -r segons SO
* @Parametres: in: filepath
* @Retorn: string amb la comanda (a alliberar) o NULL si error
*
************************************************/
static char* buildMD5Command(const char *filepath) {
	char *command;
#ifdef __APPLE__
	if (asprintf(&command, "md5 -r \"%s\"", filepath) < 0) return NULL;
#else
	if (asprintf(&command, "md5sum \"%s\"", filepath) < 0) return NULL;
#endif
	return command;
}

/***********************************************
*
* @Finalitat: Codi que executa el proces hijo: redirigeix stdout al pipe
*             i fa exec del shell amb la comanda
* @Parametres: in: command, in: pipe_read_fd, in: pipe_write_fd
* @Retorn: ----  (mai retorna; _exit en error)
*
************************************************/
static void md5ChildExec(const char *command, int pipe_read_fd, int pipe_write_fd) {
	int fd_to_close;
	int max_fd;

	close(pipe_read_fd);
	dup2(pipe_write_fd, STDOUT_FILENO);
	dup2(pipe_write_fd, STDERR_FILENO);
	close(pipe_write_fd);

	max_fd = (int)sysconf(_SC_OPEN_MAX);
	if (max_fd < 0) max_fd = 1024;
	for (fd_to_close = 3; fd_to_close < max_fd; fd_to_close++) {
		close(fd_to_close);
	}

	execlp("sh", "sh", "-c", command, NULL);
	_exit(1);
}

/***********************************************
*
* @Finalitat: Llegeix tot el contingut del pipe fins EOF o omplir el buffer
* @Parametres: in: fd_read, out: buffer, in: buffer_size
* @Retorn: bytes llegits (0 si error)
*
************************************************/
static ssize_t readPipeAll(int fd_read, char *buffer, ssize_t buffer_size) {
	ssize_t total = 0;
	ssize_t n = 1;

	while (total < buffer_size - 1 && n > 0) {
		n = read(fd_read, buffer + total, (size_t)(buffer_size - 1 - total));
		if (n > 0) total += n;
	}
	if (total > 0) buffer[total] = '\0';
	return total;
}

/***********************************************
*
* @Finalitat: Extreu els 32 caracters de hash del buffer de sortida
* @Parametres: in: buffer
* @Retorn: hash de 32 caracters (a alliberar), NULL si format invalid
*
************************************************/
static char* extractMD5Hash(const char *buffer) {
	char *hash;
	int i;

	hash = malloc(sizeof(char) * (MD5_HASH_LENGTH + 1));
	if (!hash) return NULL;
	for (i = 0; i < MD5_HASH_LENGTH && buffer[i] != '\0' && buffer[i] != ' '; i++) {
		hash[i] = buffer[i];
	}
	hash[i] = '\0';
	if (i != MD5_HASH_LENGTH) { free(hash); return NULL; }
	return hash;
}

/***********************************************
*
* @Finalitat: Calcula el MD5 d'un fitxer fent fork+exec de md5/md5sum
* @Parametres: in: filepath
* @Retorn: string de 32 caracters (a alliberar), NULL si error
*
************************************************/
char* calculateMD5(const char *filepath) {
	char *command;
	char *buffer;
	char *hash;
	int fd_pipe[2];
	pid_t pid;
	int status;
	ssize_t buffer_size;
	ssize_t total_read;

	if (!filepath) return NULL;

	command = buildMD5Command(filepath);
	if (!command) return NULL;

	if (pipe(fd_pipe) < 0) { free(command); return NULL; }

	pid = fork();
	if (pid < 0) {
		close(fd_pipe[0]); close(fd_pipe[1]); free(command); return NULL;
	}
	if (pid == 0) {
		md5ChildExec(command, fd_pipe[0], fd_pipe[1]);
	}

	close(fd_pipe[1]);
	free(command);

	buffer_size = MD5_HASH_LENGTH + 1 + (ssize_t)strlen(filepath) + 2;
	buffer = malloc(sizeof(char) * (size_t)buffer_size);
	if (!buffer) {
		close(fd_pipe[0]); waitpid(pid, &status, 0); return NULL;
	}

	total_read = readPipeAll(fd_pipe[0], buffer, buffer_size);
	close(fd_pipe[0]);
	waitpid(pid, &status, 0);

	if (total_read <= 0) { free(buffer); return NULL; }

	hash = extractMD5Hash(buffer);
	free(buffer);
	return hash;
}

/***********************************************
*
* @Finalitat: Verifica si el MD5 recibido coincide con el archivo local
* @Parametres: in: filepath = ruta al archivo local
*              in: expected_md5 = hash MD5 esperado (32 chars)
* @Retorn: 1 si coincide, 0 si no coincide, -1 si error
*
************************************************/
int verifyMD5(const char *filepath, const char *expected_md5) {
	char *calculated_md5;
	int result;
	int i;

	if (filepath == NULL || expected_md5 == NULL) {
		return -1;
	}

	calculated_md5 = calculateMD5(filepath);
	if (calculated_md5 == NULL) {
		return -1;
	}

	//comparar ignorando mayusculas/minusculas
	result = 1;
	for (i = 0; i < MD5_HASH_LENGTH && result; i++) {
		char c1 = calculated_md5[i];
		char c2 = expected_md5[i];

		//convertir a minusculas para comparar
		if (c1 >= 'A' && c1 <= 'F') {
			c1 = c1 - 'A' + 'a';
		}
		if (c2 >= 'A' && c2 <= 'F') {
			c2 = c2 - 'A' + 'a';
		}

		if (c1 != c2) {
			result = 0;
		}
	}

	free(calculated_md5);
	return result;
}

/***********************************************
*
* @Finalitat: Extrae el MD5 de un header con formato "FileName&FileSize&MD5SUM"
* @Parametres: in: header_data = string con formato "FileName&FileSize&MD5SUM"
* @Retorn: String con MD5 (caller debe liberar con free), NULL si error
*
************************************************/
char* extractMD5FromHeaderData(const char *header_data) {
	char *first_amp;
	char *second_amp;
	char *md5;

	if (!header_data) {
		return NULL;
	}

	//buscar primer '&'
	first_amp = strchr(header_data, '&');
	if (!first_amp) {
		return NULL;
	}

	//buscar segundo '&'
	second_amp = strchr(first_amp + 1, '&');
	if (!second_amp) {
		return NULL;
	}

	//el MD5 esta despues del segundo '&'
	md5 = malloc(MD5_HASH_LENGTH + 1);
	if (!md5) {
		return NULL;
	}

	strncpy(md5, second_amp + 1, MD5_HASH_LENGTH);
	md5[MD5_HASH_LENGTH] = '\0';

	return md5;
}

/***********************************************
*
* @Finalitat: Envia trama ACK_MD5SUM (0x32) con resultado de verificacion
* @Parametres: in: fd_socket = socket donde enviar
*              in: check_ok = 1 para CHECK_OK, 0 para CHECK_KO
*              in: my_realm_name = nombre del reino que envia
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int sendMD5Response(int fd_socket, int check_ok, const char *my_realm_name) {
	Frame frame;
	char buffer[FRAME_SIZE];
	char data[100];
	ssize_t bytes_written;

	//construir DATA: CHECK_OK&RealmName o CHECK_KO&RealmName
	if (check_ok) {
		snprintf(data, sizeof(data), "CHECK_OK&%s", my_realm_name ? my_realm_name : "");
	} else {
		snprintf(data, sizeof(data), "CHECK_KO&%s", my_realm_name ? my_realm_name : "");
	}

	//crear trama con ORIGIN y DESTINATION vacios
	if (createFrame(FRAME_TYPE_ACK_MD5SUM, "", "", data, &frame) < 0) {
		return -1;
	}

	//serializar y enviar
	if (serializeFrame(&frame, buffer) < 0) {
		return -1;
	}

	bytes_written = write(fd_socket, buffer, FRAME_SIZE);
	if (bytes_written != FRAME_SIZE) {
		return -1;
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Construeix una trama de fragment amb les dades llegides
* @Parametres: in: frame_type, in: origin, in: destination, in: data_buffer,
*              in: bytes_read, out: frame
* @Retorn: ----
*
************************************************/
static void buildFragmentFrame(uint8_t frame_type, const char *origin,
                               const char *destination, const char *data_buffer,
                               ssize_t bytes_read, Frame *frame) {
	memset(frame, 0, sizeof(Frame));
	frame->type = frame_type;
	strncpy(frame->origin, origin, FRAME_ORIGIN_SIZE - 1);
	frame->origin[FRAME_ORIGIN_SIZE - 1] = '\0';
	strncpy(frame->destination, destination, FRAME_DEST_SIZE - 1);
	frame->destination[FRAME_DEST_SIZE - 1] = '\0';
	frame->data_length = (uint16_t)bytes_read;
	memcpy(frame->data, data_buffer, bytes_read);
	frame->checksum = calculateChecksum(frame);
}

/***********************************************
*
* @Finalitat: Espera i valida la recepcio d'un ACK pel socket
* @Parametres: in: fd_socket
* @Retorn: 0 si es rep un ACK valid, -1 en cas contrari
*
************************************************/
static int waitForAck(int fd_socket) {
	char ack_buffer[FRAME_SIZE];
	Frame ack_frame;
	ssize_t ack_read;

	ack_read = read(fd_socket, ack_buffer, FRAME_SIZE);
	if (ack_read != FRAME_SIZE) {
		return -1;
	}
	if (deserializeFrame(ack_buffer, &ack_frame) < 0) {
		return -1;
	}
	if (ack_frame.type != FRAME_TYPE_ACK) {
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Serialitza i envia una trama, despres espera l'ACK
* @Parametres: in: fd_socket, in: frame
* @Retorn: 0 si exit, -1 si error
*
************************************************/
static int sendFrameAndWaitAck(int fd_socket, Frame *frame) {
	char frame_buffer[FRAME_SIZE];
	ssize_t bytes_written;

	if (serializeFrame(frame, frame_buffer) < 0) {
		return -1;
	}
	bytes_written = write(fd_socket, frame_buffer, FRAME_SIZE);
	if (bytes_written != FRAME_SIZE) {
		return -1;
	}
	return waitForAck(fd_socket);
}

/***********************************************
*
* @Finalitat: Llegeix l'arxiu en fragments i els envia un per un pel socket
* @Parametres: in: fd_socket, in: fd_file, in: frame_type, in: origin, in: destination
* @Retorn: 0 si exit, -1 si error
*
************************************************/
static int sendFileFragments(int fd_socket, int fd_file, uint8_t frame_type,
                             const char *origin, const char *destination) {
	char data_buffer[MAX_FRAGMENT_SIZE];
	Frame frame;
	ssize_t bytes_read;

	while ((bytes_read = read(fd_file, data_buffer, MAX_FRAGMENT_SIZE)) > 0) {
		if (bytes_read < MAX_FRAGMENT_SIZE) {
			data_buffer[bytes_read] = '\0';
		}
		buildFragmentFrame(frame_type, origin, destination, data_buffer, bytes_read, &frame);
		if (sendFrameAndWaitAck(fd_socket, &frame) < 0) {
			return -1;
		}
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Envia la trama final (data_length=0) i espera l'ACK final
* @Parametres: in: fd_socket, in: frame_type, in: origin, in: destination
* @Retorn: 0 si exit, -1 si error
*
************************************************/
static int sendEndOfFileFrame(int fd_socket, uint8_t frame_type,
                              const char *origin, const char *destination) {
	Frame frame;

	memset(&frame, 0, sizeof(Frame));
	frame.type = frame_type;
	strncpy(frame.origin, origin, FRAME_ORIGIN_SIZE - 1);
	strncpy(frame.destination, destination, FRAME_DEST_SIZE - 1);
	frame.data_length = 0;
	frame.checksum = calculateChecksum(&frame);
	return sendFrameAndWaitAck(fd_socket, &frame);
}

/***********************************************
*
* @Finalitat: Fragmenta y envia un archivo por socket
* @Parametres: in: fd_socket = socket donde enviar
*              in: filepath = ruta al archivo a enviar
*              in: frame_type = tipo de trama para los fragmentos
*              in: origin = IP:Port origen
*              in: destination = reino destino
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int sendFile(int fd_socket, const char *filepath, uint8_t frame_type,
             const char *origin, const char *destination) {
	int fd_file;

	if (filepath == NULL || origin == NULL || destination == NULL) {
		return -1;
	}

	fd_file = open(filepath, O_RDONLY);
	if (fd_file < 0) {
		return -1;
	}

	if (sendFileFragments(fd_socket, fd_file, frame_type, origin, destination) < 0) {
		close(fd_file);
		return -1;
	}
	close(fd_file);

	return sendEndOfFileFrame(fd_socket, frame_type, origin, destination);
}

/***********************************************
*
* @Finalitat: Envia una trama ACK simple amb data "OK&<realm>"
* @Parametres: in: fd_socket, in: my_realm_name
* @Retorn: 0 si exit, -1 si error
*
************************************************/
static int sendOkAck(int fd_socket, const char *my_realm_name) {
	Frame ack_frame;
	char ack_buffer[FRAME_SIZE];
	char ack_data[100];
	ssize_t bytes_written;

	snprintf(ack_data, sizeof(ack_data), "OK&%s", my_realm_name ? my_realm_name : "");
	createFrame(FRAME_TYPE_ACK, "", "", ack_data, &ack_frame);
	if (serializeFrame(&ack_frame, ack_buffer) < 0) {
		return -1;
	}
	bytes_written = write(fd_socket, ack_buffer, FRAME_SIZE);
	if (bytes_written != FRAME_SIZE) {
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Llegeix una trama del socket i la valida (checksum i tipus)
* @Parametres: in: fd_socket, in: expected_type, out: frame
* @Retorn: 0 si trama valida, -1 si error
*
************************************************/
static int readAndValidateFrame(int fd_socket, uint8_t expected_type, Frame *frame) {
	char frame_buffer[FRAME_SIZE];
	ssize_t bytes_read;

	bytes_read = read(fd_socket, frame_buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE) {
		return -1;
	}
	if (deserializeFrame(frame_buffer, frame) < 0) {
		return -1;
	}
	if (!validateChecksum(frame)) {
		return -1;
	}
	if (frame->type != expected_type) {
		return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Bucle principal de recepcio: llegeix fragments, els escriu i envia ACK
* @Parametres: in: fd_socket, in: fd_file, in: expected_type, in: my_realm_name
* @Retorn: 0 si exit, -1 si error
*
************************************************/
static int receiveFileLoop(int fd_socket, int fd_file, uint8_t expected_type,
                           const char *my_realm_name) {
	Frame frame;
	ssize_t bytes_written;
	int done;
	int error;

	done = 0;
	error = 0;
	while (!done && !error) {
		if (readAndValidateFrame(fd_socket, expected_type, &frame) < 0) {
			error = 1;
		} else if (frame.data_length == 0) {
			sendOkAck(fd_socket, my_realm_name);
			done = 1;
		} else {
			bytes_written = write(fd_file, frame.data, frame.data_length);
			if (bytes_written != frame.data_length) {
				error = 1;
			} else if (sendOkAck(fd_socket, my_realm_name) < 0) {
				error = 1;
			}
		}
	}
	return error ? -1 : 0;
}

/***********************************************
*
* @Finalitat: Recibe fragmentos y reensambla un archivo
* @Parametres: in: fd_socket = socket de donde recibir
*              in: output_path = ruta donde guardar el archivo
*              in: expected_type = tipo de trama esperado
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int receiveFile(int fd_socket, const char *output_path, uint8_t expected_type,
                const char *my_realm_name) {
	int fd_file;
	int result;

	if (output_path == NULL) {
		return -1;
	}

	fd_file = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_file < 0) {
		return -1;
	}

	result = receiveFileLoop(fd_socket, fd_file, expected_type, my_realm_name);
	if (result == 0) {
		fsync(fd_file);
	}
	close(fd_file);
	return result;
}

/***********************************************
*
* @Finalitat: Envia un archivo de sigil para alianza
* @Parametres: in: fd_socket = socket donde enviar
*              in: sigil_path = ruta al archivo sigil
*              in: origin = IP:Port origen
*              in: destination = reino destino
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int sendSigilFile(int fd_socket, const char *sigil_path,
                  const char *origin, const char *destination) {
	return sendFile(fd_socket, sigil_path, FRAME_TYPE_ALLIANCE_SIGIL_DATA,
	                origin, destination);
}

/***********************************************
*
* @Finalitat: Recibe y guarda un archivo de sigil
* @Parametres: in: fd_socket = socket de donde recibir
*              in: output_path = ruta donde guardar
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int receiveSigilFile(int fd_socket, const char *output_path, const char *my_realm_name) {
	return receiveFile(fd_socket, output_path, FRAME_TYPE_ALLIANCE_SIGIL_DATA, my_realm_name);
}

/***********************************************
*
* @Finalitat: Envia lista de productos como archivo
* @Parametres: in: fd_socket = socket donde enviar
*              in: products_file = ruta al archivo de productos
*              in: origin = IP:Port origen
*              in: destination = reino destino
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int sendProductsList(int fd_socket, const char *products_file,
                     const char *origin, const char *destination) {
	return sendFile(fd_socket, products_file, FRAME_TYPE_PRODUCTS_LIST_DATA,
	                origin, destination);
}

/***********************************************
*
* @Finalitat: Recibe lista de productos
* @Parametres: in: fd_socket = socket de donde recibir
*              in: output_path = ruta donde guardar
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int receiveProductsList(int fd_socket, const char *output_path, const char *my_realm_name) {
	return receiveFile(fd_socket, output_path, FRAME_TYPE_PRODUCTS_LIST_DATA, my_realm_name);
}

/***********************************************
*
* @Finalitat: Envia orden de compra (trade request)
* @Parametres: in: fd_socket = socket donde enviar
*              in: order_data = datos de la orden
*              in: order_size = tamaño de los datos
*              in: origin = IP:Port origen
*              in: destination = reino destino
* @Retorn: 0 si exito, -1 si error
*
************************************************/
int sendTradeOrder(int fd_socket, const char *order_data, size_t order_size,
                   const char *origin, const char *destination) {
	Frame frame;
	char buffer[FRAME_SIZE];
	ssize_t bytes_written;

	if (order_data == NULL || order_size == 0) {
		return -1;
	}

	//si la orden cabe en una trama
	if (order_size <= MAX_FRAGMENT_SIZE) {
		memset(&frame, 0, sizeof(Frame));
		frame.type = FRAME_TYPE_TRADE_ORDER;
		strncpy(frame.origin, origin, FRAME_ORIGIN_SIZE - 1);
		strncpy(frame.destination, destination, FRAME_DEST_SIZE - 1);
		frame.data_length = (uint16_t)order_size;
		memcpy(frame.data, order_data, order_size);
		frame.checksum = calculateChecksum(&frame);

		if (serializeFrame(&frame, buffer) < 0) {
			return -1;
		}

		bytes_written = write(fd_socket, buffer, FRAME_SIZE);
		if (bytes_written != FRAME_SIZE) {
			return -1;
		}

		return 0;
	}

	//TODO: si la orden es mas grande, fragmentar
	return -1;
}

