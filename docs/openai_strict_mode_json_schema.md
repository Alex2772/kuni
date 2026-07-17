# Fix: strict-mode JSON schema for OpenAITools parameters

## Problem

vibecode.moe (and OpenAI structured outputs / strict mode in general) enforces a rule that plain
`.required = {}` (i.e. some properties simply omitted from `required`) does not satisfy: **every**
property listed in `parameters.properties` must also be listed in `required`. Truly optional
parameters must instead be expressed as nullable, via a `type` union that includes `"null"`
(e.g. `"type": ["string", "null"]`).

The existing `OpenAITools::Tool::Parameters::Property` schema had no way to express "nullable" or
"one-value-or-array" types, and one tool (`remove_message`) was working around the lack of union
types with a literal (non-standard) string `"integer|array"` as `type`, which strict-mode clients
reject outright.

Confirmed working after this change: vibecode.moe. OpenRouter continued to work as before (it was
not broken by strict mode either way). Ollama was not tested.

## Changes

### `src/OpenAITools.h`

Added three new fields to `Property` (and a small factory helper):

- `bool nullable = false` — when true, `"null"` is added to the property's `type` union. Use this
  for any argument the LLM may legitimately omit — it must still be listed in `required`, but the
  model can pass `null`.
- `bool orArray = false` — when true, `"array"` is added to the `type` union, meaning the LLM may
  pass either a single value of `type` or an array of `items`.
- `_<Property> items` — element schema, required when `type == "array"` or `orArray == true`.
- `static _<Property> make(Property p)` — convenience factory for building an `items` schema inline
  (properties are stored by value elsewhere, but `items` needs a `_<Property>` handle).

### `src/OpenAITools.cpp`

Replaced the `AJSON_FIELDS(...)` auto-generated serialization for `Property` with a hand-written
`AJsonConv<OpenAITools::Tool::Parameters::Property>` specialization. It builds the `type` field as
either a single string (when there's exactly one type) or a JSON array (when `orArray`/`nullable`
add extra types), and emits `items` when the property is array-typed. `fromJson` is intentionally
`AUI_ASSERT(0)` — tool schemas are only ever sent *to* the LLM, never parsed back, so no
deserialization path is needed.

### Tool-by-tool fixes

- **`src/tools/ask.cpp`** — `include_web_search_results` was optional-by-omission; added it to
  `required` (it already defaults sensibly in the handler, so no nullable needed — the LLM is
  simply now forced to state it explicitly).
- **`src/tools/forward_message.cpp`** — `message_ids` is an array; added `.items` typed as
  `integer` (previously undeclared, which is invalid under strict mode for array types). `comment`
  is genuinely optional, so it's now `.nullable = true` and moved into `required`.
- **`src/tools/remove_message.cpp`** — replaced the non-standard `.type = "integer|array"` hack
  with `.orArray = true` + `.items = Property::make({.type = "integer"})`, producing a proper
  `["integer", "array"]` type union with a declared item schema.
- **`src/tools/send_telegram_message.cpp`** — `text`, `photo_filename`, `audio_filename`, and
  `reply_to_message_id` are all optional depending on what's being sent (text-only, photo-only,
  reply vs. not, etc.). All four became `.nullable = true` and were added to `required`.
- **`src/tools/stickers.cpp`** — `reply_to_message_id` is optional; same treatment (`.nullable =
  true`, added to `required`).

## Why this approach

Rather than patching each tool's JSON by hand or special-casing strict mode per-provider, the fix
is at the schema level: `Property` now has enough expressiveness (`nullable`, `orArray`, `items`)
to describe what these tools actually accept, and every tool call site was updated to declare its
true optionality via `nullable` + `required` instead of omission. This keeps a single JSON
generation path (`AJsonConv<Property>::toJson`) correct for every strict-mode-compliant client,
rather than branching behavior by provider.

## Verification

Not yet run in this session: build/tests. The user (RedMoth) reports having compiled these changes
locally — vibecode.moe works, OpenRouter still works, Ollama untested. Recommended before merging:
a full rebuild and a run of the existing test suite (if any) to make sure the new `AJsonConv`
specialization compiles cleanly and `Property::make` call sites don't collide with other usages.
