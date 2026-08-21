# Contribuir a IR0

Guía corta para producir un cambio aceptable. El camino oficial de primer boot
está en el [README](README.md) y en [SETUP.md](SETUP.md).

## 1. Arranque del entorno

```bash
make check-env                    # alias de make deptest
make check-env PROFILE=desktop-x86_64
make defconfig && make ir0 && make run
make sync-mandocs && make man TOPIC=boot
```

`check-env` / `deptest` diagnostican el host (required / optional /
unsupported_version / present_but_unusable) con hints de paquete por distro.
No tratan `command -v` como prueba suficiente.

Perfiles y techos honestos: `make help-profiles`.

## 2. Estilo

- C del kernel: Allman braces; cabecera IR0 + SPDX (ver `.cursor/rules` /
  `Documentation/ai_driven_dev/`).
- APIs públicas vía `#include <ir0/…>` — no acoplar `fs/`/`mm/` a `drivers/`.
- Diff mínimo: resolver el requerimiento; sin refactors de paso.
- Sin stubs que finjan éxito (`return 0` en APIs reales). Preferir `-ENOSYS` /
  error honesto.
- Comentarios solo para lógica no obvia o techos intencionales.

## 3. Tests requeridos

Antes de pedir review:

```bash
make pre-submit                   # batería mínima → PRE_SUBMIT_OK
make pre-submit SUBSYSTEM=mm      # + smoke-mm-cow-lazy
make pre-submit SUBSYSTEM=net     # + smoke-stream-sock
make pre-submit SUBSYSTEM=arm64   # + smoke-arm64
```

`pre-submit` **no** publica nada. Corre build + `arch-guard` + `tests/host` +
chequeo de formato del diff, y smokes del subsistema si aplica.

Gates CTR habituales (oleadas mayores):

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
```

## 4. Formato del commit

Español, oraciones completas, foco en el *por qué*. Siempre `-s` (DCO):

```text
feat(ámbito): resumen breve

Qué problema resuelve y enfoque.

Cambios funcionales:
- …

Tests:
- make pre-submit SUBSYSTEM=… → PRE_SUBMIT_OK

Signed-off-by: Iván Ezequiel Rodriguez <ivanrwcm25@gmail.com>
```

Tipos: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`, `perf`.
Sin ningún `Co-authored-by:` (ni del agente, ni del mantenedor como
co-autor). Author y Committer: Iván Ezequiel Rodriguez
`<ivanrwcm25@gmail.com>`. Los agentes de la nube deben usar
`python3 scripts/ir0_git_commit.py` (o `git commit-tree`) y no publicar
si un hook inyectó trailers.

## 5. Parche

1. Rama desde `dev` (o la rama que indique el mantenedor).
2. Un tema por PR; Makefile raíz fino — recetas nuevas en `scripts/make/*.mk`
   con `-include` explícito.
3. No añadir descargas de red en el build por defecto.
4. Documentación técnica en inglés bajo `Documentation/`; español en
   `Documentation/esp/` si se pide. Mandocs: `Documentation/mandocs/{en,esp}/`
   luego `make sync-mandocs`.

## 6. Reporte de resultados

Incluí en el PR o mensaje:

```text
PRE_SUBMIT_OK
Subsystem: mm
Tests: … passed
Smoke gates: … passed
Patch formatting: OK
```

Comandos exactos corridos (no “compila en el IDE”). Si un smoke se salta
(p. ej. sin `raspi4b` en QEMU), decilo.

## Principios de entrada (qué copiamos de kernels de producción)

- Superficie de entrada estable (`check-env` → `defconfig` → `ir0` → `run`).
- Ayuda descubrible: `make help`, `help-profiles`, `help-docs`, `help-pre-submit`.
- Perfiles explícitos y configuraciones reproducibles (`setup/configs/*.defconfig`).
- Docs versionadas junto al código; diagnósticos accionables.
- Targets pequeños y composables; sin magia de red en el build.
- Diferencia clara entre camino **Supported** y experimento (lab UART, stub).

Preguntas de alcance o ABI: abrir issue / hablar con el mantenedor antes de
agrandar superficie.
