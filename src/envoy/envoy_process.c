/***********************************************
*
* @Proposit: Codi que executa el proces fill Envoy.
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
* Aquest fitxer conte tot el codi que viu dins del proces hijo creat
* per fork(): el seu bucle principal, els handlers de signal, i la
* logica de cada tipus de mision (PLEDGE, TRADE). El proces pare
* (Maester) no fa servir cap d'aquestes funcions; les crida nomes
* indirectament via envoyMainLoop().
*
************************************************/
#include "envoy.h"

//Delay per hop en segons (simula temps de viatge del corb)
#define HOP_DELAY 5

//Flag que el signal handler posa a 0 per fer-li sortir del bucle
static volatile sig_atomic_t envoy_running = 1;

/***********************************************
*
* @Finalitat: Signal handler del proces Envoy: marca el flag de sortida
* @Parametres: in: sig = numero de signal
* @Retorn: ----
*
************************************************/
static void envoySignalHandler(int sig) {
	if (sig == SIGTERM || sig == SIGINT) {
		envoy_running = 0;
	}
	//re-registrar el handler (defensiu, com fa el professor a signals_2.c)
	signal(sig, envoySignalHandler);
}

/***********************************************
*
* @Finalitat: Configura el handler envoySignalHandler per SIGTERM i SIGINT
*             al proces Envoy
*
************************************************/
static void setupEnvoySignals(void) {
	signal(SIGTERM, envoySignalHandler);
	signal(SIGINT, envoySignalHandler);
}

/***********************************************
*
* @Finalitat: Espera i valida un ACK (0x31) del receptor
* @Parametres: in: fd = socket descriptor
* @Retorn: 0 si ACK valid, -1 si error o timeout
*
************************************************/
static int envoyWaitForAck(int fd) {
	char buffer[FRAME_SIZE];
	Frame ack_frame;
	int wait_result;
	ssize_t bytes_read;

	wait_result = waitForData(fd, 30);
	if (wait_result <= 0) {
		return -1;
	}

	bytes_read = read(fd, buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE) {
		return -1;
	}

	if (deserializeFrame(buffer, &ack_frame) != 0) {
		return -1;
	}

	if (!validateChecksum(&ack_frame)) {
		return -1;
	}

	if (ack_frame.type != FRAME_TYPE_ACK) {
		return -1;
	}

	return 0;
}

/***********************************************
*
* @Finalitat: Executa una mision PLEDGE completa: header + sigil + MD5
* @Parametres: in: mission = dades de la mision
*              out: result = resultat omplert per la funcio
* @Retorn: 0 si exit, -1 si error
*
************************************************/
/***********************************************
*
* @Finalitat: Posa una resposta de fallida al MissionResult i tanca recursos
* @Parametres: in/out: result, in: msg, in: fd a tancar, in: origin a alliberar
* @Retorn: -1 (sempre - per encadenar als returns d'error)
*
************************************************/
static int pledgeMissionFail(MissionResult *result, const char *msg, int fd, char *origin) {
	copiarString(result->result_data, msg, 256);
	if (origin) free(origin);
	if (fd >= 0) close(fd);
	return -1;
}

/***********************************************
*
* @Finalitat: Calcula tamany i MD5 del fitxer sigil i construeix el DATA
*             "Realm&SigilName&FileSize&MD5SUM" per la trama 0x01
* @Parametres: in: mission, out: data_buffer (mida FRAME_DATA_SIZE)
* @Retorn: 0 ok, -1 error
*
************************************************/
static int buildAllianceRequestData(MissionMessage *mission, char *data_buffer) {
	long file_size;
	char *md5sum;
	int sigil_fd;

	sigil_fd = open(mission->sigil_file, O_RDONLY);
	if (sigil_fd < 0) return -1;
	file_size = lseek(sigil_fd, 0, SEEK_END);
	close(sigil_fd);

	md5sum = calculateMD5(mission->sigil_file);
	if (!md5sum) return -1;

	int written = snprintf(data_buffer, FRAME_DATA_SIZE, "%s&%s&%ld&%s",
                           mission->realm_name, mission->sigil_file, file_size, md5sum);
	free(md5sum);
	if (written >= FRAME_DATA_SIZE) {
        // Maneja el error como prefieras (ej. retornar otro código o limpiar el buffer)
        return -2; 
    }
	return 0;
}

/***********************************************
*
* @Finalitat: Envia el MD5 del sigil (trama 0x32) i espera la resposta
*             CHECK_OK / CHECK_KO del receptor
* @Parametres: in: fd, origin, dest, sigil_file
* @Retorn: 0 ok, -1 error o CHECK_KO (caller posa missatge)
*
************************************************/
static int sendAndVerifySigilMD5(int fd, const char *origin, const char *dest,
                                 const char *sigil_file, const char **err_msg) {
	char *md5sum;
	Frame md5_frame, response_frame;
	char buffer[FRAME_SIZE];
	ssize_t bytes_read;

	md5sum = calculateMD5(sigil_file);
	if (!md5sum) { *err_msg = "Failed to calculate MD5"; return -1; }

	createFrame(FRAME_TYPE_ACK_MD5SUM, origin, dest, md5sum, &md5_frame);
	serializeFrame(&md5_frame, buffer);
	if (write(fd, buffer, FRAME_SIZE) != FRAME_SIZE) {
		free(md5sum); *err_msg = "Failed to send MD5"; return -1;
	}
	free(md5sum);

	bytes_read = read(fd, buffer, FRAME_SIZE);
	if (bytes_read != FRAME_SIZE) { *err_msg = "Failed to receive MD5 response"; return -1; }
	if (deserializeFrame(buffer, &response_frame) != 0 ||
	    response_frame.type != FRAME_TYPE_ACK_MD5SUM) {
		*err_msg = "Invalid MD5 response"; return -1;
	}
	if (strncmp(response_frame.data, "CHECK_KO", 8) == 0) {
		*err_msg = "Receiver reported MD5 mismatch"; return -1;
	}
	return 0;
}

