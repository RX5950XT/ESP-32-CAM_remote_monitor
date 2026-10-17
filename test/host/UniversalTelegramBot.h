#pragma once
#include "WiFiClientSecure.h"
#include <vector>
struct telegramMessage { String chat_id,text; };
inline std::vector<telegramMessage> telegramPending;
inline std::vector<String> telegramReplies;
class UniversalTelegramBot {
public:
    int longPoll=0, last_message_received=0;
    std::vector<telegramMessage> messages;
    UniversalTelegramBot(const char*,WiFiClientSecure&) {}
    int getUpdates(int) { messages=telegramPending; telegramPending.clear(); last_message_received+=messages.size(); return messages.size(); }
    bool sendMessage(const String&,const String& message,const String&) { telegramReplies.push_back(message); return telegramMessageSuccess; }
};
