#include "config.h"

#include <toml.hpp>

#include "AUI/IO/APath.h"
#include "AUI/Logging/ALogger.h"
#include "AUI/Platform/linux/AINotifyFileWatcher.h"
#include "AUI/Platform/unix/UnixIoThread.h"
#include "AUI/Util/kAUI.h"

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(Endpoint, baseUrl, bearerKey)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(EndpointAndModel, endpoint, model)

template <>
struct toml::from<AString> {
    static AString from_toml(const toml::value& v) { return v.as_string(); }
};

template <>
struct toml::into<AString> {
    template <typename T>
    static toml::basic_value<T> into_toml(const AString& str) {
        return str.toStdString();
    }
};

template <typename T>
struct toml::from<AOptional<T>> {
    static AOptional<T> from_toml(const toml::value& v) {
        if (v.is_string()) {
            if (v.as_string() == "none") {
                return std::nullopt;
            }
        }
        return toml::get<T>(v);
    }
};

template <typename T>
struct toml::into<AOptional<T>> {
    template <typename V>
    static toml::basic_value<V> into_toml(const AOptional<T>& v) {
        if (!v) {
            return "none";
        }
        return *v;
    }
};

template <>
struct toml::from<Config::LockdownMode> {
    static Config::LockdownMode from_toml(const toml::value& v) {
        const auto& str = v.as_string();
        if (str == "none")
            return Config::LockdownMode::NONE;
        if (str == "contacts_only")
            return Config::LockdownMode::CONTACTS_ONLY;
        if (str == "papik_only")
            return Config::LockdownMode::PAPIK_ONLY;
        throw AException("Invalid lockdown mode: {}"_format(toml::format(v)));
    }
};

template <>
struct toml::into<Config::LockdownMode> {
    template <typename T>
    static toml::basic_value<T> into_toml(Config::LockdownMode mode) {
        switch (mode) {
            case Config::LockdownMode::NONE:
                return "none";
            case Config::LockdownMode::CONTACTS_ONLY:
                return "contacts_only";
            case Config::LockdownMode::PAPIK_ONLY:
                return "papik_only";
        }
        throw AException("bad");
    }
};

template <>
struct toml::from<Config::TTSBackend> {
    static Config::TTSBackend from_toml(const toml::value& v) {
        const auto& str = v.as_string();
        if (str == "elevenlabs")
            return Config::TTSBackend::ELEVENLABS;
        if (str == "openai")
            return Config::TTSBackend::OPENAI;
        throw AException("Invalid TTS backend: {}"_format(toml::format(v)));
    }
};

template <>
struct toml::into<Config::TTSBackend> {
    template <typename T>
    static toml::basic_value<T> into_toml(Config::TTSBackend backend) {
        switch (backend) {
            case Config::TTSBackend::ELEVENLABS:
                return "elevenlabs";
            case Config::TTSBackend::OPENAI:
                return "openai";
        }
        throw AException("bad");
    }
};

