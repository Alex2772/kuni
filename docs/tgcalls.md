# tgcalls — интеграция голосовых звонков

Сводка по архитектуре звонков в Telegram Desktop и что это значит для kuni.

---

## Стек зависимостей tdesktop

```
tdesktop
  ├── lib_tgcalls          ← главная либа звонков (desktop-app/tgcalls)
  │     └── tg_owt         ← форк Chromium WebRTC под Telegram
  │           ├── libvpx, libopenh264
  │           ├── opus, libsrtp
  │           ├── libyuv, librnnoise
  │           └── FFmpeg
  ├── lib_webrtc            ← обёртка-абстракция над tg_owt
  └── external_openal       ← аудио
```

Все зависимости подтягиваются через cmake-таргеты из [desktop-app](https://github.com/desktop-app) орги на GitHub.

---

## Архитектура звонков в tdesktop

### Обычные P2P звонки (`calls/`)
- `calls_call.cpp/h` — основная логика звонка
- `calls_instance.cpp/h` — менеджер инстансов
- `calls_controller_webrtc.cpp/h` — WebRTC контроллер (основной)
- `calls_controller_tgvoip.h` — легаси TgVoip (для совместимости)

### Групповые звонки (`calls/group/`)
- `calls_group_call.cpp` — 1800+ строк, использует `tgcalls::GroupInstanceCustomImpl`
- Три бэкенда рендера видео: OpenGL, raster, viewport

---

## Flow: MTProto → tgcalls (tdesktop использует raw MTProto, не TDLib)

```
startOutgoing()
  → MTPphone_RequestCall(gaHash, protocol)
  → handleUpdate(mtpc_phoneCallAccepted)
    → DH key exchange (MTP::CreateAuthKey)
      → MTPphone_ConfirmCall(ga, keyFingerprint, protocol)
        → response: MTPDphoneCall
          → createAndStartController(MTPDphoneCall)  ← создаётся tgcalls
```

Для входящего:
```
handleUpdate(mtpc_phoneCall)
  → startConfirmedCall(MTPDphoneCall)
    → MTP::CreateAuthKey(g_a_or_b, randomPower, dhConfig.p)
    → createAndStartController(MTPDphoneCall)
```

---

## createAndStartController — ключевая функция

```cpp
// Регистрация имплементаций (один раз при старте)
const auto Register    = tgcalls::Register<tgcalls::InstanceImpl>();
const auto RegisterV2  = tgcalls::Register<tgcalls::InstanceV2Impl>();
const auto RegV2Ref    = tgcalls::Register<tgcalls::InstanceV2ReferenceImpl>();

// Создание дескриптора
tgcalls::Descriptor descriptor = {
    .version = versionString,  // из phoneCallProtocol::library_versions[0]
    .config = tgcalls::Config{
        .initializationTimeout = serverConfig.callConnectTimeoutMs / 1000.,
        .receiveTimeout        = serverConfig.callPacketTimeoutMs / 1000.,
        .dataSaving            = tgcalls::DataSaving::Never,
        .enableP2P             = call.is_p2p_allowed(),
        .enableAEC = false, .enableNS = true, .enableAGC = true,
        .enableVolumeControl   = true,
        .maxApiLayer           = protocol.vmax_layer().v,
    },
    .encryptionKey = tgcalls::EncryptionKey(
        encryptionKeyValue,      // 256 байт DH ключа
        (_type == Type::Outgoing)  // isOutgoing — роль в DTLS handshake
    ),
    .signalingDataEmitted = [=](const std::vector<uint8_t> &data) {
        // → MTPphone_SendSignalingData(...)
    },
    .createAudioDeviceModule = Webrtc::AudioDeviceModuleCreator(...),
};

// Endpoints из call.vconnections()
const auto ids = CollectEndpointIds(call.vconnections().v);
for (const auto &connection : call.vconnections().v) {
    AppendEndpoint(descriptor.endpoints, connection);  // UDP relay
}
for (const auto &connection : call.vconnections().v) {
    AppendServer(descriptor.rtcServers, connection, ids);  // TURN/STUN
}

_instance = tgcalls::Meta::Create(versionString, std::move(descriptor));
```

---

## Маппинг MTProto → tgcalls параметры

| tgcalls параметр | MTProto источник |
|---|---|
| `encryptionKey.value` | 256-байтный DH ключ (`_authKey`) |
| `encryptionKey.isOutgoing` | `_type == Type::Outgoing` |
| `config.enableP2P` | `MTPDphoneCall::is_p2p_allowed()` |
| `config.maxApiLayer` | `phoneCallProtocol::max_layer` |
| `descriptor.version` | `phoneCallProtocol::library_versions[0]` |
| `endpoints[].endpointId` | `phoneConnection::id` |
| `endpoints[].host.ipv4/ipv6` | `phoneConnection::ip / ipv6` |
| `endpoints[].port` | `phoneConnection::port` |
| `endpoints[].peerTag` | `phoneConnection::peer_tag` (16 байт) |
| `rtcServers[].login` | `"reflector"` (фиксированный) |
| `rtcServers[].password` | hex(`phoneConnection::peer_tag`) |
| `rtcServers[].isTurn` | `true` |
| `rtcServers[].isTcp` | `phoneConnection::is_tcp()` |

Для WebRTC-соединений (`MTPDphoneConnectionWebrtc`):
- STUN: `isTurn = false`, без логина/пароля
- TURN: `isTurn = true`, `login/password` из `data.vusername()/vpassword()`

---

## Signaling data

tgcalls эмитит signaling через коллбек `signalingDataEmitted` → отправляется через `MTPphone_SendSignalingData`.  
Принимается через `updatePhoneCallSignalingData` → `_instance->receiveSignalingData(data)`.

---

## Важно для kuni (TDLib)

**TDLib абстрагирует DH-обмен** — в `callStateReady` уже есть готовый ключ:

```cpp
// td::td_api::callStateReady содержит:
state->encryption_key_   // std::string — 256 байт ключа (бинарно, не base64)
state->servers_          // vector<callServer> — TURN/STUN серверы
state->protocol_         // callProtocol — library_versions, max_layer
state->allow_p2p_        // bool
```

Т.е. DH делать не надо — TDLib уже сделал. Нужно:
1. Взять `encryption_key_` из `callStateReady`
2. Собрать `tgcalls::Descriptor` аналогично tdesktop
3. Смаппить `callServer` → `tgcalls::RtcServer` / `tgcalls::Endpoint`
4. Создать `tgcalls::Meta::Create(version, descriptor)`
5. Signaling: слушать `updateNewCallSignalingData` → `_instance->receiveSignalingData()`
6. Отправлять signaling через `td::td_api::sendCallSignalingData`

### Маппинг TDLib callServer → tgcalls

```cpp
// td::td_api::callServerTypeTelegramReflector → tgcalls::Endpoint + tgcalls::RtcServer (TURN)
// td::td_api::callServerTypeWebrtc → tgcalls::RtcServer (STUN/TURN)

for (const auto& server : state->servers_) {
    if (auto* r = dynamic_cast<td::td_api::callServerTypeTelegramReflector*>(server->type_.get())) {
        // UDP relay endpoint
        tgcalls::Endpoint ep;
        ep.endpointId = server->id_;
        ep.host = { .ipv4 = server->ip_address_, .ipv6 = server->ipv6_address_ };
        ep.port = server->port_;
        ep.type = tgcalls::EndpointType::UdpRelay;
        memcpy(ep.peerTag, r->peer_tag_.data(), 16);
        descriptor.endpoints.push_back(ep);

        // TURN server
        descriptor.rtcServers.push_back({
            .host = server->ip_address_,
            .port = (uint16_t)server->port_,
            .login = "reflector",
            .password = hex(r->peer_tag_),  // peer_tag как hex
            .isTurn = true,
        });
    } else if (auto* w = dynamic_cast<td::td_api::callServerTypeWebrtc*>(server->type_.get())) {
        if (w->supports_stun_) {
            descriptor.rtcServers.push_back({ .host = server->ip_address_, .port = (uint16_t)server->port_, .isTurn = false });
        }
        if (w->supports_turn_) {
            descriptor.rtcServers.push_back({
                .host = server->ip_address_, .port = (uint16_t)server->port_,
                .login = w->username_, .password = w->password_, .isTurn = true,
            });
        }
    }
}
```

---

## Текущее состояние в kuni

- `VoiceCallManager.cpp` — слушает `updateCall`, принимает звонки от papik
- `onCallReady()` — **пустая**, нужно реализовать
- Signaling data — слушается через `updateNewCallSignalingData`, но не передаётся в инстанс (инстанса нет)
- `KUNI_VOICE_CALLS` — compile-time флаг для включения tgcalls

### Что нужно сделать

1. Подтянуть `tgcalls` как зависимость в CMakeLists.txt (через `tg_owt` + `tgcalls`)
2. Реализовать `onCallReady` — собрать `tgcalls::Descriptor` из `callStateReady`
3. Хранить `std::unique_ptr<tgcalls::Instance>` в `VoiceCallManager`
4. Пробросить signaling data в обе стороны
5. Обработать `callStateDiscarded` — вызвать `_instance->stop()`
