#pragma once

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <memory>
#include <map>

#include "Event.h"
#include <interface/key.h>
#include <interface/mouse.h>

enum EventType {
    Key = 0,
    Char,
    MouseButton,
    MouseMotion,
    MouseWheel,
    WindowResize,
    CursorSet,
    Count,
};

enum EventState {
    Pressed = true,
    Released = false,
};

template<EventType ET, typename... Args>
class GlobalEvent : public Event<Args...> {
public:
    GlobalEvent<ET, Args...>() : Event<Args...>(ET) {}
    inline EventType name() const { return ET; }
};

//timestamp, key, modifier and state
typedef GlobalEvent<EventType::Key, uint32_t, Input::Key, uint16_t, EventState>
    GlobalKeyEvent;

typedef GlobalEvent<EventType::Char, uint32_t, unsigned int>
    GlobalCharEvent;

typedef GlobalEvent<EventType::MouseButton, uint32_t, Input::Button, EventState>
    GlobalButtonEvent;

typedef GlobalEvent<EventType::MouseMotion, uint32_t, float, float>
    GlobalMotionEvent;

typedef GlobalEvent<EventType::MouseWheel, uint32_t, float, float>
    GlobalWheelEvent;

typedef GlobalEvent<EventType::WindowResize, uint32_t, size_t, size_t>
    GlobalResizeEvent;

typedef GlobalEvent<EventType::CursorSet, uint32_t, Input::Cursor>
    GlobalCursorSetEvent;

class EventSystem {
public:
    static EventSystem* sys()
    {
        static EventSystem s_EventSystem;
        return &s_EventSystem;
    }

    //TODO This is not ideal, if we can directly pass GlobalEvent as template
    //then we can avoid ET
    template<EventType ET, typename T>
    std::shared_ptr<T> globalEvent()
    {
        std::shared_ptr<T> event = m_Events.find(ET) != m_Events.end() ?
            std::static_pointer_cast<T>(m_Events.at(ET)) : nullptr;

        if (event == nullptr)
            event = std::make_shared<T>();
        m_Events[ET] = event;
        return event;
    }

    //TODO destructor

private:
    std::map<EventType, std::shared_ptr<EventBase>> m_Events;
};