// Comments are set separately in config::get(), after toml is parsed.
static const std::unordered_map<AStringView, AStringView> CONFIG_COMMENTS = {
    {
      "general.character_name",
      "Used by system and other prompts, defines character name.\n"
      "\n"
      "Character's phylosophy and appearance can be adjusted in character_base.md and\n"
      "character_appearance.md, respectively.",
    },
    {
      "general.character_nickname",
      "Character nickname in Telegram."
    },
    { "general.papik_name",
      "Used by system and other prompts, defines instance owner name.\n"
      "Should match with papik_chat_id account name." },
    {
      "general.papik_chat_id",
      "User id of the instance owner.\n"
      "Kuni's kernel gives several benefits to the \"instance owner user\":\n"
      "- Kuni can't ban instance owner\n"
      "- Messages from instance owner are prioritized (processed ASAP)\n"
      "- A message from instance owner wakes up Kuni if she sleeps\n"
      "\n"
      "Your user id can be obtained by @getmyid_bot, @raw_data_bot or @userinfobot.",
    },
    {
      "general.telegram_api_id",
      "tdlib API key, see\n"
      "https://core.telegram.org/api/obtaining_api_id\n"
      "Если ты русский и получаешь ошибку ERROR, смотри\n"
      "https://habr.com/ru/articles/923168/",
    },
    {
      "general.telegram_api_hash",
      "tdlib API hash, acquired on the same page as telegram_api_id.",
    },
    {
      "general.llm",
      "Main LLM endpoint. Can be text-only.\n"
      "\n"
      "- local ollama instance: \"http://localhost:11434/v1/\",\n"
      "- openrouter: \"https://openrouter.ai/api/v1/\",\n"
      "- specify bearerKey for 3rdparty services,\n"
      "\n"
      "Generally, deepseek-v4-flash is cheap and fine enough. Local models sub 26b are stupid.",
    },
    {
      "general.embedding",
      "Embeddings endpoint. Used for RAG. A local qwen3-embedding is good enough.\n"
      "You should specify an **embedding** model specifically.",
    },
    {
      "general.lockdown",
      "Whether other users can access Kuni.\n"
      "Restricted users will be ignored.\n"
      "LLM will see allowed chats only.\n"
      "\n"
      "- \"none\" Kuni will be publicly available (everyone can access her)\n"
      "- \"contacts_only\" only Kuni's contacts can access her\n"
      "- \"papik_only\" papik_chat_id is the only one who can access her",
    },
    {
      "capabilities.take_photo.enabled",
      "Whether Kuni can generate new photos. Requires `vision` capability.",
    },
    {
      "capabilities.take_photo.sd.endpoint",
      "Address to stable-diffusion deployment (from docker-compose.yml)",
    },
    {
      "capabilities.take_photo.sd.checkpoint",
      "Stable diffusion checkpoint. https://civitai.com/models/376130/nova-anime-xl",
    },
    {
      "capabilities.web_search.enabled",
      "Whether Kuni can use web search.",
    },
    {
      "capabilities.vision.enabled",
      "Whether Kuni can see images/stickers. You should use a vision-capable LLM here.\n"
      "Images/stickers are transcribed to text via LLM and passed to main LLM. Transcriptions are cached.",
    },
    {
      "misc.can_write_to_a_new_person",
      "Allows Kuni to text a completely new person Kuni didn't have a prior dialog with.\n"
      "This defaults to false to avoid using Kuni as spam bot.",
    },
    {
      "capabilities.use_stickers.enabled",
      "Allows Kuni to save and send stickers. Requires `vision` capability.",
    },
    {
      "capabilities.hearing.enabled",
      "Whether Kuni can listen to voice messages if telegram premium is not available.",
    },
    {
      "misc.wake_up_on_pinned_chat",
      "If true, the pinned chats will wake up Kuni as soon as a message receives. If set to false, only the\n"
      "PAPIK_CHAT_ID will be able to do so.\n"
      "\n"
      "The instance owner (also known as PAPIK) can pin chats from Kuni's Telegram account with his friends so Kuni's\n"
      "tokens preservation mechanisms (sleep) are not applied to them.",
    },
    {
      "misc.randomly_go_sleep",
      "If true, Kuni will randomly go to sleep after some time of inactivity to save LLM tokens.\n"
      "While sleeping, Kuni won't respond to messages (except from papik_chat_id).\n",
    },
    {
      "misc.tool_reminder_probability",
      "Probability (0.0-1.0) that Kuni will be reminded about available tools in a given message.\n"
      "Helps prevent Kuni from forgetting she has tools like record audio or photo generation.",
    },
    {
      "misc.diary_token_count_trigger",
      "When the conversation context exceeds this token count, Kuni dumps her memory to the diary\n"
      "and restarts with a clean context. Keeps context window usage under control.\n"
      "Requires context window >= this value + some headroom.",
    },
    {
      "misc.diary_injection_max_length",
      "Maximum number of tokens injected from diary into the context per message.\n"
      "0 means unlimited. Tune this if diary recall is too verbose.",
    },
    {
      "misc.diary_plagiarism_threshold",
      "Cosine similarity threshold (0.0-1.0) above which a new diary entry is considered a duplicate\n"
      "of an existing one and will not be saved. Prevents redundant diary entries.",
    },
    {
      "misc.diary_min_relatedness",
      "Minimum cosine similarity (0.0-1.0) for a diary entry to be injected into context.\n"
      "Higher values = only very relevant memories are recalled.",
    },
    {
      "misc.chat_max_history_length",
      "Maximum number of messages kept in the in-memory chat history per chat.\n"
      "Older messages beyond this limit are dropped from the active context.",
    },
    {
      "misc.llm_temperature",
      "LLM sampling temperature. Higher = more creative/random, lower = more deterministic.\n"
      "\"none\" to use the model's default.",
    },
    {
      "misc.llm_top_p",
      "LLM nucleus sampling (top-p). Limits token selection to the top cumulative probability mass.\n"
      "\"none\" to use the model's default.",
    },
    {
      "misc.llm_top_k",
      "LLM top-k sampling. Limits token selection to the k most likely tokens.\n"
      "\"none\" to use the model's default.",
    },
    {
      "misc.llm_min_p",
      "LLM min-p sampling. Filters tokens below a minimum probability relative to the top token.\n"
      "\"none\" to use the model's default.",
    },
    {
      "misc.presence_penalty",
      "LLM presence penalty. Positive values discourage the model from repeating topics already mentioned.\n"
      "\"none\" to use the model's default.",
    },
    {
      "misc.repetition_penalty",
      "LLM repetition penalty. Values > 1.0 penalize repeated tokens.\n"
      "\"none\" to use the model's default.",
    },
    {
      "misc.anti_repeat_trigger_max",
      "Maximum cosine similarity threshold for the anti-repeat mechanism.\n"
      "If the new response is too similar to a recent one (above this threshold), it is regenerated.",
    },
    {
      "misc.anti_repeat_trigger_avg",
      "Average cosine similarity threshold for the anti-repeat mechanism.\n"
      "Works together with anti_repeat_trigger_max to detect repetitive responses.",
    },
    {
      "misc.anti_repeat_max_history",
      "Number of recent responses kept in history for the anti-repeat similarity check.",
    },
    {
      "capabilities.web_search.ollama_bearer_key",
      "Bearer key for the Ollama web search integration.\n"
      "Ollama provides a free web search API key. Leave empty to disable web search via Ollama.",
    },
    {
      "capabilities.vision.llm_image_to_text",
      "LLM endpoint and model used to transcribe images/stickers to text.\n"
      "Should be a vision-capable model. Transcriptions are cached to save tokens.",
    },
    {
      "capabilities.hearing.llm_audio_to_text",
      "LLM endpoint and model used to transcribe voice messages to text.\n"
      "Used when Telegram Premium (native transcription) is not available.",
    },
    {
      "capabilities.record_voice.enabled",
      "Whether Kuni can send voice messages (text-to-speech).\n"
      "Requires configuring a TTS backend (elevenlabs or openai).",
    },
    {
      "capabilities.record_voice.backend",
      "TTS backend to use for voice message generation.\n"
      "- \"elevenlabs\" — ElevenLabs API (high quality, requires key)\n"
      "- \"openai\" — OpenAI TTS API (requires key)/local OpenAI-compatible TTS service",
    },
    {
      "capabilities.record_voice.elevenlabs.key",
      "ElevenLabs API key for TTS. Required when backend is \"elevenlabs\".\n"
      "Get your key at https://elevenlabs.io/",
    },
    {
      "capabilities.record_voice.elevenlabs.voice_id",
      "ElevenLabs voice ID to use for TTS. Find voice IDs in your ElevenLabs account.",
    },
    {
      "capabilities.record_voice.openai.url",
      "OpenAI-compatible TTS API base URL. Can point to a self-hosted TTS service.",
    },
    {
      "capabilities.record_voice.openai.key",
      "API key for the OpenAI TTS endpoint. Required when backend is a non-local service.",
    },
    {
      "capabilities.record_voice.openai.model",
      "OpenAI TTS model name, e.g. \"tts-1\" or \"tts-1-hd\".",
    },
    {
      "capabilities.record_voice.openai.voice",
      "OpenAI TTS voice name, e.g. \"alloy\", \"echo\", \"fable\", \"onyx\", \"nova\", \"shimmer\".",
    },
    {
      "capabilities.proxy.enabled",
      "Whether to enable the built-in OpenAI-compatible proxy server.\n"
      "When enabled, Kuni exposes an API endpoint that forwards requests through her LLM,\n"
      "allowing external tools (e.g. VS Code Copilot) to use Kuni as a model backend.",
    },
};

