# TECHNICAL_NOTES — Coded Arms (PSP) → PC

Solo se documenta aquí lo **verificado** contra los datos reales. Cada entrada indica
cómo se comprobó. Lo que es hipótesis se marca como tal.

Imagen de referencia: `Coded Arms (Europe)` — ULES-00124, ISO de 193 855 488 bytes,
SHA-1 `fabc93b30962f9e4df25c9ecbc7f7098ebe31c4f`, MD5 `7ba77364feed5efbb381660541c95b93`.
PARAM.SFO: TITLE `CODED ARMS`, DISC_ID `ULES00124`, versión de disco 1.00, PSP_SYSTEM_VER 1.50.

## 1. Contenido de la ISO (ISO 9660 estándar)

```
UMD_DATA.BIN                       "ULES-00124|18AF588988526506|0001|G"
PSP_GAME/PARAM.SFO, ICON0.PNG, PIC1.PNG, ICON1.PMF
PSP_GAME/SYSDIR/BOOT.BIN           3 336 488 B  ELF32 MIPS (Allegrex) SIN CIFRAR
PSP_GAME/SYSDIR/EBOOT.BIN          3 336 832 B  "~PSP" cifrado (nombre de módulo "Coded_Arms")
PSP_GAME/SYSDIR/UPDATE/*           actualización de firmware (no relevante)
PSP_GAME/USRDIR/Arc/MAIN.ARC       40.5 MB  475 entradas
PSP_GAME/USRDIR/Arc/DG.ARC         20.9 MB   12 entradas (escenarios)
PSP_GAME/USRDIR/Arc/CASTEST.ARC     4.7 MB    1 entrada  (CASVIEW.BPX)
PSP_GAME/USRDIR/SOUND/SeData.bin   "KSS SE Link Data  04/16/2005"
PSP_GAME/USRDIR/SOUND/TrSeData.bin "KSS BGM Link Data  03/24/2005"
PSP_GAME/USRDIR/SOUND/WaveData.bin "KSS Wave Link Data  04/09/2005" (24 MB)
PSP_GAME/USRDIR/movie/*.pmf        vídeo PSMF ("PSMF0014"): opening, entry, ending, sector00/01
PSP_GAME/USRDIR/module/*.prx       módulos de sistema (red, audio, mpeg)
```

**Consecuencia clave:** `BOOT.BIN` es el ejecutable en claro (cabecera `7F 45 4C 46`,
`file`: "ELF 32-bit LSB, MIPS-II"). Se puede analizar estáticamente (Ghidra + plugin
Allegrex/PSP) **sin descifrar nada** y por tanto sin eludir protecciones.

## 2. Contenedor ARC — "KCEO ARCDT 1.0"  ✅ verificado

Comprobado en los 3 archivos (488 entradas): offsets crecientes, todo dentro del archivo,
el final de la última entrada redondeado a 0x800 coincide con el tamaño del archivo.

| Offset | Tipo | Significado |
|---|---|---|
| 0x00 | char[16] | `KCEO ARCDT 1.0\0\0` |
| 0x10 | u32 | número de entradas N |
| 0x14 | u32 | tamaño de sector (2048) |
| 0x18 | u32 | tamaño de tabla = N·36 |
| ... | | relleno con `-` hasta 0x800 |
| 0x800 | entrada[N] | 36 bytes: char[24] nombre, u32 sector inicial, u32 tamaño, u32 = 0 siempre |

Tras la tabla hay un array de u16 (¿índice ordenado/hash?) — no necesario para extraer.
Herramientas: `tools/arc_tool.py`, `src/assets/Formats.cpp: parseArc`.

## 3. Contenedor BPX  ✅ verificado

Anidable (1506 BPX anidados en total). Cabecera:

| Offset | Tipo | Significado |
|---|---|---|
| 0x00 | char[4] | `BPX\0` |
| 0x04 | u32 | tamaño total |
| 0x08 | char[16] | nombre interno (p.ej. `gamecommon_eur`, `tp`) |
| 0x18 | u32 | número de entradas N |
| 0x1C | u32 | offset de una tabla de N u32 (offsets de datos, redundante) |
| 0x20 | u32 | offset de la tabla de nombres |
| 0x24 | u32 | desconocido (0 o un offset) |
| 0x28 | u32 | inicio de datos |

Tabla de nombres: entradas de 32 bytes = char[24] nombre, u32 offset, u32 tamaño.
Comprobado en TPP_TITLE.BPX: `TEX.PSPIP` @0x100 tamaño 0x280F0 y `FILETBL.BPX` @0x28200
tamaño 0x7F, que terminan exactamente en el tamaño total.

Tipos encontrados dentro (por extensión → magia): PSPM (`PSM\x02`, modelo), PSPIP
(`PSPI01`, texturas), MOT (`MOT1`, animación), BIN (`COLL`, `PSM\x02`, `PSPI01`, `BDB0`…),
CLM (`CLM\0`), WPT, PLT, RLT (`light_room…`), C2B (`CAS2`, interfaz), F2B (`f2b`, texto),
RLVDEFBIN (`BIN ROOT…`), RLVSNAP.

## 4. Textura PSPI ("PSPI01")  ✅ verificado visualmente

