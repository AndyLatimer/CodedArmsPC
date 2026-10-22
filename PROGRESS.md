# PROGRESS — Coded Arms PC port

Leyenda de procedencia: **[ORIG]** código o recurso original del juego (BOOT.BIN / ISO del usuario) ·
**[RE]** comportamiento reconstruido por ingeniería inversa · **[PPSSPP]** código portado de PPSSPP
(GPL-2.0+) · **[NUEVO]** sistema escrito para el port · **[PROV]** provisional, a sustituir.

## Arquitectura actual: recompilación estática (desde 2026-10-21)

El ejecutable `CodedArms` contiene **el código original del juego** (BOOT.BIN, MIPS Allegrex)
traducido a C por `tools/recomp/` y compilado de forma nativa, más un runtime HLE que sustituye al
sistema operativo y al hardware de la PSP, y un front end de PC. Por eso la jugabilidad, la IA, los
niveles, los menús, los efectos y las físicas son exactamente los originales: es el mismo código.
El código generado se crea en `build/` a partir de la ISO del usuario y nunca se sube a git.

## Completado (verificado)

### Recompilador y CPU
- [x] [NUEVO] Cargador de PRX con reubicaciones, descubrimiento de funciones (jal, punteros, tablas de saltos, epílogos compartidos), decodificador Allegrex completo (100 % de las instrucciones del juego), generador de C con ranuras de retardo, saltos probables y `jr` por tablas.
- [x] [PPSSPP] VFPU mediante el intérprete de PPSSPP.
- [x] Prueba diferencial contra el intérprete de PPSSPP: las 72 634 palabras de instrucción distintas del juego coinciden.
- [x] Trazado por función con comprobación de la ABI (`-DCA_RECOMP_TRACE`).

### Runtime HLE (src/psp)
- [x] [NUEVO] Kernel: hilos (un hilo del host por hilo del juego, planificación por prioridades), semáforos, event flags, callbacks, interrupciones de vblank, memoria.
- [x] [NUEVO/PPSSPP] E/S: disc0 desde la ISO, ms0 en una carpeta del host; **temporización asíncrona como PPSSPP** (corrigió el cuelgue del cargador de fuentes: el orden de las tareas de carga depende de ella).
- [x] [PPSSPP] sceSasCore (mezclador de voces VAG, ADSR, reverberación).
- [x] [PPSSPP] sceMpeg + MediaEngine + FFmpeg mínimo (H.264) + decodificador ATRAC3+: los 5 vídeos PMF se reproducen completos; el audio del vídeo de introducción es idéntico muestra a muestra al de PPSSPP.
- [x] [PPSSPP] sceUtility: diálogos de guardado/carga y de mensajes, cifrado de partidas compatible (libkirk/sceChnnlsv); 93 pruebas contra el código de PPSSPP. Tarjeta de memoria simulada de 1 GB (el juego calcula el espacio libre en 32 bits).

### Gráficos (GE → OpenGL 3.3)
- [x] [NUEVO, fórmulas de PPSSPP] Renderizador por hardware: estado completo de la GE, decodificador de vértices (todos los VTYPE, morph, skinning), texturas (todos los formatos, swizzle, CLUT, DXT), transformación, viewport y offset de la GE, 4 luces, texgen, niebla, funciones de textura, tests alfa/color, rango de profundidad, blending, stencil, framebuffers como FBO, render-to-texture, transferencias de bloque, framebuffers escritos por la CPU.
- [x] [PPSSPP] Parches bezier/spline teselados (el juego construye sus niveles con ellos).
- [x] **Resolución interna escalable** (múltiplo de 480×272, automática según la ventana).
- [x] Verificado jugando: logo de Konami, vídeo de introducción, pantalla de título, menús, introducción de la historia, entrada de nombre, terminal, selección de sector y **misión de entrenamiento con escenario 3D, arma, HUD y minimapa**, comparado con capturas de PPSSPP.

### Front end de PC (src/frontend)
- [x] [NUEVO] Ventana SDL2 redimensionable, pantalla completa (F11 / Alt+Intro), VSync, relación de aspecto conservada o estirada.
- [x] [NUEVO] Teclado, ratón y mandos (Xbox/PlayStation/XInput vía SDL) asignables en `config.ini`; zona muerta radial; valores por defecto según el esquema de control A del juego.
- [x] [NUEVO] Audio por SDL mezclando los canales sceAudio del juego.
- [x] Compilación de Windows (`CodedArms.exe`, MinGW) probada con Wine: arranque, logo y vídeo.

### Herramientas
- [x] `ca_psprun`: ejecución sin ventana o con GL oculto (`--gl --shot`), captura de audio (`--wav`), pulsaciones programadas (`--press`) y control interactivo por FIFO (`--control`: press/hold/stick/shot/gpudump/maxdraw).
- [x] Oráculo: PPSSPP headless (renderizador por software) para comparar capturas y audio.

## Pendiente (siguiente)
- [ ] [RE] **Apuntado con ratón real**: localizar las variables de cámara (yaw/pitch) del jugador en la memoria del juego y escribirlas directamente (ahora el ratón emula los botones de mirar).
- [ ] [RE] **Panorámico 16:9 / ultrapanorámico**: ampliar el FOV horizontal (Hor+) parcheando la proyección y ajustar el HUD.
- [ ] [NUEVO] Dibujar los diálogos del sistema (guardar/cargar/mensajes) en el front end (la interfaz `host_dialog` ya existe).
- [ ] [NUEVO] Menú de opciones del port (resolución, controles, sensibilidad X/Y, inversión, vibración).
- [ ] [NUEVO] Contexto de controles: menús con ratón/teclado (Intro/Esc) frente a juego.
- [ ] Verificación exhaustiva del renderizador contra PPSSPP (más escenas y efectos).
- [ ] Red ad hoc (multijugador): de momento, sin conexión.
- [ ] Paquete de distribución de Windows.

## Proyecto anterior (sustituido)
`CodedArmsLegacy` es la reimplementación inicial (cargadores propios de ARC/BPX/PSPI/PSPM/RLVB y
un motor FPS nuevo). Se conserva por sus herramientas y pruebas; ver `TECHNICAL_NOTES.md` §1–8.

## Registro de decisiones
- 2026-10-21: Recompilación estática en lugar de reimplementación: es la única forma de que el resultado sea "idénticamente el mismo juego".
- 2026-10-21: Se permite copiar código de PPSSPP; el proyecto pasa a GPL-2.0-or-later (libkirk es GPL-3.0+, por lo que los binarios que la enlazan son GPLv3).
- 2026-10-22: La E/S asíncrona sigue la temporización de PPSSPP (necesaria para el orden de carga del juego).
- 2026-10-22: Tarjeta de memoria simulada de 1 GB (con 16 GB el juego avisa de falta de espacio).
- 2026-10-22: Renderizador propio en lugar de enlazar la GPU de PPSSPP (está integrada en su núcleo monolítico); se copian sus decodificadores y fórmulas.