static constexpr auto CONFIG_TOML = "config.toml";
static constexpr auto LOG_TAG = CONFIG_TOML;

/**
 * @brief Apparently, toml11 can't make an extra `\n` right after keys. we'll add these by ourselves to make the toml
 * cleaner.
 */
static constexpr auto REPLACE_ME_WITH_NEWLINE_SHITFIX = "REPLACEMEWITHBLANKLINE";

template <typename T>
static void populate(toml::ordered_value& dst, T defaultValue) {
    if (dst.is_empty()) {
        dst = defaultValue;
    }

    dst.comments().insert(dst.comments().begin(), REPLACE_ME_WITH_NEWLINE_SHITFIX);
}

template <typename T>
static void populateComment(toml::ordered_value& dst, T defaultValue, AStringView comment) {
    if (dst.comments().empty() || (dst.comments().size() == 1 && dst.comments()[0] == REPLACE_ME_WITH_NEWLINE_SHITFIX)) {
        auto stripped = comment;
        while (stripped.startsWith('\n')) stripped = stripped.substr(1);
        while (stripped.endsWith('\n')) stripped = stripped.substr(0, stripped.size() - 1);

        std::vector<std::string> newComments;
        newComments.push_back(REPLACE_ME_WITH_NEWLINE_SHITFIX);
        for (auto line : stripped.split('\n')) {
            line.insert(0, ' ');
            newComments.push_back(std::move(line));
        }
        newComments.push_back(" ");   // blank line
        AString fmt = toml::format(toml::value(defaultValue));
        fmt.replaceAll("\n", "; ");
        newComments.push_back(" default value: {}"_format(fmt));
        dst.comments() = std::move(newComments);
    }
}

