# Servidores con `online-mode=true` (cuentas premium)

**Estado: no soportado, y no se puede terminar sin un dato que solo tiene quien lo publique.**

MC-WEB entra en servidores 1.8 y abre los suyos en **modo offline** (`online-mode=false`): el nombre
de jugador es el que escribes y el UUID se calcula a partir de él, como en cualquier servidor sin
autenticación. Los servidores que exigen cuenta de Minecraft (`online-mode=true`) piden además:

1. **Cifrado de la conexión** (RSA para intercambiar la clave y AES-CFB8 sobre el flujo del protocolo
   de 1.8). Es protocolo público y se puede implementar sin problemas.
2. **Una sesión de Mojang/Microsoft**: el cliente tiene que demostrarle al servicio de sesiones
   (`sessionserver.mojang.com`) que es el dueño de la cuenta, y para obtener ese token hay que
   iniciar sesión con Microsoft (OAuth 2.0 con Xbox Live y los servicios de Minecraft).

El paso 2 necesita un **client ID de una aplicación de Azure que Microsoft/Mojang hayan aprobado**
para usar la API de Minecraft. Sin él, el inicio de sesión falla y no hay manera honesta de saltarlo
(ni se debe: las credenciales y los tokens de otras personas no son nuestros). Por eso este
documento existe en lugar de la función.

## Qué haría falta para añadirlo

- Registrar una aplicación en Azure, pedir a Mojang/Microsoft que la aprueben para la API de
  Minecraft y poner su client ID en la configuración del juego. **Eso solo lo puede hacer quien lo
  publica.**
- En el cliente: el flujo de inicio de sesión con Microsoft (código de dispositivo, para que valga en
  el terminal, en el escritorio y en el móvil), guardar el token y renovarlo.
- En `net/client.cpp`: el paquete *Encryption Request/Response*, AES-CFB8 sobre el transporte
  (`net::Transport`) y la llamada de `join` a `sessionserver.mojang.com`.
- En el servidor propio, si se quisiera: la comprobación de `hasJoined` contra el mismo servicio.

## Mientras tanto

- Juega en LAN o en servidores en modo offline (`online-mode=false`), o con el servidor dedicado propio
  (`mcweb-server`), que es offline.
- Para unirse a un servidor premium con proxy de autenticación (por ejemplo, uno con *BungeeCord*
  en modo offline por dentro) basta la conexión directa: el cifrado lo hace el proxy de delante.
