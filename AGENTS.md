# AGENTS.md

Instrucciones para agentes que trabajen en este repo.

- Antes de editar, leer este archivo y el `README.md` de la raiz. Si el cambio altera comportamiento, build, arquitectura o flujo de trabajo, actualizar la documentacion correspondiente.
- No leer todo el proyecto por defecto. Buscar con `rg`, abrir solo los archivos relevantes y seguir los limites de modulo existentes (`src/core`, `src/core/agent`, `src/core/profiles`, `qml/pages`, etc.).
- Todo bug arreglado debe incluir una prueba de regresion cuando sea viable. Toda feature nueva debe cubrir al menos el camino feliz y los bordes principales.
- Antes de terminar, correr `tests.bat Debug` cuando se toque C++/QML/core. Si no se puede correr, dejar el motivo concreto.
- Al compilar para entregar cambios, generar siempre ambas configuraciones con `build.bat Both NOPAUSE`. No usar el script sin `NOPAUSE`, porque el `pause` bloquea la automatización. Verificar que se actualizaron tanto `build/Debug/LlamaCode.exe` como `build/Release/LlamaCode.exe`.
- Mantener sincronizada la identidad visual por configuración: Debug debe usar `assets/debug_icon.ico` (llama roja) tanto en el `.exe`, acceso directo, ventana principal y splash; Release debe usar `assets/app_icon.ico`. La selección en C++ y recursos debe depender de `LC_DEBUG_ICON`, no de `QT_DEBUG`.
- Para perfiles locales Qwen/coding, preferir sampling conservador: `--temp 0.6 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0`. No subir creatividad sin justificarlo.
- No revertir cambios ajenos. Si el working tree ya esta sucio, trabajar alrededor de esos cambios y reportar que archivos se tocaron.
- Si hay repo git disponible, al terminar hacer commit y push de los cambios propios, salvo que el usuario indique lo contrario o haya un bloqueo real.
- Mantener los cambios acotados. Evitar refactors amplios si no son necesarios para la tarea.