static toml::ordered_value& findOrEmplaceEmpty(toml::ordered_value& root, AStringView dottedPath) {
    toml::ordered_value* cur = &root;
    for (auto key : dottedPath.split('.')) {
        cur = &(*cur)[key.toStdString()];
    }
    return *cur;
}

static void formatTable(toml::ordered_value& toml) {
    if (!toml.is_table()) {
        return;
    }
    if (toml.comments().empty()) {
        toml.as_table_fmt().fmt = toml::table_format::dotted;
    }
    for (auto& i : toml.as_table()) {
        formatTable(i.second);
    }
}

static void save(toml::ordered_value toml) {
    for (auto& i : toml.as_table()) {
        // just a stylistic choice to flatten multilayered tables.
        formatTable(i.second);
    }

    AString formatted = toml::format<toml::ordered_type_config>(toml);
    formatted.replaceAll("#{}"_format(REPLACE_ME_WITH_NEWLINE_SHITFIX), "");
    AFileOutputStream(CONFIG_TOML) << formatted;
}

static void scanUnaccessedValues(const toml::ordered_table& table) {
    for (const auto& [k, v] : table) {
        if (!v.accessed()) {
            auto location = v.location();
            ALogger::warn(LOG_TAG) << toml::format_error(
                "an unexpected key \"{}\""_format(k), location, "will be ignored");
        }
    }
}

