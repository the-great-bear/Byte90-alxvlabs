/**
 * EventBus.h
 *
 * Declarations for EventBus.
 */

#pragma once

#include <Arduino.h>
#include <array>
#include <functional>
#include <vector>

/**
 * EventType - Supported event types for app coordination.
 */
enum class EventType : uint8_t {
    CONNECT_PROTOCOL = 0,
    START_LISTENING,
    STOP_LISTENING,
    CANCEL_OPENAI_RESPONSE,
    BUTTON_CLICK,
    BUTTON_LONG_PRESS,
    COUNT
};

/**
 * Event - Basic event payload.
 */
struct Event {
    EventType type;
    String reason;
};

/**
 * EventBus - Simple in-process pub/sub bus.
 */
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    int subscribe(EventType type, Handler handler);
    void unsubscribe(EventType type, int id);
    void publish(const Event& event);

private:
    struct HandlerEntry {
        int id;
        Handler handler;
    };

    static constexpr size_t EVENT_TYPE_COUNT = static_cast<size_t>(EventType::COUNT);
    std::array<std::vector<HandlerEntry>, EVENT_TYPE_COUNT> _handlers;
    int _next_id = 1;
};