/***********************************************
*
* @Finalitat: Executa una mision PLEDGE completa: header + sigil + MD5
* @Parametres: in: mission, out: result
* @Retorn: 0 ok, -1 error (result conte detalls)
*
************************************************/
int executePledgeMission(MissionMessage *mission, MissionResult *result) {
	int fd;
	char *origin;
	Frame request_frame;
	char datos[FRAME_SIZE];
	char data_buffer[FRAME_DATA_SIZE];
	const char *err_msg;

	result->status = MISSION_FAILED;
	result->type = MISSION_PLEDGE;
	copiarString(result->realm, mission->realm, MAX_REALM_NAME);

	fd = openClientConn(mission->target_ip, mission->target_port);
	if (fd < 0) return pledgeMissionFail(result, "Connection failed", -1, NULL);

	asprintf(&origin, "%s:%d", mission->origin_ip, mission->origin_port);

	if (buildAllianceRequestData(mission, data_buffer) != 0) {
		return pledgeMissionFail(result, "Sigil file or MD5 unavailable", fd, origin);
	}

	createFrame(FRAME_TYPE_ALLIANCE_REQUEST, origin, mission->realm, data_buffer, &request_frame);
	serializeFrame(&request_frame, datos);
	if (write(fd, datos, FRAME_SIZE) != FRAME_SIZE) {
		return pledgeMissionFail(result, "Failed to send request", fd, origin);
	}

	if (envoyWaitForAck(fd) != 0) {
		return pledgeMissionFail(result, "No ACK received", fd, origin);
	}

	if (sendSigilFile(fd, mission->sigil_file, origin, mission->realm) != 0) {
		return pledgeMissionFail(result, "Failed to send sigil", fd, origin);
	}

	if (sendAndVerifySigilMD5(fd, origin, mission->realm, mission->sigil_file, &err_msg) != 0) {
		return pledgeMissionFail(result, err_msg, fd, origin);
	}

	free(origin);
	close(fd);

	result->status = MISSION_SUCCESS;
	copiarString(result->result_data, "Sigil sent. Awaiting response via server.", 256);
	return 0;
}

/***********************************************
*
* @Finalitat: Bucle principal del proces Envoy: espera misions per pipe,
*             les executa segons tipus, i contesta amb el resultat.
*             Quan rep EOF al pipe o SIGTERM/SIGINT, surt.
* @Parametres: in: envoy_id = identificador d'aquest Envoy
*              in: pipe_read_fd = fd per llegir misions
*              in: pipe_write_fd = fd per enviar resultats
*              in: config = config compartit (origin, realm, folder)
* @Retorn: ----
*
************************************************/
void envoyMainLoop(int envoy_id, int pipe_read_fd, int pipe_write_fd, EnvoySharedConfig *config) {
	MissionMessage mission;
	MissionResult result;
	ssize_t bytes_read;
	ssize_t bytes_written;

	setupEnvoySignals();

	while (envoy_running) {
		//leer mision del pipe (bloqueante)
		bytes_read = read(pipe_read_fd, &mission, sizeof(MissionMessage));

		if (bytes_read <= 0) {
			//pipe cerrado o error - terminar
			envoy_running = 0;
		} else if (bytes_read != sizeof(MissionMessage)) {
			//mensaje incompleto - ignorar
		} else {
			memset(&result, 0, sizeof(MissionResult));
			result.envoy_id = envoy_id;

			//copiar datos de config a la mision si no estan
			if (mission.origin_ip[0] == '\0') {
				copiarString(mission.origin_ip, config->origin_ip, 20);
			}
			if (mission.origin_port == 0) {
				mission.origin_port = config->origin_port;
			}
			if (mission.realm_name[0] == '\0') {
				copiarString(mission.realm_name, config->realm_name, MAX_REALM_NAME);
			}

			//ejecutar mision segun tipo (con delay de hop)
			//NOTA: MISSION_TRADE NO es procesa per l'Envoy; el TRADE real
			//es interactiu i el gestiona el thread principal a trade.c.
			//Aqui nomes manejem PLEDGE (l'unic tipus que els Envoys envien).
			switch (mission.type) {
				case MISSION_PLEDGE:
					sleep(HOP_DELAY);
					executePledgeMission(&mission, &result);
					break;
				default:
					result.status = MISSION_FAILED;
					result.type = mission.type;
					copiarString(result.result_data, "Unsupported mission type for Envoy", 256);
					break;
			}

			bytes_written = write(pipe_write_fd, &result, sizeof(MissionResult));
			if (bytes_written != sizeof(MissionResult)) {
				//error escribiendo resultado - continuar de todos modos
			}
		}
	}

	close(pipe_read_fd);
	close(pipe_write_fd);
}
