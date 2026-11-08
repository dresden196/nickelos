// NickelOS: Mozc's session handler in WebAssembly, for ChromeOS's Japanese
// input method (the nacl_mozc extension), whose engine was a Native Client
// module, which Chrome no longer runs. What NaCl's nacl_session_handler.cc
// did (google/mozc, src/chrome/nacl), for nickel-mozc.js: commands in and
// out as JSON (protobuf's mapping, proto field names), and the few events
// the extension asks for. Copyright NickelOS; BSD-3-Clause, as Mozc.

#include <emscripten/emscripten.h>

#include <memory>
#include <string>
#include <utility>

#include "base/system_util.h"
#include "base/version.h"
#include "config/config_handler.h"
#include "data_manager/oss/oss_data_manager.h"
#include "dictionary/user_dictionary_util.h"
#include "engine/engine.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/util/json_util.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/user_dictionary_storage.pb.h"
#include "session/session_handler.h"

namespace {

std::unique_ptr<mozc::SessionHandler> g_handler;
std::string g_reply;

std::string ToJson(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  std::string json;
  if (!google::protobuf::util::MessageToJsonString(message, &json, options)
           .ok()) {
    return "{}";
  }
  return json;
}

std::string Quote(const std::string& text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char buffer[8];
      snprintf(buffer, sizeof(buffer), "\\u%04x", c);
      out += buffer;
    } else {
      out += c;
    }
  }
  return out + "\"";
}

}  // namespace

extern "C" {

// Starts the engine with its user files (history, user dictionary, config)
// in |profile_dir|; returns the InitializeDone event the extension waits
// for.
EMSCRIPTEN_KEEPALIVE const char* mozc_init(const char* profile_dir) {
  mozc::SystemUtil::SetUserProfileDirectory(profile_dir);
  absl::StatusOr<std::unique_ptr<mozc::Engine>> engine =
      mozc::Engine::CreateEngine(std::make_unique<mozc::oss::OssDataManager>());
  if (!engine.ok()) {
    g_reply = "{\"event\":{\"type\":\"InitializeFailed\"}}";
    return g_reply.c_str();
  }
  g_handler = std::make_unique<mozc::SessionHandler>(std::move(*engine));
  const std::shared_ptr<const mozc::config::Config> config =
      mozc::config::ConfigHandler::GetSharedConfig();
  g_reply = "{\"event\":{\"type\":\"InitializeDone\",\"config\":" +
            ToJson(*config) + ",\"version\":" +
            Quote(mozc::Version::GetMozcVersion()) + "}}";
  return g_reply.c_str();
}

// One mozc::commands::Command (its input), evaluated; its output back.
EMSCRIPTEN_KEEPALIVE const char* mozc_eval(const char* command_json) {
  mozc::commands::Command command;
  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = true;
  if (!g_handler ||
      !google::protobuf::util::JsonStringToMessage(command_json, &command,
                                                   options)
           .ok()) {
    g_reply = "{}";
    return g_reply.c_str();
  }
  g_handler->EvalCommand(&command);
  g_reply = ToJson(command);
  return g_reply.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* mozc_versions() {
  g_reply = "{\"version\":" + Quote(mozc::Version::GetMozcVersion()) +
            ",\"data_version\":" +
            Quote(g_handler ? std::string(g_handler->engine().GetDataVersion())
                            : std::string()) +
            "}";
  return g_reply.c_str();
}

EMSCRIPTEN_KEEPALIVE int mozc_is_valid_reading(const char* reading) {
  return mozc::user_dictionary::IsValidReading(reading) ? 1 : 0;
}

// The user dictionary's parts of speech, for the options page:
// [{"type": NAME, "name": NAME}], from the PosType enum.
EMSCRIPTEN_KEEPALIVE const char* mozc_pos_list() {
  const google::protobuf::EnumDescriptor* pos_types =
      mozc::user_dictionary::UserDictionary::PosType_descriptor();
  g_reply = "[";
  for (int i = 0; i < pos_types->value_count(); ++i) {
    const std::string name(pos_types->value(i)->name());
    g_reply += (i ? ",{\"type\":" : "{\"type\":") + Quote(name) +
               ",\"name\":" + Quote(name) + "}";
  }
  g_reply += "]";
  return g_reply.c_str();
}

}  // extern "C"
