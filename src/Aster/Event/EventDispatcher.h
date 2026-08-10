#pragma once
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include "Event.h"

namespace Event
{
    class EventDispatcher
    {
    public:
        static EventDispatcher &Instance()
        {
            static EventDispatcher instance;
            return instance;
        }
        EventDispatcher(const EventDispatcher &) = delete;
        EventDispatcher &operator=(const EventDispatcher &) = delete;

        // 成员函数注册
        template <typename EventType, typename ClassType>
        void RegisterHandler(const std::shared_ptr<ClassType> &instance, void (ClassType::*method)(const std::shared_ptr<EventType> &))
        {
            std::weak_ptr<ClassType> weakInstance = instance;
            auto handler = [weakInstance, method](const std::shared_ptr<BaseEvent> &e)
            {
                if (auto sharedInstance = weakInstance.lock())
                {
                    auto event = std::dynamic_pointer_cast<EventType>(e);
                    if (event)
                    {
                        (sharedInstance.get()->*method)(event);
                    }
                }
            };

            eventHandlers[typeid(EventType)].push_back(handler);
        }

        // 成员函数注册
        template <typename EventType, typename ClassType>
        void RegisterHandler(ClassType *instance, void (ClassType::*method)(const std::shared_ptr<EventType> &))
        {
            auto handler = [instance, method](const std::shared_ptr<BaseEvent> &e)
            {
                if (!instance)
                    return; // 安全检查

                auto event = std::dynamic_pointer_cast<EventType>(e);
                if (event)
                {
                    (instance->*method)(event);
                }
            };

            eventHandlers[typeid(EventType)].push_back(handler);
        }

        // 常规函数或静态方法注册
        template <typename EventType>
        void RegisterHandler(std::function<void(const std::shared_ptr<EventType> &)> func)
        {
            auto handler = [func](const std::shared_ptr<BaseEvent> &e)
            {
                auto event = std::dynamic_pointer_cast<EventType>(e);
                if (event)
                {
                    func(event);
                }
            };

            eventHandlers[typeid(EventType)].push_back(handler);
        }

        // Lambda 表达式注册
        template <typename EventType>
        void RegisterHandler(std::function<void(const std::shared_ptr<EventType> &)> &&func)
        {
            RegisterHandler<EventType>(func);
        }

        void Dispatch(const std::shared_ptr<BaseEvent> &event)
        {
            if (!event)
                return;
            const BaseEvent &ev = *event; // 先解引用，避免 typeid 作用于带副作用的表达式
            auto it = eventHandlers.find(typeid(ev));
            if (it != eventHandlers.end())
            {
                auto &handlers = it->second;
                handlers.erase(
                    std::remove_if(handlers.begin(), handlers.end(),
                                   [](const EventHandler &handler)
                                   { return handler == nullptr; }),
                    handlers.end());
                for (auto &handler : handlers)
                {
                    handler(event);
                }
            }
        }

        // 清理特定类型的事件处理器
        template <typename EventType>
        void ClearHandlers()
        {
            eventHandlers.erase(typeid(EventType));
        }

        void ClearHandlers()
        {
            eventHandlers.clear();
        }

    private:
        EventDispatcher() = default;

        using EventHandler = std::function<void(const std::shared_ptr<BaseEvent> &)>;
        std::unordered_map<std::type_index, std::vector<EventHandler>> eventHandlers;
    };
}