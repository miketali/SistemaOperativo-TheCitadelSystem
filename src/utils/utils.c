/***********************************************
*
* @Proposit: Funciones de utilidad del sistema
* @Autor/s: Maximiliano López y Mikel Hidalgo
* @Data creacio: 01/10/2025
* @Data ultima modificacio: 16/05/2026
*
************************************************/
#include "utils.h"
/***********************************************
*
* @Finalitat: Escribe un string a stdout
* @Parametres: in: str = cadena a escribir
* @Retorn: bytes escritos o -1 si error
*
************************************************/
int writeString(const char *str) {
	int len = strlen(str);
	return write(STDOUT_FILENO, str, len);
}

/***********************************************
*
* @Finalitat: Crea un string con formato "IP:Port"
* @Parametres: in: ip = direccion IP
*              in: port = numero de puerto
* @Retorn: String con formato "IP:Port" (debe liberarse con free)
*           NULL si hay error
*
************************************************/
char* createOriginString(const char *ip, int port) {
	char *origin;
	char *result;

	result = NULL;
	if (asprintf(&origin, "%s:%d", ip, port) >= 0) {
		result = origin;
	}
	return result;
}

/***********************************************
*
* @Finalitat: Parsea un string "IP:Port" en sus componentes
* @Parametres: in: origin = string con formato "IP:Port"
*              out: out_ip = puntero donde guardar la IP (se reserva memoria)
*              out: out_port = puntero donde guardar el puerto
* @Retorn: 0 si ok, -1 si error
*
************************************************/
int parseOriginString(const char *origin, char **out_ip, int *out_port) {
	int i;
	int j;
	int ip_len;
	char *ip;
	int result;

	result = -1;
	if (origin && out_ip && out_port) {
		//buscar posicion del ':'
		i = 0;
		while (origin[i] != ':' && origin[i] != '\0') {
			i++;
		}

		if (origin[i] == ':') {
			//reservar memoria para IP
			ip_len = i;
			ip = malloc((ip_len + 1) * sizeof(char));
			if (ip) {
				//copiar IP
				for (j = 0; j < ip_len; j++) {
					ip[j] = origin[j];
				}
				ip[ip_len] = '\0';

				//parsear puerto
				i++;  //saltar ':'
				*out_port = 0;
				while (origin[i] != '\0' && origin[i] >= '0' && origin[i] <= '9') {
					*out_port = (*out_port) * 10 + (origin[i] - '0');
					i++;
				}

				*out_ip = ip;
				result = 0;
			}
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Llegeix d'un fd byte a byte fins trobar el caracter cEnd
*             (o EOF). El caracter delimitador no s'inclou al resultat.
*             Patro proporcionat als exemples de codi del professor.
* @Parametres: in: fd = file descriptor (0 = stdin)
*              in: cEnd = caracter delimitador (p.ex. '\n')
* @Retorn: string llegit (a alliberar amb free), NULL si EOF sense res
*
************************************************/
char* readUntil(int fd, char cEnd) {
	int i = 0;
	int chars_read = 0;
	char c = ' ';
	char *buffer = NULL;

	while (c != cEnd) {
		chars_read = (int) read(fd, &c, sizeof(char));
		if (chars_read <= 0) {
			//EOF o error: retornar el que tinguem fins ara, o NULL si res
			if (buffer == NULL) return NULL;
			buffer[i] = '\0';
			return buffer;
		}
		if (buffer == NULL) {
			buffer = (char *) malloc(sizeof(char));
		}
		if (c != cEnd) {
			buffer[i] = c;
			buffer = (char *) realloc(buffer, (sizeof(char)) * (i + 2));
		}
		i++;
	}

	buffer[i - 1] = '\0';
	return buffer;
}

/***********************************************
*
* @Finalitat: Convierte string a minusculas (sin usar tolower)
* @Parametres: in/out: str = cadena a convertir
* @Retorn: ----
*
************************************************/
void toLower(char *str) {
	int i;
	for (i = 0; str[i]; i++) {
		if (str[i] >= 'A' && str[i] <= 'Z') {
			str[i] = str[i] + 32;  //diferencia ASCII entre 'A' y 'a'
		}
	}
}

/***********************************************
*
* @Finalitat: Elimina espacios en blanco al inicio y final del string
* @Parametres: in/out: str = cadena a limpiar
* @Retorn: ----
*
************************************************/
void trim(char *str) {
	int start, end, i;

	start = 0;
	while (str[start] && (str[start] == ' ' || str[start] == '\t')) {
		start++;
	}

	end = strlen(str) - 1;
	while (end >= start && (str[end] == ' ' || str[end] == '\t')) {
		end--;
	}

	for (i = 0; i <= end - start; i++) {
		str[i] = str[start + i];
	}
	str[i] = '\0';
}

/***********************************************
*
* @Finalitat: Copia un string de forma segura con limite de caracteres
* @Parametres: out: dest = string destino
*              in: src = string origen
*              in: max = tamaño maximo del destino
* @Retorn: ----
*
************************************************/
void copiarString(char *dest, const char *src, int max) {
	int i;

	i = 0;
	while (i < max - 1 && src[i] != '\0') {
		dest[i] = src[i];
		i++;
	}
	dest[i] = '\0';
}

/***********************************************
*
* @Finalitat: Copia un string con asignacion dinamica
* @Parametres: in: src = string origen
* @Retorn: Puntero al nuevo string (debe liberarse con free)
*           NULL si hay error
*
************************************************/
char* copyStringDynamic(const char *src) {
	char *destino;
	char *result;
	int capacidad;
	int i;
	int error;

	result = NULL;
	//empezar con buffer pequeño
	capacidad = 16;
	destino = malloc(capacidad);
	if (destino) {
		i = 0;
		error = 0;
		while (src[i] != '\0' && !error) {
			//expandir si es necesario
			if (i >= capacidad - 1) {
				capacidad *= 2;
				destino = realloc(destino, capacidad);
				if (!destino) {
					error = 1;
				}
			}
			if (!error) {
				destino[i] = src[i];
				i++;
			}
		}
		if (!error) {
			destino[i] = '\0';
			result = destino;
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Parsea tokens separados por espacios (como strtok pero custom)
* @Parametres: in: str = string a parsear (NULL para continuar)
*              out: next_pos = siguiente posicion
* @Retorn: puntero al token o NULL si no hay mas
*
************************************************/
char* parseToken(char *str, char **next_pos) {
	char *start;
	char *current;
	char *result;

	result = NULL;
	current = NULL;

	//si str es NULL, continuamos desde donde lo dejamos
	if (str != NULL) {
		current = str;
	} else if (next_pos != NULL && *next_pos != NULL) {
		current = *next_pos;
	}

	if (current != NULL) {
		//saltar espacios en blanco iniciales
		while (*current == ' ' || *current == '\t') {
			current++;
		}

		//si llegamos al final, no hay mas tokens
		if (*current == '\0') {
			if (next_pos != NULL) {
				*next_pos = NULL;
			}
		} else {
			//marcar inicio del token
			start = current;

			//avanzar hasta el siguiente espacio o final de string
			while (*current != ' ' && *current != '\t' && *current != '\0') {
				current++;
			}

			//si no es el final, poner un terminador y avanzar
			if (*current != '\0') {
				*current = '\0';
				current++;
			}

			//guardar posicion para la proxima llamada
			if (next_pos != NULL) {
				*next_pos = current;
			}

			result = start;
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Compara strings sin distinguir mayusculas (strcasecmp custom)
* @Parametres: in: s1, s2 = cadenas a comparar
* @Retorn: 0 si iguales, !=0 si diferentes
*
************************************************/
int strcasecmpCustom(const char *s1, const char *s2) {
	int i;
	char c1, c2;
	int result;
	int done;

	i = 0;
	done = 0;
	result = 0;
	while (s1[i] != '\0' && s2[i] != '\0' && !done) {
		c1 = s1[i];
		c2 = s2[i];

		//convertir a minuscula si es mayuscula
		if (c1 >= 'A' && c1 <= 'Z') {
			c1 = c1 + 32;
		}
		if (c2 >= 'A' && c2 <= 'Z') {
			c2 = c2 + 32;
		}

		if (c1 != c2) {
			result = c1 - c2;
			done = 1;
		} else {
			i++;
		}
	}

	if (!done) {
		//si llegamos aqui, verificar si ambos terminaron
		if (s1[i] == '\0' && s2[i] == '\0') {
			result = 0;  // Iguales
		} else {
			//uno es mas largo que el otro
			c1 = s1[i];
			c2 = s2[i];
			if (c1 >= 'A' && c1 <= 'Z') {
				c1 = c1 + 32;
			}
			if (c2 >= 'A' && c2 <= 'Z') {
				c2 = c2 + 32;
			}
			result = c1 - c2;
		}
	}
	return result;
}

/***********************************************
*
* @Finalitat: Cerca la posicio del ultim espai o tabulador d'una cadena
* @Parametres: in: str, in: len = longitud de str
* @Retorn: index de l'ultim espai o -1 si no n'hi ha
*
************************************************/
static int findLastSpace(const char *str, int len) {
	int i;
	int last_space;

	last_space = -1;
	for (i = len - 1; i >= 0 && last_space == -1; i--) {
		if (str[i] == ' ' || str[i] == '\t') {
			last_space = i;
		}
	}
	return last_space;
}

/***********************************************
*
* @Finalitat: Copia els bytes [0, end) de str a un buffer dinamic creixent
* @Parametres: in: str, in: end = index exclusiu fins on copiar
* @Retorn: cadena dinamica (a alliberar) o NULL si error
*
************************************************/
static char* copyProductName(const char *str, int end) {
	int i;
	int capacidad;
	char *nombre_prod;

	capacidad = 16;
	nombre_prod = malloc(capacidad);
	if (!nombre_prod) {
		return NULL;
	}
	for (i = 0; i < end; i++) {
		if (i >= capacidad - 1) {
			capacidad *= 2;
			nombre_prod = realloc(nombre_prod, capacidad);
			if (!nombre_prod) {
				return NULL;
			}
		}
		nombre_prod[i] = str[i];
	}
	nombre_prod[i] = '\0';
	trim(nombre_prod);
	return nombre_prod;
}

/***********************************************
*
* @Finalitat: Llegeix els caracters posteriors a start dins de str i intenta
*             convertir-los a un enter positiu (ignora espais)
* @Parametres: in: str, in: start = primer index a llegir, in: len, out: num
* @Retorn: 1 si tot el text es numeric, 0 en cas contrari
*
************************************************/
static int parseQuantityFromTail(const char *str, int start, int len, int *num) {
	int qty_str_size;
	char *str_cantidad;
	int i;
	int j;
	int valid_number;
	int value;

	qty_str_size = len - start + 1;
	str_cantidad = malloc(qty_str_size * sizeof(char));
	if (!str_cantidad) {
		return 0;
	}
	j = 0;
	for (i = start; i < len; i++) {
		if (str[i] != ' ' && str[i] != '\t') {
			str_cantidad[j++] = str[i];
		}
	}
	str_cantidad[j] = '\0';

	value = 0;
	valid_number = (j > 0);
	for (i = 0; str_cantidad[i] != '\0' && valid_number; i++) {
		if (str_cantidad[i] < '0' || str_cantidad[i] > '9') {
			valid_number = 0;
		} else {
			value = value * 10 + (str_cantidad[i] - '0');
		}
	}
	free(str_cantidad);
	if (valid_number) {
		*num = value;
	}
	return valid_number;
}

/***********************************************
*
* @Finalitat: Parsea un string de producto y cantidad con asignacion dinamica
* @Parametres: in: str = string a parsear
*              out: product = puntero donde guardar el producto (se asigna memoria)
*              out: qty = puntero donde guardar la cantidad
* @Retorn: 1 si el parseo fue exitoso, 0 si fallo
*
************************************************/
int parseProductAndQtyDynamic(const char *str, char **product, int *qty) {
	int last_space;
	int len;
	int num;
	char *nombre_prod;

	len = strlen(str);
	if (len == 0) {
		return 0;
	}
	last_space = findLastSpace(str, len);
	if (last_space == -1) {
		return 0;
	}
	nombre_prod = copyProductName(str, last_space);
	if (!nombre_prod) {
		return 0;
	}
	if (strlen(nombre_prod) == 0) {
		free(nombre_prod);
		return 0;
	}
	if (!parseQuantityFromTail(str, last_space + 1, len, &num)) {
		free(nombre_prod);
		return 0;
	}
	*product = nombre_prod;
	*qty = num;
	return 1;
}