static Config load(bool saveBack = false) {
    // if (!APath(CONFIG_TOML).isRegularFileExists()) {
    //     // config.toml does not exist - so we create one.
    //     save(toml::value(Config {}));
    //     ALogger::info(LOG_TAG) << "Created fresh config file";
    // }
    bool aNewFile = false;
    if (auto file = APath(CONFIG_TOML); !file.isRegularFileExists()) {
        file.touch();
        ALogger::info(LOG_TAG) << "Created fresh config file";
        aNewFile = true;
    }
    toml::ordered_value toml = toml::parse<toml::ordered_type_config>(CONFIG_TOML);
#define X(cppType, cppName, cppDefaultValue, tomlPath) \
    populate<cppType>(findOrEmplaceEmpty(toml, tomlPath), cppDefaultValue);
    CONFIG_MODEL
#undef X

#define X(cppType, cppName, cppDefaultValue, tomlPath)                         \
    if (auto it = CONFIG_COMMENTS.find(tomlPath); it != CONFIG_COMMENTS.end()) \
        populateComment<cppType>(findOrEmplaceEmpty(toml, tomlPath), cppDefaultValue, it->second);
    CONFIG_MODEL
#undef X

    toml.at("general").comments() = std::vector<std::string> {
        R"(# Kuni kernel project, https://github.com/alex2772/kuni/
# WARNING - SENSITIVE DATA! DO NOT SHARE NOR COMMIT THIS FILE!!!
#
# This file contains all the settings available in Kuni's kernel.
# To adjust prompts, go to prompts/ directory.
# If kuni is running, it will autoreload changes to this file and prompts automatically.
# Setting up [general] section is mandatory, everything else is optional.
#
# ВНИМАНИЕ - ЗДЕСЬ ХРАНЯТСЯ ТВОИ КЛЮЧИ/СЕКРЕТЫ! НИКОМУ НЕ ДАВАЙ ДОСТУП К ЭТОМУ ФАЙЛУ!!!
#
# В этом файле содержатся все настройки, которые доступны в ядре Куни.
# Если хочешь поменять промпты, сходи в папку prompts/.
# Если kuni уже запущено, оно само перезагрузит изменения в этот файл и промпты.
# Тебе обязательно нужно настроить секцию [general] под себя, остальное неважно.
)"
    };
    toml.at("capabilities").comments() = std::vector<std::string> { " Optional builtin modules" };
    toml.at("misc").comments() = std::vector<std::string> { " Technical stuff you probably don't want to adjust" };


#ifndef AUI_TESTS_MODULE
    if (aNewFile) {
        save(toml);
        ALogger::err(LOG_TAG) << R"(
########################################################################################################################
#                                                      IMPORTANT                                                       #
#                                            Please populate config.toml                                               #
#                                                     and restart                                                      #
########################################################################################################################
)";
        std::exit(-1);
        return {};
    }
#endif

    Config out;
#define X(cppType, cppName, cppDefaultValue, tomlPath)    \
    {                                                     \
        auto keys = AString(tomlPath).split('.');         \
        toml::ordered_value* v = &toml;                   \
        for (auto& k : keys) v = &v->at(k.toStdString()); \
        out.cppName = toml::get<cppType>(*v);             \
    }
    CONFIG_MODEL
#undef X

    scanUnaccessedValues(toml.as_table());

    // save back: if an instance owner updates kuni kernel, new fields will be added here
    if (saveBack) {
        save(toml);
    }

#ifndef AUI_TESTS_MODULE
    // validation
    {
        const auto& value = toml["general"]["telegram_api_id"];
        if (value.as_integer() == 0) {
            ALogger::err(LOG_TAG) << toml::format_error(
                (toml::make_error_info("general.telegram_api_id should be populated", value, "the actual value is 0")));
            std::exit(-1);
        }
    }
    {
        const auto& value = toml["general"]["telegram_api_hash"];
        if (value.as_string().empty()) {
            ALogger::err(LOG_TAG) << toml::format_error((toml::make_error_info(
                "general.telegram_api_hash should be populated", value, "the actual string is empty")));
            std::exit(-1);
        }
    }
#endif
    ALogger::info(LOG_TAG) << CONFIG_TOML << " loaded";
    return out;
}

emits<> gConfigUpdated;

const Config& config() {
    static Config cfg = load(true);
    AUI_DO_ONCE {
        static auto watcher = _new<AINotifyFileWatcher>();
        auto h = watcher->addWatch(APath(CONFIG_TOML).absolute(), AINotifyFileWatcher::Mask::MODIFY);
        AObject::connect(watcher->fired, AObject::GENERIC_OBSERVER, [h](const AINotifyFileWatcher::Event& event) {
            if (event.watchDescriptor != h) {
                return;
            }
            ALogger::info(LOG_TAG) << CONFIG_TOML << " updated - reloading";
            try {
                cfg = load();
            } catch (const AException& e) {
                ALogger::err(LOG_TAG) << e;
            } catch (const std::exception& e) {
                ALogger::err(LOG_TAG) << e;
            }
            *watcher ^ gConfigUpdated();
        });
    };
    return cfg;
}