```
0x00 char[8]  "PSPI01\0\0"
0x08 u16 nImages, u16 nPalettes
0x0C u32 inicio de datos
0x10 registros de 24 bytes: primero nImages imágenes, luego nPalettes paletas
  imagen : u16 id, u16 paletteId, u16 w, u16 h, u32 bpp, u16 bufW, u16 bufH, u32 dataOff, u32 ?
  paleta : u16 id, u16 nColores, u32 dataOff, 16 bytes a 0
```
- Paletas RGBA8888 (orden de bytes R,G,B,A).
- Píxeles con el **swizzle de la PSP** (bloques de 16 bytes × 8 filas). Sin deshacer el
  swizzle la imagen sale corrupta; deshecho, aparece correcta (logo CODED ARMS,
  fondo del título). Los índices de 4 bpp usan primero el nibble bajo.
- Estadística global: 1803 imágenes de 4 bpp, 192 de 8 bpp, 2 de 32 bpp,
  13 con `bpp = 65540` (variante **no descifrada**: CELLEFFECT, BOX, LEVELMAP, WALLBOX).
- Validado: 2090/2103 imágenes decodifican (`ca_assetcheck`).

## 5. Modelo PSPM ("PSM\x02")  ✅ geometría y texturas verificadas visualmente

```
0x00 char[4] "PSM\x02", u32 tamaño
0x08 float[10]  caja: min xyz, max xyz, centro xyz, radio (centro = (min+max)/2 comprobado)
0x30 u16[4]     desconocidos (p.ej. 5,3,1,2 / 0,2,1,2)
0x38 u32        offset de la primera malla (0x60)
Malla (repetida; se avanza por su tamaño):
  +0x00 float[10] caja de la malla
  +0x28 u32 tamaño total de la malla
  +0x2C u32 tamaño de la cabecera de malla (0x70, 0xC0…)
  +0x32 u16 número de tiras
  +0x38 u16 índice de imagen en el PSPI del conjunto  (verificado: texturas correctas)
  Tira: cabecera de 0x50 bytes y luego los vértices
    u32 prim (3 = triángulos, 4 = tira, 5 = abanico — códigos de la GE)
    u32 vtype (registro VTYPE de la GE; visto 0x1FE = UV u16, color 8888, normal float, pos float)
    u32 nVértices, u32 ?, u32 ?, u32 bytes (= n·stride), u32 ?, u32 relleno tras los vértices (0x10)
```
- Las tiras se unen con vértices degenerados repetidos (se descartan al triangular).
- UV u16 normalizadas a /32768. Ejes: **Y arriba** (las marcas de suelo tienen Y constante).
- Validado: 1626/1690 modelos parsean. Fallan 64: modelos de enemigos `EH*` y `SHADOW_HUMAN`
  (probablemente vértices con pesos/skinning o cabecera distinta) → **pendiente**.

## 6. Escenarios (DG.ARC)  ✅ estructura / ⏳ ensamblado

`DG_00..DG_08.BPX` + `BOSS_0x.BPX`. Cada uno es un **kit modular** de piezas de sala:
```
CLM.BPX       *.CLM   (163 en DG_00; nombres como AO_A_J_00_000T / Y3 / Y4)
MDL.BPX       MODEL.BPX (98 modelos de pieza)  +  TP.BPX/TEX.PSPIP (109 imágenes)
LIFT_MARK.BPX marcas N/E/W/S
WAYPOINT.BPX  *.WPT (20 bytes por pieza)   PLT.BPX  *.PLT (20 bytes por pieza)
AREALIGHT.BPX LIGHT_ROOMxxxx.RLT
MOTION_AO.BPX piezas animadas (MDL + MOT + BDB + COLL)
AOD_POS.BPX   posiciones (12 bytes = 3 floats)
```
- Renderizar todas las piezas de DG_00 en sus coordenadas guardadas produce piezas
  **superpuestas**: son variantes para las mismas posiciones. El montaje lo decide el
  generador aleatorio a partir de `MAIN.ARC/DGxx_*.RLVDEFBIN` (bloques `ROOT`, `PASS`,
  `ROOM`…). **Pendiente de descifrar.**
- Escala: provisionalmente 4 unidades = 1 m (bounds DG_00 ≈ 37×40×40 m). **No verificada.**

Coincide con Wikipedia: "All of the levels in the game are randomly generated when they
are accessed".

## 7. Datos de diseño verificados (fuente: Wikipedia, artículo "Coded Arms")

- Ambientación: simulador militar de realidad virtual abandonado "AIDA".
- Niveles generados aleatoriamente al acceder.
- Sector 00 (Training, 3 niveles); Sector 01: City, Base, Ruins (6 niveles cada uno);
  Sector 02: 3 campos de 13 niveles con jefe en el 13; Sector 03 "Infinity" ilimitado.
- Armas/armaduras con atributos (físico… fuego, eléctrico, viral…); mejoras con "Opt_Key".
- Objetos con extensiones `.arm` (armas), `.dfn` (armaduras), `.med` (salud).
- Multijugador wifi 4 jugadores: Deathmatch, Keep the Mark, Last Man Standing.

## 8. Por verificar (no usar como hecho)

- Frecuencia de simulación original (60 Hz es supuesto del port).
- Velocidad de movimiento, salto, gravedad, FOV originales (valores del port provisionales).
- Uso del alfa del color de vértice (algunos vértices tienen A = 0).
- Formatos KSS (sonido), MOT1, COLL, CLM, WPT, PLT, RLT, C2B, F2B, RLVDEFBIN, RLVSNAP.
