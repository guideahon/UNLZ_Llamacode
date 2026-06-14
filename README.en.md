<p align="center">
  <img src="https://github.com/JonatanBogadoUNLZ/PPS-Jonatan-Bogado/blob/9952aac097aca83a1aadfc26679fc7ec57369d82/LOGO%20AZUL%20HORIZONTAL%20-%20fondo%20transparente.png?raw=true" alt="National University of Lomas de Zamora — Faculty of Engineering" width="520">
</p>

<h1 align="center">UNLZ_Llamacode</h1>

<p align="center">
  <strong>National University of Lomas de Zamora (UNLZ) — Faculty of Engineering</strong><br>
  Institutional project · Supervised Professional Practice / Applied research
</p>

<p align="center">
  🇦🇷 <a href="README.md">Español</a> ·
  🇬🇧 <strong>English</strong> (this document)
</p>

---

> **Institutional project of the National University of Lomas de Zamora (UNLZ), Faculty of Engineering.**
>
> UNLZ_Llamacode is a local-AI workstation: a native desktop app (Qt/QML + C++)
> to **orchestrate `llama.cpp` models**, manage chat with persistent history, and
> run **coding agents** over local repositories — all running on your own hardware,
> with no dependency on cloud services.
>
> It is developed as a foundation for academic and research work, aimed at teaching,
> experimentation with local LLMs, and the work of the Faculty's fellows/thesis
> students.

## Table of contents

