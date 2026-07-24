#pragma once
#include <Arduino.h>

namespace OfflineQueue {
enum class Destination : uint8_t { AfmsWeb, GoogleSheets };

bool begin();
bool enqueue(Destination destination, const String &eventId, const String &payload);
bool peek(Destination destination, String &eventId, String &payload);
bool removeHead(Destination destination);
uint16_t count(Destination destination);
uint32_t droppedCount(Destination destination);
void clear(Destination destination);
}
