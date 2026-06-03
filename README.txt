The Citadel System - Fase 4
Maxi Lopez y Mikel Hidalgo

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

La memoria esta en memoria.pdf.
