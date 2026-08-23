#include "App.h"
#include "IPlugin.h"

using namespace std::chrono_literals;

namespace {

constexpr auto LOG_TAG = "main";

AEventLoop gEventLoop;
}   // namespace

AArc<IPlugin> __attribute__((weak)) kuni_private_plugin_init(App& app) {
    // this is a mechanism allowing to non-intrusively modify kuni's kernel code without affecting kernel's code itself.
    // this allows you to easily sync with mainline public kuni kernel code while making exclusive features.
    //
    // usage:
    // 1. create a kuni-private dir alongside kuni/ repo:
    // - kuni/
    // - kuni-private/
    //
    // 2. in that dir, create a CMakeLists.txt with the following contents:
    //
    // file(GLOB_RECURSE SRCS src/*.cpp)
    // target_sources(kuni PRIVATE ${SRCS})
    // target_include_directories(kuni PRIVATE src)
    //
    // 3. create kuni-private/src/plugin.cpp:
    // #include <App.h>
    // class KuniPrivatePlugin: public IPlugin {
    // public:
    //    KuniPrivate(App& app) {
    //      // do whatever shit you want here
    //    }
    // };
    // AArc<IPlugin> kuni_private_plugin_init(App& app) {
    //    return _new<KuniPrivatePlugin>(app);
    // }
    //

    // this function is a weak stub, i.e., an external cpp defining this function will override implementation.
    return nullptr;
}

AUI_ENTRY {
    config();   // load config
    if (args.contains("--debug")) {
        ALogger::info(LOG_TAG) << "--debug mode enabled; service is not running";
        _new<KuniDebugWindow>()->show();
        return 0;
    }

    using namespace std::chrono_literals;

    AAsyncHolder async;
    _<prometheus::IExporter> prometheus;
    _<App> app;
    _<proxy_server::IProxyServer> proxyServer;
    _<proxy_server::ContextBridge> contextBridge;

    if (config().telegramEnabled) {
        auto telegram = _new<TelegramClientImpl>();
        async << [](_<ITelegramClient> telegram) -> AFuture<> {
            ALogger::info(LOG_TAG) << "Waiting for Telegram network...";
            co_await telegram->waitForConnection();
            switch (config().lockdown) {
                case Config::LockdownMode::NONE: break;
                case Config::LockdownMode::CONTACTS_ONLY:
                    ALogger::info(LOG_TAG) << "Lockdown mode is enabled (config.toml lockdown). Kuni can only chat with her contacts."; break;
                case Config::LockdownMode::PAPIK_ONLY:
                    ALogger::info(LOG_TAG) << "Lockdown mode is enabled (config.toml lockdown). Kuni can only open chat with ID {} (PAPIK_CHAT_ID)."_format(config().papikChatId); break;
            }
        }(telegram);

        AObject::connect(telegram->loggedIn, telegram, [&] {
            auto openAI = _new<OpenAIChatMeasurable>(std::make_unique<OpenAIChatImpl>());
            app = _new<App>(telegram, openAI);
            if (auto plugin = kuni_private_plugin_init(*app)) {
                app->plugins << std::move(plugin);
            }
            async << app->sendNotificationsOnInit();

            if (config().proxyEnabled) {
                auto diary = std::make_shared<Diary>(Diary::Init { .diaryDir = "data/diary", .openAI = openAI });
                proxyServer = proxy_server::init({
                  .upstreamEndpoint = config().llm.endpoint,
                  .port = 10434,
                  .toolsFactory = [openAI, diary](IOpenAIChat::Session ctx) {
                      return OpenAITools { tools::ask([ctx = std::move(ctx)] { return ctx.empty() ? AString {} : AString(ctx.last().content); }, openAI, *diary) };
                  },
                });
                contextBridge = _new<proxy_server::ContextBridge>(proxy_server::ContextBridge::Config { .endpoint = config().llm.endpoint, .diary = diary });
                AObject::connect(proxyServer->sentRequestToLLM, AUI_SLOT(contextBridge)::collectRequestToLLM);
                app->chatHistoryMessageProcessors << contextBridge;
            }
            prometheus = prometheus::setup(app->metricBreadcumbs());
            prometheus->registerOpenAI(*openAI);
            prometheus->registerAppBase(*app);
            _new<AThread>([] {
                ALogger::info(LOG_TAG) << "Bot is up and running. Press enter to shutdown gracefully.";
                std::cin.get();
                ALogger::info(LOG_TAG) << "Bot is shutting down. Please give some time to dump remaining context";
                gEventLoop.stop();
            })->start();
        });
    } else {
        auto openAI = _new<OpenAIChatMeasurable>(std::make_unique<OpenAIChatImpl>());
        if (config().proxyEnabled) {
            auto diary = std::make_shared<Diary>(Diary::Init { .diaryDir = "data/diary", .openAI = openAI });
            proxyServer = proxy_server::init({
              .upstreamEndpoint = config().llm.endpoint,
              .port = 10434,
              .toolsFactory = [openAI, diary](IOpenAIChat::Session ctx) {
                  return OpenAITools { tools::ask([ctx = std::move(ctx)] { return ctx.empty() ? AString {} : AString(ctx.last().content); }, openAI, *diary) };
              },
            });
            contextBridge = _new<proxy_server::ContextBridge>(proxy_server::ContextBridge::Config { .endpoint = config().llm.endpoint, .diary = diary });
            AObject::connect(proxyServer->sentRequestToLLM, AUI_SLOT(contextBridge)::collectRequestToLLM);
            ALogger::info(LOG_TAG) << "Proxy server started standalone (No Telegram)!";
        }
        
        auto dummyBreadcrumbs = _new<MetricsBreadcumbs>();
        prometheus = prometheus::setup(dummyBreadcrumbs);
        prometheus->registerOpenAI(*openAI);
        _new<AThread>([] {
            ALogger::info(LOG_TAG) << "Bot is up and running. Press enter to shutdown gracefully.";
            std::cin.get();
            ALogger::info(LOG_TAG) << "Bot is shutting down. Please give some time to dump remaining context";
            gEventLoop.stop();
        })->start();
    }

    IEventLoop::Handle h(&gEventLoop);
    gEventLoop.loop();

    if (app) {
        async << app->diaryDumpMessages();
    }
    if (contextBridge) { async << contextBridge->collectAndSaveSessionsNotNewerThan(std::chrono::system_clock::now()); }

    while (!async.empty()) { gEventLoop.iteration(); }
    return 0;
}
