/**
 * EventBus.cpp
 *
 * Implementation for EventBus.
 */

#include "EventBus.h"

int EventBus::subscribe(EventType type, Handler handler) {
    const size_t index = static_cast<size_t>(type);
    const int id = _next_id++;
    _handlers[index].push_back({id, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(EventType type, int id) {
    const size_t index = static_cast<size_t>(type);
    auto& list = _handlers[index];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->id == id) {
            list.erase(it);
            return;
        }
    }
}

void EventBus::publish(const Event& event) {
    const size_t index = static_cast<size_t>(event.type);
    const auto handlers = _handlers[index];
    for (const auto& entry : handlers) {
        if (entry.handler) {
            entry.handler(event);
        }
    }
}
