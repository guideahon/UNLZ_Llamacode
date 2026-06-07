# Benchmark Improvements

## Objetivo

Convertir los benchmarks personalizados en pruebas reproducibles con score automatico, no solo prompts y metricas de velocidad.

## Checklist

- [x] Mantener compatibilidad con benchmarks existentes que solo tienen `prompts`.
- [x] Permitir que cada prompt defina criterios de aceptacion ejecutables.
- [x] Ejecutar criterios de aceptacion en el workspace aislado del benchmark agente.
- [x] Guardar resultados de aceptacion en el historial.
- [x] Mostrar score automatico, fallos y detalle de comandos en la UI.
- [x] Permitir editar criterios desde la UI.
- [x] Agregar una suite inicial recomendada de benchmarks de programacion.
- [x] Compilar y verificar que el QML embebido cargue.

## Formato Propuesto

Cada prompt puede incluir:

```json
{
  "id": "mini_calc_lang",
  "prompt": "...",
  "isSpeed": true,
  "maxTokens": 8192,
  "acceptance": {
    "files": ["mini_calc_lang.py"],
    "commands": [
      {
        "name": "py_compile",
        "command": "python -m py_compile mini_calc_lang.py",
        "timeoutMs": 30000
      }
    ]
  }
}
```

## Scoring

- Cada archivo esperado existente suma 1 punto.
- Cada comando con exit code 0 suma 1 punto.
- Si falta un archivo esperado o falla un comando, la pasada queda marcada como fallo parcial.
- Si falla carga de servidor, request o agente, la pasada queda marcada como fallo duro y el benchmark continua.

## Suite Inicial

- Doom Fire con `--self-test`.
- Rain Terminal.
- Expense Tracker.
- Mini Calc Language.
- Tiny Notes API.
- Inventory + unittest.
- Log Analyzer.
- Minesweeper CLI.
- Todo Project multiarchivo + unittest.
