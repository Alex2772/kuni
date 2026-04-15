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

## Technical details

- C++20 CMake-based project with heavy usage of modern C++ features such as coroutines
- Uses [tdlib](https://core.telegram.org/tdlib) for Telegram API access
- Used with a self-hosted Ollama server through OpenAI API.

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

### 3. Create Secrets File
Create `build/secrets/secrets.h` with:
```cpp
#pragma once

namespace secrets {
static constexpr auto TELEGRAM_API_ID = 123456;     // tdlib API key, see
static constexpr auto TELEGRAM_API_HASH = "abcdef"; // https://core.telegram.org/api/obtaining_api_id
static constexpr auto DEEPSEEK_BEARER_KEY = "";     // specify this if you want to use deepseek cloud
static constexpr auto OLLAMA_BEARER_KEY = "";       // specify this if you want ask_google capability
}
```

### Build Steps

```bash
# Configure with CMake (from project root)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build the project
cmake --build build
```

**Alternative using VS Code CMake Tools:**
1. Open the project in VS Code
2. Use the CMake extension to configure and build
3. Select the `kuni` target to build

### Dependencies
The project uses CMake's `auib_import` to automatically fetch:
- **AUI Framework** (C++ GUI framework)
- **tdlib** (Telegram client library)

These are automatically downloaded during the CMake configuration phase.

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

### 2. Run Tests
```bash
# Build and run tests
cmake --build build --target Tests
cd build && ctest --output-on-failure
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
- `cmake-build-debug/` - Debug build directory

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

## Notes
1. The project uses **coroutines** extensively (C++20 feature)
2. **Telegram authentication** will be interactive on first run
3. **AI services** must be running before the application starts
4. **Memory/diary data** is stored in the `data/` directory (created at runtime)
