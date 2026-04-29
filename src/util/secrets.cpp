#include "secrets.h"

#include "AUI/IO/APath.h"
#include "AUI/Logging/ALogger.h"

#include <toml.hpp>

static constexpr auto LOG_TAG = "secrets";
static constexpr auto TEMPLATE = R"(
# SECRETS file, kuni project, https://github.com/alex2772/kuni/
# WARNING - SENSITIVE DATA! DO NOT SHARE NOR COMMIT THIS FILE!!!

[telegram_api]
# tdlib API key, see
# https://core.telegram.org/api/obtaining_api_id
id = 0
hash = ""

[ollama]
# uncomment and specify this if you want Ollama web search
# bearer_key = ""

[elevenlabs]
# ElevenLabs API key for TTS
# api_key = ""
# Optional default voice ID. If omitted, the built-in default voice id is used.
# voice_id = "pPdl9cQBQq4p6mRkZy2Z"
)";


toml::basic_value<toml::type_config> util::secrets() {
    static auto data = [] {
        const auto location = APath("data") / "secrets.toml";
        if (!location.isRegularFileExists()) {
            AFileOutputStream(location) << TEMPLATE;
            ALogger::err(LOG_TAG) << R"(
########################################################################################################################
#                                                      IMPORTANT                                                       #
#                                         Please populate data/secrets.toml                                            #
########################################################################################################################
)";
            std::exit(-1);
        }

        try {
            toml::color::enable();
            auto data = toml::parse(location, toml::spec::v(1,1,0));;

            {
                const auto& value = data["telegram_api"]["id"];
                if (value.as_integer() == 0) {
                    ALogger::err(LOG_TAG) << toml::format_error((toml::make_error_info("telegram_api.id should be populated", value, "the actual value is 0")));
                    std::exit(-1);
                }
            }
            {
                const auto& value = data["telegram_api"]["hash"];
                if (value.as_string().empty()) {
                    ALogger::err(LOG_TAG) << toml::format_error((toml::make_error_info("telegram_api.hash should be populated", value, "the actual string is empty")));
                    std::exit(-1);
                }
            }
            return data;
        } catch (const std::exception& e) {
            ALogger::err(LOG_TAG) << "Failed to parse " << location;
            throw;
        }
    }();
    return data;
}
