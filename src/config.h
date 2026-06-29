#pragma once
#include <chrono>

#include "Endpoint.h"
#include "AUI/Common/ASignal.h"

// clang-format off
#define CONFIG_MODEL \
  X(AString, characterName, "Kuni", "general.character_name") \
  X(AString, characterNickname, "@kunii_chan", "general.character_nickname") \
  X(AString, papikName, "Alex2772", "general.papik_name") \
  X(std::int64_t, papikChatId, 625207005,"general.papik_chat_id") \
  X(std::int64_t, telegramApiId, 0,"general.telegram_api_id") \
  X(AString,telegramApiHash, "", "general.telegram_api_hash") \
  X(EndpointAndModel, llm, (EndpointAndModel{.endpoint={"http://localhost:11434/v1/"},.model="deepseek-v4-flash"}), "general.llm") \
  X(EndpointAndModel, embedding, (EndpointAndModel{.endpoint={"http://localhost:11434/v1/"},.model="qwen3-embedding"}), "general.embedding") \
  X(::Config::LockdownMode, lockdown, ::Config::LockdownMode::PAPIK_ONLY, "general.lockdown") \
  X(bool, canWriteToANewPerson, false, "misc.can_write_to_a_new_person") \
  X(bool, wakeUpOnPinnedChat, false, "misc.wake_up_on_pinned_chat") \
  X(bool, randomlyGoSleep, true, "misc.randomly_go_sleep") \
  X(float, toolReminderProbability, 0.02f, "misc.tool_reminder_probability") \
  X(size_t, diaryTokenCountTrigger, 40000, "misc.diary_token_count_trigger") \
  X(size_t, diaryInjectionMaxLength, 0, "misc.diary_injection_max_length") \
  X(float, diaryPlagiarismThreshold, 0.97, "misc.diary_plagiarism_threshold") \
  X(float, diaryMinRelatedness, 0.80, "misc.diary_min_relatedness") \
  X(size_t, chatMaxHistoryLength, 2000, "misc.chat_max_history_length") \
  X(AOptional<float>, llmTemperature, 0.2, "misc.llm_temperature") \
  X(AOptional<float>, llmTopP, std::nullopt, "misc.llm_top_p") \
  X(AOptional<float>, llmTopK, std::nullopt, "misc.llm_top_k") \
  X(AOptional<float>, llmMinP, std::nullopt, "misc.llm_min_p") \
  X(AOptional<float>, llmPresencePenalty, std::nullopt, "misc.presence_penalty") \
  X(AOptional<float>, llmRepetitionPenalty, std::nullopt, "misc.repetition_penalty") \
  X(float, antiRepeatTriggerMax, 0.95, "misc.anti_repeat_trigger_max") \
  X(float, antiRepeatTriggerAvg, 0.85, "misc.anti_repeat_trigger_avg") \
  X(size_t, antiRepeatMaxHistory, 32, "misc.anti_repeat_max_history") \
  X(bool, capabilityWebSearch, false, "capabilities.web_search.enabled") \
  X(AString, webSearchOllamaKey, "", "capabilities.web_search.ollama_bearer_key") \
  X(bool, capabilityVision, false, "capabilities.vision.enabled") \
  X(EndpointAndModel, llmImageToText, (EndpointAndModel{.endpoint={"http://localhost:11434/v1/"},.model="qwen3.5:9b"}), "capabilities.vision.llm_image_to_text") \
  X(bool, capabilityUseStickers, false, "capabilities.use_stickers.enabled") \
  X(bool, capabilityTakePhoto, false, "capabilities.take_photo.enabled") \
  X(Endpoint, sdEndpoint, (Endpoint{.baseUrl="http://localhost:7860/"}),"capabilities.take_photo.sd.endpoint") \
  X(AString, sdCheckpoint, "novaAnimeXL_ilV170.safetensors", "capabilities.take_photo.sd.checkpoint") \
  X(bool, capabilityHearing, false, "capabilities.hearing.enabled") \
  X(EndpointAndModel, llmAudioToText, (EndpointAndModel{.endpoint={"http://localhost:11434/v1/"},.model="openai/gpt-4o-mini-transcribe"}), "capabilities.hearing.llm_audio_to_text") \
  X(bool, capabilityRecordVoice, false, "capabilities.record_voice.enabled") \
  X(::Config::TTSBackend, recordVoiceBackend, ::Config::TTSBackend::ELEVENLABS, "capabilities.record_voice.backend") \
  X(AString, recordVoiceElevenLabsKey, "", "capabilities.record_voice.elevenlabs.key") \
  X(AString, recordVoiceElevenLabsVoice, "pPdl9cQBQq4p6mRkZy2Z", "capabilities.record_voice.elevenlabs.voice_id") \
  X(AString, recordVoiceOpenAIUrl, "https://api.openai.com/v1/", "capabilities.record_voice.openai.url") \
  X(AString, recordVoiceOpenAIKey, "", "capabilities.record_voice.openai.key") \
  X(AString, recordVoiceOpenAIModel, "tts-1", "capabilities.record_voice.openai.model") \
  X(AString, recordVoiceOpenAIVoice, "alloy", "capabilities.record_voice.openai.voice") \
  X(bool, proxyEnabled, false, "capabilities.proxy.enabled") \

// clang-format on

struct Config {
    // these are technical constants that are not interesting for consumers
    static constexpr auto REQUEST_TIMEOUT = std::chrono::seconds(30);
    static constexpr auto SLEEP_MAX_TIME = std::chrono::hours(6);

    enum class LockdownMode {
        NONE, // public
        CONTACTS_ONLY,
        PAPIK_ONLY,
    };

    enum class TTSBackend {
        ELEVENLABS,
        OPENAI,
    };

#define X(cppType, cppName, cppDefaultValue, tomlPath) cppType cppName = cppDefaultValue;
    CONFIG_MODEL
#undef X
};

extern emits<> gConfigUpdated;

const Config& config();