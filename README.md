# Coded Arms — port nativo para PC

Reimplementación nativa (C++17 + SDL2 + OpenGL 3.3) de *Coded Arms* (Konami, PSP, 2005)
que **lee los recursos originales directamente de la ISO del propio usuario**. El
repositorio no contiene ningún recurso del juego.

Estado: prueba técnica funcionando (geometría/texturas originales + control FPS moderno).
Ver `PROGRESS.md` (tareas) y `TECHNICAL_NOTES.md` (formatos verificados).

## Arquitectura y por qué

| Opción | Valoración |
|---|---|
| Emulación/recompilación del ejecutable | `BOOT.BIN` es MIPS Allegrex en claro: sirve para *estudiar* la lógica (Ghidra), pero la recompilación estática a x86 exigiría reimplementar el SO de la PSP y la GE: coste enorme y frágil. |
| Unreal Engine | Desproporcionado; requiere convertir cada formato propietario a FBX/uasset y su editor para empaquetar. |
| Godot | Viable, pero igualmente obliga a convertir formatos o escribir importadores GDExtension; menos control del paso fijo y del render tipo GE. |
| **Motor propio C++/SDL2/GL** (elegido) | Cargadores nativos de ARC/BPX/PSPI/PSPM, render fiel a la GE (textura × color de vértice, alpha test), simulación a paso fijo, compilación cruzada a Windows sin editor. |

Comportamiento de juego: se **reconstruye** estudiando `BOOT.BIN` y los datos; nada se
presenta como "código original recuperado".

```
src/core      config INI, registro, matemáticas
src/platform  entrada por acciones (teclado, ratón, mandos SDL)
src/render    cargador GL 3.3, renderizador, HUD
src/assets    lectores ISO9660, ARC, BPX, PSPI, PSPM (formatos originales)
src/game      jugador, colisión, mundo original, nivel provisional
tools/        arc_tool.py, ca_assetcheck
tests/        pruebas unitarias sin GPU
```

## Compilar

Linux: `scripts/build_linux.sh` (requiere `libsdl2-dev`, CMake, GCC).
Windows (desde Linux): `scripts/fetch_deps.sh && scripts/build_windows.sh` → `dist/CodedArmsPort-win64.zip`.
Windows nativo con MSYS2/MinGW: `cmake -S . -B build -G "MinGW Makefiles"` con `third_party/SDL2-2.30.9` presente.

## Ejecutar

```
CodedArms --iso "Coded Arms (Europe).iso" [--tileset DG_03] [--scale 4]
CodedArms --autotest out/        # recorrido guionizado con capturas (CI)
ca_assetcheck game.iso           # valida todos los formatos contra la ISO
```
Instrucciones de usuario y controles: `docs/LEEME_WINDOWS.txt`.
