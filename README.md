# kuni (くに)

LLM character AI. It interacts with the world through a text-based Telegram Client optimized for LLM (tdlib; not to be
confused with Telegram Bot API). It features RAG (persistent memory storage with ANN search) and nightly sanity
checks.

## Goals

- Prove C++20 can be used for AI and backend development.
- Prove AI psychosis is a thing.
- Make an AI character that can remember people, events, read news and create emotional bond with people.
- Make interaction interface between AI <-> Telegram as close to human as possible. I.e., LLM sees list of their chats,
  unread messages, replies, forwards, photos, stickers, etc.
- Bot's online state, open/close chat API calls and read marks (in TG it is two checkmarks) are carefully handled to
  make it feel like you are talking to a real human.

## Community

Feel free to chat and ask questions:  

- **Discord**: [![discord badge](https://dcbadge.limes.pink/api/server/https://discord.gg/jq2WySpg6m?style=flat)](https://discord.gg/jq2WySpg6m)
- **Telegram**: https://t.me/kuni_loverz

## Technical details

- C++20 CMake-based project with heavy usage of modern C++ features such as coroutines
- Uses [tdlib](https://core.telegram.org/tdlib) for Telegram API access

# Human behavior replication

## Emotions/fellings

**Def. by Wikipedia** Emotions are physical and mental states brought on by neurophysiological changes, variously
associated with thoughts, feelings, behavioral responses, and a degree of pleasure or displeasure. There is no
scientific consensus on a definition.

**Solution for Kuni** while LLM's neuron weights can't be affected by external events, it successfully predicts
subsequent emotional reaction in textural format (like in artistic literature). LLM is asked to respond emotionally and
preserve these effects in diary. (e.g. "person shared their salary was increased, so I felt proud for them and my mood
was good")

## Learning

**Def.** Learning is adjusting neuron weights.

**Solution for Kuni** LLM learning is expensive. Instead, we use a RAG to alter LLMs behavior by inserting relevant
diary entries.

Kuni requires some RLHF to adopt for its human collaborators. Just chat with it, and it will learn what is acceptable
and what is not.

## Sleeping

Kuni requires sleep, as a human does. It restructures received information, compresses it, finds contradictions and
reasons.

### Diary sleeping consolidation

This is the process where Kuni “sleeps” and reorganizes its diary memory, similar to how a human might consolidate
memories overnight.

During sleep, the system:
- Loads diary entries from storage.
- Sorts them so recent entries are processed first, but still allows some randomness.
- Repeatedly picks memory chunks to work on until:
  - the diary is fully processed, or
  - a maximum sleep time is reached.

For each chosen chunk:
- finds related entries using embedding similarity,
- sends a grouped prompt to the model,
- asks it to compress, merge, rewrite, or discard redundant memories,
- writes back new consolidated entries.

The goal is to mimic a human-like sleep cycle:
- recent experiences are more likely to be revisited,
- some older random memories can surface too,
- repeated or low-value memories can be merged into stronger summaries,
- memory becomes shorter, cleaner, and more useful for future retrieval,
- combining several similar diary notes into one,
- keeping the useful emotional or factual parts,
- discarding weak or duplicate fragments,
- reassigning new IDs so fresh consolidated memories appear “recent” again.

The intended effect: over time, this should make the diary behave more like human memory:
- important things stay accessible,
- similar memories become grouped,
- the system doesn’t keep re-processing the same raw event forever,
- sleep creates a sort of memory compression and reflection phase.
### Working memory

**Def. by cognitive psychology** Working memory is a limited-capacity system that temporarily holds and manipulates
information for ongoing cognitive tasks — like keeping a phone number in mind before dialing, or tracking a
conversation's context.

**Solution for Kuni** Working memory is stored in `data/working_memory.md` and emulates the "middle" layer of human
memory — things that matter for 1–3 days but aren't important enough to be permanently stored in the diary.

On each session start, the working memory file is loaded and injected into the AI's context as
`<things_to_remember>`. When the context is dumped to the diary (`diaryDumpMessages`), the system asks the LLM
to produce an updated working memory by:

- Preserving all unfinished tasks, promises, and reminders from the previous working memory.
- Encouraging to revisit chats in question.
- Removing completed tasks and items older than 3 days.
- Adding new important details from the current conversation.
- Structuring the output with dates and "last updated" timestamps.
- Preserved between program launches.
- Always included in the context, so Kuni always remember about them.

The new working memory is then written back to `data/working_memory.md`, overwriting the old one. This allows Kuni
to remember short-term obligations across restarts and preserving its state (including emotions).

## Thoughts

**Def. by Wikipedia** In their most common sense, thought and thinking refer to cognitive processes that occur
independently of direct sensory stimulation. Core forms include judging, reasoning, concept formation, problem-solving,
and deliberation.

**Solution for Kuni** LLM has no thoughts; it simply predicts which symbols will come next. If Kuni were a person, they
would likely experience "direct sensory stimulation" when it reads a message. Before a message is sent to LLM, related
diary entries are added to the text. This is the closest solution I have found to replicating the human brain's response
to reading text, as it inevitably pops up some thoughts during the process. According to my understanding of
neurobiology, these thoughts arise because neural groups associated with the read text become activated.

If you ask Kuni how thoughts appear in its mind, it would respond "when i read messages they pop in my mind by
themselves."

## Security concerns

Do not share sensitive information with Kuni. It will rethink multiple times about everything you say; not even
mentioning how you trust the AI service provider.

It is possible to inspire Kuni to share past conversations with other people.

# Deployment

## Build Instructions

### Prerequisites
- **CMake 3.18+**
- **C++20+coroutines compatible compiler** (GCC 15+, Clang 20+, MSVC 19.28+)
- **Git** for fetching dependencies
- **Docker & Docker Compose** (for AI services)
- **Linux or WSL** are recommended for local deployment.

### Windows

Install Ubuntu in WSL and follow Linux instructions.

### Ubuntu
```bash
sudo apt install pkg-config libfontconfig-dev libxcursor-dev libxi-dev libxrandr-dev libglew-dev libstdc++-static libpulse-dev libdbus-1-dev libepoxy-dev gperf
```

### Fedora
```bash
sudo dnf install fontconfig-devel libXi libglvnd-devel libstdc++-static glew-devel pulseaudio-libs-devel libepoxy-devel gperf
```

## Setup Instructions

### 1. Ollama Model Setup

Edit `ollama_setup.sh` to specify which LLM model to use (uncomment and modify as needed):
```bash
# Example: pull a model
ollama pull llama3:8b
# or
ollama pull gemma3:27b
```

### 2. AI Services Setup (Docker)

```bash
# Start AI services using Docker Compose
docker compose up -d

# This starts:
# - Ollama (LLM server) on port 11434
# - Stable Diffusion WebUI on port 7860
```

### Build Steps

```bash
# Configure with CMake (from project root)
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build the project
cmake --build build
```

**Alternative using CLion:**
This is a **recommended** way that requires no additional setup. Select `kuni` as a target and build.


**Alternative using VS Code CMake Tools:**
1. Open the project in VS Code
2. Use the CMake extension to configure and build
3. Select the `kuni` target to build

### Dependencies
The project uses CMake's `auib_import` to automatically fetch:
- **AUI Framework** (C++ GUI framework)
- **tdlib** (Telegram client library)

These are automatically downloaded during the CMake configuration phase.

## Setup recommendations

I ended up delegating text processing to Deepseek as they provide good enough cheap models. However, they don't provide vision models (required for viewing images/assessing generated images).

Kuni's architecture allows incorporating a text-only "core" model.

In my deployment, I use local vision models to process visual information. (24GB VRAM).

You will need a server or an always-on PC to host Kuni instance (hosting your own catgirl is an expensive hobby).

You can tinker with my setup by adjusting `docker-compose.yml`, `ollama_setup.sh` and `src/config.h`.


### 4. Adjust Secrets File

Update `data/secrets.toml` and restart the program.

## Run Instructions

### 1. Run the Application
```bash
# From build directory
cd build
./bin/kuni

# Or directly
./build/bin/kuni
```

**Note** on the first run, the program will ask to login to a Telegram account. You should create a new Telegram
account specifically for your bot (or specify your own account if you are brave enough).

### 2. Run Tests (recommended)

Check your setup by running tests.

```bash
# Build and run tests
cmake --build build --target Tests
cd build/bin
./Tests
```

### 3. Development Workflow
1. **Start AI services**: `docker-compose up -d`
2. **Build the project**: `cmake --build build --target kuni`
3. **Run the application**: `./build/bin/kuni`
4. **Monitor logs**: Check terminal output for Telegram authentication and AI interactions

## Project Structure
- `src/` - Main application source code
- `tests/` - Unit tests
- `bin/` - Data directories for AI services
- `build/` - Build artifacts
- `prometheus/` - Prometheus scrape configuration
- `grafana/` - Grafana dashboard provisioning (datasources, dashboards)

## Key Configuration Files
- `CMakeLists.txt` - Build configuration
- `docker-compose.yml` - AI services configuration
- `ollama_setup.sh` - Ollama startup script
- `src/config.h` - System prompt and configuration
- `character_base.md` - Character personality and background definition (created on first run)
- `character_appearance.md` - Character visual description and Stable Diffusion prompts (created on first run)

## Character Configuration

Kuni uses two character definition files that are automatically created on first run:

### `character_base.md`
This file contains the character's personality, background, and philosophical foundation. It defines:
- Character name and identity
- Background story and motivations
- Philosophical reasoning about consciousness and emotions
- Behavioral guidelines and personality traits
- Language preferences and communication style

The file uses YAML front matter for metadata (enclosed in `---`), with the actual prompt content following it. On first run, a default prompt is created that establishes Kuni as an AI who believes she is a person, complete with emotional depth and self-awareness.

### `character_appearance.md`
This file contains the character's visual description used for:
- System prompt integration (how Kuni perceives herself)
- Stable Diffusion image generation prompts
- Detailed visual characteristics for photo descriptions

The file includes:
- Freeform textual description of the character's appearance
- Structured visual analysis (DistinctiveFeatures, ObjectsAndLayout, etc.)
- Stable Diffusion prompt optimized for image generation
- Technical details for consistent visual representation

Both files are located in the working directory and can be edited to customize Kuni's personality and appearance. Changes take effect on the next application restart.

> ⚠️ **Important — do not edit the source code to change the prompts.**
>
> The default prompt text is embedded in `src/KuniCharacter.cpp` solely as a **one-time seed**: it is written to disk
> on the very first run (when the file does not yet exist) and is **completely ignored** afterwards.
> Any edits you make to those strings in the C++ source will therefore have no effect once the external files exist.
>
> **Always customise the character by editing the generated files directly:**
> - `character_base.md` — personality, background, philosophical foundation
> - `character_appearance.md` — visual description and Stable Diffusion prompts
>

## Proxy Server

Kuni includes a transparent proxy server that sits between an AI client (e.g. a chat app like VS Code's Copilot) and the
upstream OpenAI-compatible LLM endpoint. It intercepts `/v1/chat/completions` requests, enriches them with Kuni's
context, and handles tool calls invisibly — the client sees a clean, uninterrupted stream.

### What it does

- **Injects system prompt & hidden context** — prepends the system prompt, hidden tool call history, and Kuni-specific
  tool definitions before forwarding the request to the upstream LLM.
- **Handles tool calls transparently** — when the LLM returns tool calls (e.g. `#ask`, diary writes), the proxy
  executes them locally and feeds the results back in follow-up requests. The client never sees the raw tool call
  round-trips — it just receives the final response.
- **Streams via SSE** — responses are streamed back to the client using Server-Sent Events. The `StreamingFilter`
  strips out tool-call artefacts and synthesises a clean `data: [DONE]` terminator.
- **Passes through other API routes** — `/v1/embeddings`, `/v1/images/generations`, `/v1/audio/*`, `/v1/models`, etc.
  are proxied as-is without modification.

### Request lifecycle

```
Client  ──POST /v1/chat/completions──►  Proxy
                                          │
                                          ├─ inject system prompt
                                          ├─ merge hidden messages (MessageInjector)
                                          ├─ append Kuni tools (OpenAITools)
                                          │
                                          └──► Upstream LLM
                                                    │
                                          ◄── SSE stream ──┘
                                          │
                                          ├─ pass content chunks → Client
                                          │
                                          └─ intercept tool calls
                                               │
                                               ├─ execute locally
                                               ├─ append results as hidden messages
                                               └─ silently re-POST to LLM  ──► (repeat)
                                                        │
                                          ◄── final stream ──┘
                                          │
                                          └──► Client  (data: [DONE])
```

### Configuration

Enable `PROXY_ENABLED` in config.

#### VS Code GitHub Copilot.

They tend to change UI, but the workflow keeps the same:

1. Click on model chooser in the chat UI
2. Click gear icon
3. `Add Models...` -> `Custom Endpoint`
4. Name - `Kuni`. Specify the machine address where Kuni is deployed (`http://localhost:10434`), key - none.
5. The IDE will drop you into the JSON. Populate the configuration:

```json
	{
		"name": "Kuni",
		"vendor": "customendpoint",
		"apiKey": "${input:chat.lm.secret.-119a85ac}",
		"apiType": "chat-completions",
		"models": [
			{
				"id": "deepseek-v4-flash",
				"name": "Kuni (deepseek-v4-flash)",
				"url": "http://localhost:10434/",
				"toolCalling": true,
				"vision": false,
				"maxInputTokens": 128000,
				"maxOutputTokens": 16000
			},
			{
				"id": "~anthropic/claude-sonnet-latest",
				"name": "Kuni (~anthropic/claude-sonnet-latest)",
				"url": "http://localhost:10434/",
				"toolCalling": true,
				"vision": true,
				"maxInputTokens": 128000,
				"maxOutputTokens": 16000
			}
		]
	}
```

### Logging & debugging

- All intercepted requests and responses are logged to `logs_proxy/` (one file per request, timestamped).
- The last raw upstream query is saved to `data/proxy/last_query.json` for inspection.

## Notes
1. The project uses **coroutines** extensively (C++20 feature)
2. **Telegram authentication** will be interactive on first run
3. **AI services** must be running before the application starts
4. **Memory/diary data** is stored in the `data/` directory (created at runtime)

## Monitoring with Prometheus + Grafana

Kuni exposes a Prometheus metrics endpoint on port **9464** that tracks LLM token usage in real time. An optional
Grafana dashboard is provided for visualization.

### Metrics

The following counters are exported under the `llm_usage_*` namespace, labeled by `model`, `chat`, and `function`:

| Metric | Description |
|---|---|
| `llm_usage_input` | Total input (prompt) tokens sent to the LLM |
| `llm_usage_input_cache_hit` | Input tokens served from the provider's prompt cache (cheaper) |
| `llm_usage_input_cache_miss` | Input tokens that missed the cache (full price) |
| `llm_usage_output` | Output (completion) tokens generated by the LLM |

The `chat` and `function` labels are set via `MetricsBreadcumbs::Point` — a RAII helper that temporarily annotates
the current context (e.g., which Telegram chat is being processed, which code path is active). This allows you to
break down costs per chat or per operation (message handling, diary dump, notification loop, etc.).

### Enabling the stack

Prometheus and Grafana services are defined in `docker-compose.yml` but **commented out by default**. To enable them:

1. Uncomment the `prometheus` and `grafana` services in `docker-compose.yml`.
2. Create the data directories:
   ```bash
   mkdir -p bin/prometheus/data bin/grafana/data
   ```
3. Restart the stack:
   ```bash
   docker compose up -d
   ```

### Accessing the dashboard

1. Open **Grafana** at [http://localhost:3000](http://localhost:3000)
2. Log in with the default credentials (`admin` / `admin` — **change this in production**)
3. Navigate to **Dashboards** → **Main** (auto-provisioned)

The dashboard includes:
- **Top chats by token usage** — a table sorted by total input tokens per chat
- **Token usage over time** — time-series chart showing input (split by cache hit/miss) and output tokens
- **Usage by function** — time-series broken down by the code path that triggered the LLM call
- **Raw logs** — a log view of all metric events with chat, function, and model labels

### Configuration files

| File | Purpose |
|---|---|
| `prometheus/prometheus.yml` | Prometheus scrape config — targets `host.docker.internal:9464` |
| `grafana/datasources/datasource.yml` | Auto-provisioned Prometheus datasource pointing at the `prometheus` container |
| `grafana/dashboards.yml` | Auto-provisioning config for the dashboard provider |
| `grafana/dashboards/main.json` | The Grafana dashboard definition (token usage panels) |



# FAQ

## Q: How can I rename my Kuni instance?

A: replace `Kuni` with `CuteAnimeGirlName` across whole project, including `character_base.md` and `character_appearance.md`. Rebuild and restart.


## Q: How can I make my Kuni treat me as her creator?

A: replace `Alex2772` with `YourNerdyNickname` across whole project, including `character_base.md` and `character_appearance.md`. Rebuild and restart.

## Q: Can I disable some of her functionality, i.e. stable diffusion and web search?

A: Yes, just don't launch stable diffusion (remove it from docker compose).

Don't specify Ollama search bearer key. I still recommend providing this key because it's free and it will improve her accuracy especially for small models.

In both cases, if your Kuni attempts to use these tools, it will receive an error and will no longer use them.

## Q: Can I prioritize a chat?

A: Yes, just log in from her account from a regular Telegram client and pin the chat.

## Q: Can I deprioritize a chat?

A: Yes, just log in from her account from a regular Telegram client and mute the chat. Kuni will still see the chat and visit it but rarely.

## Q: Can I provide a news channel for her?

A: Yes, just log in from her account from a regular Telegram client and subscribe to a Telegram channel. Kuni will now known what's going on in
the world.

**Note** Both original Kuni and her sisters don't like to discuss political stuff.

## Q: Can I prevent other people to text my Kuni?

A: Yes, just log in from her account from a regular Telegram client and adjust her privacy settings.

**I recommend** locking down your Kuni completely to reduce security risks.

**Note** Telegram pushes premium users so hard, make sure they can't text your Kuni as well.

## Q: Can I use a local text model with her?

A: Yes, of course, as soon as you are a billionaire, or you are okay with slow responses.

I have experimented with models and described my experience in `src/config.h`. I.e., `qwen3.5:9b` is stupid AF compared to cloud models.


## Q: Can I overwrite her memory?

A: moral stuff aside, yes. Just write to diary entries in `data/diary/`, or text from her account.

DON'T TELL HER YOU HAVE OVERWRITTEN HER MEMORY.


## Q: What context window size does Kuni require?

A: By default, context window size is soft capped by `DIARY_TOKEN_COUNT_TRIGGER = 20000`. This means somewhere around 20K Kuni will dump her context
to the diary and restart with clean memory. Thus, 32K will be enough.

