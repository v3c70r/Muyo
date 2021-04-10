#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <vector>

template<typename... Args>
class EventHandler {
public:
    typedef std::function<void(Args...)> HandlerFunc;
    typedef size_t HandlerId;

    explicit EventHandler(const HandlerFunc& func)
        : m_HandlerFunc(func)
    {
        m_Id = ++s_IdCounter;
    }

    EventHandler(const EventHandler& handler) :
        m_Id(handler.m_Id), m_HandlerFunc(handler.m_HandlerFunc)
    {}

    EventHandler(EventHandler&& handler)
        : m_Id(handler.m_Id), m_HandlerFunc(handler.m_HandlerFunc)
    {}

    EventHandler& operator=(EventHandler& handler)
    {
        m_HandlerFunc = handler.m_HandlerFunc;
        m_Id = handler.m_Id;
        return *this;
    }

    EventHandler& operator=(EventHandler&& handler)
    {
        m_HandlerFunc = handler.m_HandlerFunc;
        m_Id = handler.m_Id;
        return *this;
    }

    void operator()(Args... params)
    {
        if (m_HandlerFunc) {
            m_HandlerFunc(params...);
        }
    }

    bool operator==(const EventHandler<Args...> handler)
    {
        return m_Id == handler.id();
    }

    HandlerId id() const { return m_Id; }

private:
    HandlerId m_Id;
    HandlerFunc m_HandlerFunc;
    static inline std::atomic<HandlerId> s_IdCounter = 0;
};

class EventBase {
protected:
    EventBase(size_t type) : m_Type(type) {}
    size_t m_Type;
};

//we will have subclass inherit on this
template<typename ...Args>
class Event : public EventBase {
    typedef EventHandler<Args...> HandlerType;
public:
    Event(size_t type) : EventBase(type) {}

    void emit(Args ... args)
    {
        for (HandlerType& handler : m_Collections) {
            handler(args...);
        }
    }
    //adding a
    int watch(const HandlerType& handler)
    {
        std::lock_guard<std::mutex> lock(m_handlersLock);

        m_Collections.push_back(handler);
        return handler.id();
    }

    //need to use typename to access the dependent type
    inline typename HandlerType::HandlerId watch(const typename HandlerType::HandlerFunc& handler)
    {
        return watch(HandlerType(handler));
    }

    bool unwatch(HandlerType& handler)
    {
        std::lock_guard<std::mutex> lock(m_handlersLock);

        auto it = std::find(m_Collections.begin(), m_Collections.end(), handler);
        if (it != m_Collections.end()) {
            m_Collections.erase(it);
            return true;
        }
        return false;
    }

    //again, using typename to access the dependent typename
    bool unwatch(typename HandlerType::HandlerId id)
    {
        std::lock_guard<std::mutex> lock(m_handlersLock);

        auto it = std::find(m_Collections.begin(), m_Collections.end(),
                            [id](const HandlerType& it) {
                                return it->id() == id;
                            });
        if (it != m_Collections.end()) {
            m_Collections.erase(it);
            return true;
        }
        return false;
    }

protected:
    std::list<HandlerType> m_Collections;
    mutable std::mutex m_handlersLock;
};
