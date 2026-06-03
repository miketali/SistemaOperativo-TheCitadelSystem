# SistemaOperativo-TheCitadelSystem
Sistema distribuido en C de comercio entre reinos. Usa arquitectura híbrida: un servidor multihilo (pthreads) con mutexes para atender clientes en red, y un pool de procesos independientes (fork) para ejecutar misiones asíncronas comunicados por pipes. Aplica un protocolo estricto de tramas (320B) y control de integridad mediante MD5.
