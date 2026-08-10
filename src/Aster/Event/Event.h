#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>

namespace Event
{
    enum MouseButton
    {
        Left,
        Mid,
        Right
    };

    struct BaseEvent
    {
        std::string className;

        BaseEvent(const std::string &name) : className(name) {}
        virtual ~BaseEvent() = default;

        using Factory = std::function<std::shared_ptr<BaseEvent>(const std::unordered_map<std::string, std::string> &)>;

        static std::unordered_map<std::string, Factory> &registry()
        {
            static std::unordered_map<std::string, Factory> impl;
            return impl;
        }
    };

    struct MouseEvent : public BaseEvent
    {
        MouseEvent(float x, float y) : BaseEvent("MouseEvent"), cursorX(x), cursorY(y) {}
        float cursorX;
        float cursorY;
    };
    struct MouseMoveEvent : public MouseEvent
    {
        MouseMoveEvent(float x, float y) : MouseEvent(x, y) {}
    };
    struct MouseButtonEvent : public MouseEvent
    {
        enum Action
        {
            Press,
            Release
        };
        MouseButton button;
        Action action;

        MouseButtonEvent(float x, float y, MouseButton _button, Action _action)
            : MouseEvent(x, y), button(_button), action(_action) {}
    };
    struct MouseScrolledEvent : public MouseEvent
    {
        double xoffset, yoffset;
        MouseScrolledEvent(double x, double y, double xoffset, double yoffset)
            : MouseEvent(x, y), xoffset(xoffset), yoffset(yoffset) {}
    };

    struct KeyPressedEvent : public BaseEvent
    {
        int key;
        KeyPressedEvent(int _key) : BaseEvent("KeyPressedEvent"), key(_key) {}
    };
    struct KeyReleasedEvent : public BaseEvent
    {
        int key;
        KeyReleasedEvent(int _key) : BaseEvent("KeyReleasedEvent"), key(_key) {}
    };

    struct DropEvent : public BaseEvent
    {
        std::vector<std::string> paths;
        DropEvent(const std::vector<std::string> &_paths)
            : BaseEvent("DropEvent"), paths(_paths) {}
    };

    // record all events generated in every frame
    static std::vector<std::shared_ptr<BaseEvent>> &events()
    {
        static std::vector<std::shared_ptr<BaseEvent>> impl;
        return impl;
    }
}