- [Ultra-fast install](#ultra-fast-install-isolated-test-bench)
- [What it is](#what-it-is) · [Current status](#current-status) · [Goal](#goal) · [Differentiators](#differentiators)
- [Architecture](#architecture)
- [Multi-llama.cpp design](#multi-llamacpp-design) · [Multi-GGUF roots](#multi-gguf-roots-design) · [Composable profiles](#composable-multi-profile-design)
- [Model cookbook (hardware-fit)](#model-cookbook-hardware-fit-recommendations)
- [Integrated chat](#integrated-chat) · [Agent harness](#agent-harness-opencode) · [Server launch](#server-launch-launchpage)
- [Process lifecycle](#process-lifecycle) · [Tech stack](#tech-stack) · [Build](#build) · [Repo layout](#repo-layout)
- [Phases](#phases) · [Benchmarking](#benchmarking) · [Auto-tuning](#parameter-auto-tuning) · [Operational security](#operational-security)
- [Acknowledgements](#acknowledgements)

## Ultra-fast install (isolated test bench)

A single command: installs every dependency, clones the repo into an isolated
folder, builds and launches. No manual clone or environment prep required.

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.ps1 | iex
```

**Linux** (bash):

```bash
curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh | bash
```

It automatically installs: git, Python/CMake/Ninja, the C++ toolchain (MSVC v143 on
Windows / g++ on Linux) and Qt 6 (Core, Quick, Sql, Concurrent, Network, Widgets +
QML runtime modules). Clones into `%USERPROFILE%\LlamaCode` / `~/LlamaCode`.

Optional variables (set before running):

| Var | Default | What it does |
|---|---|---|
| `LC_DIR` | `~/LlamaCode` | isolated install folder |
| `LC_BRANCH` | `main` | branch to clone |
| `LC_CONFIG` | `Release` | `Release` or `Debug` |
| `LC_NORUN` | (empty) | `1` = don't launch when done |

Minimum prerequisites: **Windows** needs `winget` (App Installer from the Microsoft
Store). **Linux** supports apt / dnf / pacman / zypper and asks for `sudo` for
system packages.

---

## What it is

UNLZ_Llamacode is a native app (Qt/QML + C++) to orchestrate multiple `llama.cpp` backends, manage chat sessions, and run AI agent harnesses (opencode, aider) over local repos.

Core principle:
- The GUI does **not** embed `llama.cpp`.
- The GUI **orchestrates external binaries** (`llama-server.exe`, MTP forks, CUDA/Vulkan/CPU builds).
- The GUI **composes reusable profiles** over binaries, models and presets.
- The GUI **integrates agent harnesses** (opencode) via native HTTP API.

## Current status

**P0–P4 complete and working.**

| Component | Status |
|---|---|
| `BinaryRegistry` + `CapabilityDetector` | ✅ |
| `ModelRootRegistry` + `GGUFScanner` | ✅ |
| `ModelCatalog` (SQLite) | ✅ |
| `ProfileManager` (6 entities) | ✅ |
| `EffectiveProfileBuilder` | ✅ |
| Profile importer from CLI args | ✅ |
| Start/Stop server (QProcess + Job Object, async stop) | ✅ |
| Integrated streaming chat (P4) | ✅ |
| Chat history with projects | ✅ |
| opencode harness via HTTP API (P3) | ✅ |
| Agent View (chat bubbles) + terminal view | ✅ |
| opencode session history with projects | ✅ |
| Process lifecycle (Job Object + PID file) | ✅ |
| Dedicated `LlamaProcessManager` | ⏳ P1 refactor |
| Automatic health-check endpoint | ✅ (polling /health post-start) |
| Port-collision pre-check on server start | ✅ |
| Native agent (LlamaAgentBackend, ReAct + tools + MCP) | ✅ P5 |

## Goal

A serious launcher for `llama-server`, evolved into a command center for coding agents with integrated chat and persistent history.

## Differentiators

- **Multi-llama.cpp**: coexist with several builds/forks frictionlessly.
- **Multi-GGUF roots**: index several model folders/disks.
- **Composable multi-profiles**: mix `Backend + Model + Runtime + Harness + Workspace`.
- **Persistent chat**: conversation history grouped by project/profile.
- **Integrated agent**: opencode via HTTP API with no per-message subprocess, with sessions and projects.

## Architecture

```text
LlamaCode
├── UI Layer (Qt Quick / QML)
│   ├── Main.qml (ApplicationWindow + NavBar)
│   ├── pages/
│   │   ├── BinariesPage.qml
│   │   ├── ModelRootsPage.qml
│   │   ├── ProfilesPage.qml      ← import from CLI args
│   │   ├── LaunchPage.qml
│   │   ├── ChatPage.qml          ← streaming chat + history + projects
│   │   └── AgentPage.qml         ← Agent View + terminal view + sessions
│   └── components/
│       ├── LcButton, LcTextField, LcDialog
│       ├── NavBar, PageHeader
│       └── CommandPreview
├── AppController (singleton → QML "App")
│   ├── Chat session management   ← local JSON, grouped by launchProfile
│   ├── Agent session management  ← opencode HTTP API + SSE
│   └── Process lifecycle         ← Job Object + PID state file + orphan kill
├── Backend Manager
│   ├── BinaryRegistry
│   ├── CapabilityDetector
│   ├── ProfileManager            ← 6x ProfileListModel<T> + JSON
│   └── EffectiveProfileBuilder
├── Model Manager
│   ├── ModelRootRegistry
│   ├── GGUFScanner
│   └── ModelCatalog (SQLite)
└── Storage (AppLocalDataLocation)
    ├── binary_registry.json
    ├── model_roots.json
    ├── model_catalog.db
    ├── profiles/{backends,models,runtimes,...}.json
    ├── services.json             ← PID state for orphan detection
    ├── chat/{index.json, *.json} ← persisted chat sessions
    └── benchmarks/               ← quality benchmark cache + run results
```

## Multi-llama.cpp design

### Binary Registry

`LlamaBinary` entity:
- `id`, `name`, `path`, `flavor` (`official`, `mtp-fork`, `custom`)
- `backend` (`cuda`, `vulkan`, `cpu`, `metal`)
- `versionHint` (free text)
- `supportedFlags`, `conflictingFlags`, `flagAliases`
- `envDefaults`, `workingDirectory`, `binaryHash` (SHA256 of first 1MB)
- `pathValid` (validated at runtime)

### Capabilities Matrix

Each binary tracks supported flags, aliases and conflicts. `EffectiveProfileBuilder.addFlag()` degrades with a `warning` or emits a `blockingError` depending on criticality.

## Multi-GGUF roots design

### Model Root Registry

`ModelRoot` entity: `id`, `path`, `label`, `scanMode` (manual/startup/watch), `enabled`, `priority`, `tags`, `isOnline`.

### Model catalog (SQLite)

`CatalogModel` entity: `id`, `rootId`, `absolutePath`, `fileName`, `sizeBytes`, `mtime`, `familyHint`, `quantHint`, `isVisionCandidate`, `isDraftCandidate`, `isAvailable`, `sha256`.

### GGUFScanner

- Async scan via `QtConcurrent::run`
- Infers family (deepseek, llama, mistral, phi, qwen, gemma...) by regex over the name
- Infers quant (`Q4_K_M`, `IQ3_XS`, `BF16`...) by regex
- `isDraftCandidate`: contains "draft"/"small" OR size < 2GB

## Model cookbook (hardware-fit recommendations)

`ModelRootsPage` recommends which models to download based on detected hardware (RAM / VRAM / GPU via `nvidia-smi`), using the `assets/hwfit/hf_models.json` catalog (~900 models, based on the Odysseus cookbook).

### Scoring

Each model gets a `0–100` score combining, weighted for the *general* use case (quality 0.45 / speed 0.30 / fit 0.15 / context 0.10):

- **Quality** — preferably a **real benchmark** (Artificial Analysis *Intelligence Index*, remapped to 0–100); if there's no match, a heuristic by params + family + architecture bonus (qwen3.6 +9, qwen3.5 +8, qwen3-next +6, …) with a penalty by quant tier. Coder models are penalized in the general scan so they don't dominate.
- **Speed** — estimated t/s based on GPU bandwidth and active params (MoE-aware). In `partial_offload` speed is a harmonic GPU/CPU blend by the fraction resident in VRAM.
- **Fit** — required-memory vs. budget ratio.
- **Context** — modern target: 32k=100, 16k=85, 8k=70, 4k=50 (the 4k stub isn't rewarded).

Tie-break by version (Qwen3.6 > Qwen3.5).

### Memory estimation (`estimateCatalogMemoryGb`)

Footprint = **weights + KV cache + overhead**, sized for a realistic context, not 4k:

- **Weights** — **total** params (MoE keeps all experts in memory, only routes a subset per token) × bytes-per-param of the quant.
- **KV cache** — scales with the **full** model (every layer caches K/V regardless of MoE routing) and the **real sizing context**, not a 4k stub. Constant `1.5e-5 GB/token/B` ≈ fp16 KV of a GQA-era model (Llama3-8B @32k ≈ 4 GB).
- **Overhead** — llama.cpp compute graph + MTP/draft buffers (`0.7 GB + 5%` of weights).
- **Sizing context** (`sizingContext`) — target 32k, capped by the model's max ctx, floor 8k. Avoids underestimating KV cost with small defaults.

### Run modes (run mode / fit)

Computed against VRAM (`nvidia-smi`) and system RAM (90% usable as headroom):

| Mode | Condition | Notes |
|---|---|---|
| `gpu` | fits in VRAM | all on GPU |
| `partial_offload` | doesn't fit in VRAM, fits in VRAM+RAM | VRAM+RAM spill (llama.cpp partial `-ngl`); `gpuFraction = vram/required` |
| `cpu_only` | no GPU, fits in RAM | all in RAM |
| `no_fit` | doesn't fit in VRAM+RAM | — |

### Quality benchmark (Artificial Analysis)

- **Bundled table** `assets/benchmarks/aa_intelligence.json` — offline floor, no network dependency.
- **Weekly refresh**: if the cache (`AppLocalData/LlamaCode/benchmarks/`) is >7 days old, it fetches in the background and overwrites it; on any network/JSON failure, the bundled one stays.
- **Matching**: `benchmarkKey()` normalizes the catalog name (strips provider, quant/format, GGUF, `-4bit`, `instruct`/`it`/`base`…) to map against the table.

## Composable multi-profile design

| Entity | What it defines |
|---|---|
| `BackendProfile` | host / port / binary / base args |
| `ModelProfile` | main model + mmproj + draft |
| `RuntimePreset` | ctx / batch / threads / gpu-layers / flash-attn / cache |
| `HarnessProfile` | adapter / args / env of external harness |
| `WorkspaceProfile` | cwd / policies / shell permissions |
| `LaunchProfile` | composition of the 5 above + overrides |

### Profile importer from CLI

Paste a terminal command (e.g. `llama-server --model ... --ctx-size 8192 --n-gpu-layers 99`) and UNLZ_Llamacode extracts and configures all recognized parameters automatically.

## Integrated chat

- **Streaming chat** directly to `llama-server` via `/v1/chat/completions` SSE
- **Persisted sessions** in local JSON (`AppLocalData/LlamaCode/chat/`)
- **Grouped by project** (launch profile active when the session was created)
- **"⏳ Processing..." indicator** while waiting, `▌` cursor during generation
- **Stop generation** while saving what was received

## Agent harness (opencode)

- **Native HTTP integration**: talks to the opencode server via REST + SSE, with no `opencode run` subprocess (eliminates the SQLite DB conflict on Windows)
- **Agent View**: chat bubbles with real-time streaming
- **Terminal view**: raw log for debugging
- **opencode sessions**: history persisted in the opencode DB, grouped by directory/project
- **Automatic resume**: resumes the last session when the agent restarts
- **Auto-generated titles**: real-time update via `session.updated` SSE

## Server launch (`LaunchPage`)

- **Command preview** with a *Copy* button.
- **Start server + agent** — brings up `llama-server` and the agent harness.
- **Start server only** — just `llama-server`, no agent.
- **OpenAI endpoint** — with the server running it shows `http://<host>:<port>/v1` (read-only, selectable) + a *Copy* button, to point external agents (opencode, aider, etc.) at the local backend.

## Process Lifecycle

- **Windows Job Object**: all subprocesses (llama-server + harness) are assigned to the main process's Job Object. When UNLZ_Llamacode closes (normally or on crash), children die automatically.
- **Traceability env vars**: `LLAMACODE_MANAGED=1`, `LLAMACODE_ROLE=server|harness-*`, `LLAMACODE_APP_PID=<pid>` on all spawned processes.
- **PID state file** (`services.json`): on startup, detects orphans from previous sessions and kills them before bringing up new processes.
- **Async stop**: `stopServer()` doesn't block the UI. It sends `terminate()`, exposes a `serverStopping` property, shows "Stopping..." on the button and status. Force kill after 5s if the process doesn't finish.

## Tech stack

- **Qt 6.8.3** (`msvc2022_64`)
- **Qt modules**: Core, Quick, Sql, Concurrent, Network, Widgets
- **Compiler**: MSVC 2022 (VS BuildTools)
- **CMake 3.21+**, generator: Visual Studio 17 2022 (multi-config)
- **QML theme**: Catppuccin Mocha
- **Persistence**: JSON (registries/profiles/chat) + SQLite (catalog) + QSettings

## Build

### Fast (recommended)

`build.bat` kills hung processes, configures, builds, deploys the Qt runtime (`windeployqt`) and regenerates the shortcuts. Accepts a config:

```bat
build.bat            REM Debug + Release (default)
build.bat Debug      REM Debug only
build.bat Release    REM Release only
```

Outputs:

| Config | Binary | Shortcut | Icon |
|--------|--------|----------|------|
| Release | `build\Release\LlamaCode.exe` (optimized, `NDEBUG`) | `LlamaCode.lnk` | `assets\app_icon.ico` (normal llama) |
| Debug | `build\Debug\LlamaCode.exe` (symbols + asserts) | `LlamaCode-Debug.lnk` | `assets\debug_icon.ico` (**red** llama) |

The Debug red icon is embedded in the `.exe` (taskbar/explorer) via `app_icon.rc` + `#ifdef LC_DEBUG_ICON` (CMake defines `/dLC_DEBUG_ICON` only in Debug config), and also in the `.lnk`.

> After touching code, always rebuild — the QML is embedded in the binary via `qt_add_qml_module`.

### Manual

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --parallel
```

### First install

`install.bat` / `setup.bat` install Python deps + Qt 6.8.3 via `aqtinstall` before the first build.

## Repo layout

```text
LlamaCode/
├── CMakeLists.txt          ← CMake root
├── app_icon.rc             ← icon resource (Debug/Release conditional)
├── build.bat / install.bat / setup.bat
├── update-shortcut.ps1     ← generates the .lnk (parametrizable by config/icon)
├── LlamaCode.lnk / LlamaCode-Debug.lnk
├── src/                    ← C++ (AppController, agent backends, core)
├── qml/                    ← UI (Main.qml, pages/, components/)
├── assets/
│   ├── app_icon.ico / debug_icon.ico / app_icon.png
│   ├── hwfit/hf_models.json          ← model catalog (cookbook)
│   └── benchmarks/aa_intelligence.json ← quality scores (offline)
├── docs/                   ← documentation (agent.md, TODO.md, plan_harness.md, tuner.md, ...)
├── logs/                   ← runtime/install logs (gitignored)
├── tests/ + build_tests/   ← Qt Test suite
└── build/                  ← artifacts (Debug/ + Release/, gitignored)
```

## Phases

1. **P0** ✅ Base multi-binary/multi-model launcher + UI
2. **P1** ✅ (partial) Validation, execution, live logs
3. **P2** ✅ (partial) Profile UX, CLI importer
4. **P3** ✅ opencode harness via HTTP API + sessions + projects
5. **P4** ✅ Integrated streaming chat + persistent history + projects
6. **P5** ✅ Built-in native coding agent (`LlamaAgentBackend`): ReAct loop against `llama-server`, tools (read/write/edit/grep/glob/list_dir/run_shell/web_fetch/task), MCP stdio, approvals, plan mode, checkpoint/rollback, parallel subagents in git worktrees, per-pattern permissions, @-mentions, images (vision)

## Benchmarking

A module to compare quants and profiles systematically: measures RAM, VRAM, speed and relative quality with results persisted in a table.

### Workflow

1. Select one or more `LaunchProfile`s to compare.
2. Pick a test mode: **Short** (~30 s) or **Full** (1–5 min).
3. Run: UNLZ_Llamacode launches each profile in sequence, runs the prompts, records metrics.
4. View results in a comparison table; export or save for future comparisons.

### Test modes

| Mode | Prompts | `n_predict` | Score | Estimated time |
|------|---------|-------------|-------|----------------|
| **Short** | 5 fixed | 256 | 0–2 per prompt (max 10) | ~30 s |
| **Full** | 15 configurable | 512 | 0–5 per prompt (max 75) | 1–5 min |

Fixed parameters across every run: `temp 0`, `top_p 1`, `top_k 0`, fixed seed, `ctx` per profile.

### Prompt categories (Full mode)

```text
3 × logical reasoning
3 × code / debug
3 × technical / expert writing
3 × structured data extraction
3 × long context (1,000–4,000 input tokens)
```

Prompts are editable and persisted; the user can replace them with real cases (llama.cpp logs, expert reports, SQL, Airflow, court files, etc.).

### Scoring

```text
Short mode:  0 = fail  /  1 = acceptable  /  2 = good
Full mode:
  5 = equal or better than baseline
  4 = slight loss, usable
  3 = correct but less precise
  2 = significant error
  1 = severe failure
  0 = didn't follow the instruction
```

Relative quality normalized against the baseline profile (the highest scoring one):

```
relative_quality = profile_score / baseline_score × 100
```

### Metrics recorded per run

```text
profile / model / quant
RAM used (MB)
VRAM used (MB)
tokens/s — prompt eval
tokens/s — generation
total time (s)
short score / full score
severe errors (count)
```

### Persistence and view

- Results in JSON (`AppLocalData/LlamaCode/benchmarks/{timestamp}.json`).
- Table view in `BenchmarkPage.qml`: sortable columns, filter by profile/quant/date.
- Export to CSV from the UI.

### Example table

| Quant | Score | Δ baseline | t/s gen | RAM | VRAM |
|-------|-------|------------|---------|-----|------|
| Q8_0 | 92/100 | base | 20 | 2 GB | 28 GB |
| Q6_K | 90/100 | −2.2% | 25 | 2 GB | 22 GB |
| Q5_K_M | 86/100 | −6.5% | 30 | 2 GB | 18 GB |
| Q4_K_M | 80/100 | −13.0% | 38 | 2 GB | 14 GB |
| IQ4_XS | 77/100 | −16.3% | 42 | 2 GB | 12 GB |
| Q3_K_M | 65/100 | −29.3% | 55 | 2 GB | 9 GB |

## Parameter auto-tuning

Automatic search for the `llama-server` flags (`ngl`, `batch`, `ubatch`, `flash-attn`, `cache-type-k/v`) that maximize **tok/s** without degrading **quality**. A TPE-lite optimizer (discrete Parzen) with a **quality gate**: unlike *llama-launcher v1.3*, tuning the KV cache quant by speed alone doesn't collapse to the lowest quant, because the loss penalizes dropping below the threshold heavily.

- Runs `N` trials on a scratch port (launches/measures/kills the server per candidate, in a separate `QThread` so the UI doesn't freeze).
- Measures `timings.predicted_per_second` throughput (`/completion`) and grades the output with EvalSuite-style substrings.
- When done it **clones** the profile into a new `-tuned` one with the best config in `extraArgs`; the original stays intact.
- UI: `ProfilesPage` → **Auto-tune** / **Cancel tune** + live status.

Full detail in [`docs/tuner.md`](docs/tuner.md).

## Operational security

- Nothing destructive without explicit approval.
- Writes outside the workspace: blocked by default.
- Shell commands with allowlist/denylist per `WorkspaceProfile`.
- Subprocesses tagged with env vars for auditing and lifecycle control.

## Acknowledgements

Code, data and design taken from other projects:

| Project | Use in UNLZ_Llamacode | Repo / Source |
|---|---|---|
| **llama.cpp** | Orchestrated binaries (`llama-server`), OpenAI-compatible API, GGUF format | https://github.com/ggml-org/llama.cpp |
| **opencode** | External agent harness (HTTP API + SSE); MCP `mcp{}` config format | https://github.com/sst/opencode |
| **aider** | Supported external agent harness | https://github.com/Aider-AI/aider |
| **markitdown** | Document extraction sidecar (pdf/office → markdown) in `DocumentExtractor` | https://github.com/microsoft/markitdown |
| **Odysseus cookbook** | Base of the hardware-fit catalog `assets/hwfit/hf_models.json` (~900 models) | https://github.com/TheBlokeAI/odysseus-cookbook |
| **Artificial Analysis** | Bundled quality scores `assets/benchmarks/aa_intelligence.json` (Intelligence Index) | https://artificialanalysis.ai |
| **Catppuccin (Mocha)** | QML theme palette | https://github.com/catppuccin/catppuccin |

> When adding code/data from another repo, add the corresponding row here.
