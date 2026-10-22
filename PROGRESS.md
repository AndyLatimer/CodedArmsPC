# PROGRESS — Coded Arms PC port

Leyenda de procedencia: **[ORIG]** recurso original leído de la ISO del usuario ·
**[RE]** comportamiento/formato reconstruido por ingeniería inversa ·
**[NUEVO]** sistema implementado desde cero · **[PROV]** provisional, a sustituir.

## Completado

### Fase A — Auditoría
- [x] Entorno: CMake, GCC, MinGW-w64 (compilación cruzada a Windows), SDL2 2.30, Wine, Xvfb+Mesa.
- [x] Investigación: no existe documentación pública ni herramientas para los formatos de Coded Arms.
- [x] Inventario de la ISO ULES-00124; `BOOT.BIN` es un ELF sin cifrar (sin necesidad de descifrar).
- [x] Arquitectura elegida: motor propio C++17 + SDL2 + OpenGL 3.3 (justificación en README).

### Fase B — Prueba técnica
- [x] [RE] Lector de ARC "KCEO ARCDT 1.0" (`tools/arc_tool.py`, `Formats.cpp`).
- [x] [RE] Lector de BPX anidados.
- [x] [RE] Texturas PSPI 4/8/32 bpp con unswizzle → 2090/2103 imágenes.
- [x] [RE] Modelos PSPM (tiras de la GE, decodificador VTYPE genérico) → 1626/1690 modelos.
- [x] [NUEVO] Lector ISO 9660: el juego lee la ISO directamente, sin extraer nada.
- [x] [ORIG] Geometría y texturas originales del escenario DG_00 renderizadas en el motor.
- [x] [ORIG] Logotipo original CODED ARMS en el menú de pausa.
- [x] [NUEVO] Movimiento en primera persona WASD + ratón, salto, colisión contra triángulos.
- [x] [RE] Snapshots RLVB: los 27 niveles pregenerados se montan (9 escenarios) con piezas originales.
- [x] Compilación de Windows probada con Wine (mismo resultado determinista que Linux).

### Fase C — Base (en curso)
- [x] [NUEVO] Bucle de simulación a paso fijo (60 Hz) con interpolación de render; FPS libre o limitado.
- [x] [NUEVO] Entrada por acciones remapeable (config.ini): teclado, ratón, mandos XInput/DS4/DualSense vía SDL.
- [x] [NUEVO] Sensibilidad X/Y independiente, inversión de ejes, zonas muertas radiales, curva del stick, vibración.
- [x] [NUEVO] Ventana/pantalla completa sin bordes/exclusiva, VSync adaptativo, MSAA, FOV Hor+ (ultrapanorámico).
- [x] [NUEVO] Filtrado de texturas (cercano tipo PSP / bilineal / trilineal + anisotrópico).
- [x] [NUEVO] HUD escalado a resolución y limitado a 16:9 en ultrapanorámicos.
- [x] [NUEVO] Menú de pausa navegable con ratón, teclado y mando; guarda config.ini.
- [x] [NUEVO] Herramientas de depuración: overlay F3, cámara libre F2, capturas F12, `--autotest`.
- [x] Pruebas unitarias (`test_core`): config, bindings, zonas muertas, física, determinismo, raycast.
- [x] `ca_assetcheck`: valida todos los cargadores contra la ISO.

## Pendiente (siguiente)
- [ ] [RE] Elección exacta de pieza por celda (bytes 1–2 de celda, TIBKLAY): puertas, rampas, ascensores, objetos.
- [ ] [RE] Generador aleatorio original (`RLVDEFBIN` + código en BOOT.BIN) para niveles infinitos.
- [ ] [RE] Escala real (unidades/m) y parámetros de movimiento desde `BOOT.BIN` (Ghidra).
- [ ] [RE] Colisión original (`CLM`/`COLL`) en lugar de colisión por triángulos visuales.
- [ ] [RE] Modelos con skinning (`EH*`), animaciones `MOT1`, enemigos e IA (`WPT`).
- [ ] [RE] Audio KSS (SeData/TrSeData/WaveData) → reproductor; AT3 si aparece.
- [ ] [RE] Interfaz original (C2B/CAS2), fuentes y textos (F2B), variante PSPI `bpp=65540`.
- [ ] [NUEVO] Armas, daño, munición, HUD original, progresión de sectores.
- [ ] [NUEVO] Reproducción de vídeos PMF (requiere decodificador H.264 + ATRAC3plus).
- [x] Compilación Windows (.exe) + prueba con Wine + paquete de distribución (`scripts/build_windows.sh`).

## Bloqueado / riesgos
- Ninguno bloqueante. `EBOOT.BIN` cifrado **no es necesario** porque `BOOT.BIN` está en claro.
- Vídeos PMF: necesitan decodificar ATRAC3plus/H.264 (posible con FFmpeg; licencia a revisar).
- El contenedor de desarrollo es temporal: el trabajo debe subirse a un repositorio.

## Registro de decisiones
- 2026-10-21: C++/SDL2/OpenGL en vez de Godot/Unreal: los formatos son propietarios y de la
  GE de PSP; un cargador nativo evita conversiones y permite paso fijo y compilación cruzada.
- 2026-10-21: La ISO se lee directamente; no se redistribuye ningún recurso original
  (capturas con recursos del juego quedan fuera de git).
- 2026-10-21: Colisión genérica por triángulos (esferas + rayo de suelo) para poder caminar
  por la geometría original antes de descifrar `CLM`/`COLL`.
