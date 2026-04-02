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
