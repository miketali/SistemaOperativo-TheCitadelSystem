# SistemaOperativo-TheCitadelSystem
Este proyecto implementa un sistema distribuido en C que simula una infraestructura de red e intercambio comercial entre reinos independientes. Su arquitectura híbrida combina un servidor de red multihilo (pthreads) para atender peticiones concurrentes de aliados —sincronizados de forma segura mediante mutexes— con un pool de procesos hijos independientes (fork) que ejecutan misiones de forma asíncrona comunicándose por pipes. Finalmente, gestiona un protocolo de comunicación con fragmentación estricta de tramas (320 bytes) y delega tareas críticas de integridad al propio sistema operativo mediante execlp (md5sum).

## 📋 Requisitos del Sistema

Este sistema está diseñado para entornos basados en **POSIX (Linux / macOS)**.
* **Compilador:** GCC (soporte para C11).
* **Herramientas de construcción:** GNU Make.
* **Dependencias del Sistema:** Comando estándar `md5sum` (Linux) o `md5` (macOS) instalado en el PATH.

## 🕹️ Comandos Disponibles (CLI)

Una vez iniciado un Maester, puedes interactuar a través de su terminal con los siguientes comandos:
* `LIST REALMS`: Muestra los reinos conocidos y el estado de sus conexiones/alianzas.
* `PLEDGE <Reino> <archivo.sigil>`: Envía una misión asíncrona a través de un Envoy para proponer un pacto de alianza enviando el archivo de sello.
* `LIST PRODUCTS <Reino>`: Consulta de forma remota el catálogo de productos de un aliado (almacenando el resultado en la caché local).
* `START TRADE <Reino>`: Inicia el submenú interactivo para realizar transacciones comerciales basadas en la caché disponible.

## 🏗️ Detalles de Arquitectura e IPC

El sistema implementa un modelo **híbrido de concurrencia**:
1. **Multihilo (Red Passiva):** El servidor gestiona un hilo independiente (`pthread_create`) por cada cliente conectado. La memoria global compartida entre hilos (`SharedData`) está protegida mediante **Monitores implementados con Mutexes** (`pthread_mutex_t`).
2. **Multiproceso (Misiones Activas):** Los Envoys son procesos hijos aislados creados mediante `fork()`. La comunicación e intercambio de órdenes con el proceso padre se realiza exclusivamente mediante **Pipes anónimos** bidireccionales.
3. **Multiplexación y Timeouts:** Se utiliza `select()` en las lecturas de red para garantizar mecanismos de *timeout* y evitar hilos bloqueados de forma indefinida ante caídas de red.
4. **Verificación Externa:** El cálculo del checksum MD5 se delega al sistema operativo mediante el patrón **Fork-and-Exec** (`execlp`), abstrayendo de forma portable el comando según la plataforma (`md5sum` / `md5`).

Compilar:
    make

Ejecutar un Maester:
    ./maester <config.dat> <stock.db>

Por ejemplo, para arrancar los 5 maesters de prueba (cada uno en su
propio terminal):
    ./maester test_global/A/maesterA.dat test_global/A/stock.db
    ./maester test_global/B/maesterB.dat test_global/B/stock.db
    ./maester test_global/C/maesterC.dat test_global/C/stock.db
    ./maester test_global/D/maesterD.dat test_global/D/stock.db
    ./maester test_global/E/maesterE.dat test_global/E/stock.db

Los stock.db de cada reino ya estan generados; el Maester los actualiza
en cada compra y los conserva entre ejecuciones.

