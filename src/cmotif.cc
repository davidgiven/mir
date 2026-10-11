#include "include/cmotif.h"

static int callbackId = 0;
static std::set<Widget> registeredWidgets;
static std::map<std::pair<Widget, int>, std::function<void(XtPointer)>>
    callbacks;
static std::map<std::pair<Widget, int>, std::function<void(XtPointer, XEvent*, Boolean*)>>
    eventHandlers;

void CWidgetImplBase::executeCallback(
    Widget w, XtPointer client_data, XtPointer call_data)
{
    auto cbid = reinterpret_cast<intptr_t>(client_data);
    auto it = callbacks.find({w, cbid});
    if (it != callbacks.end())
    {
        it->second(call_data);
    }
}

void CWidgetImplBase::executeEventHandler(
    Widget w, XtPointer client_data, XEvent* event, Boolean* continue_to_dispatch)
{
    auto cbid = reinterpret_cast<intptr_t>(client_data);
    auto it = eventHandlers.find({w, cbid});
    if (it != eventHandlers.end())
    {
        it->second(nullptr, event, continue_to_dispatch);
    }
}

int CWidgetImplBase::registerCallback(
    Widget widget, std::function<void(XtPointer)> callback)
{
    if (registeredWidgets.find(widget) == registeredWidgets.end())
    {
        registeredWidgets.insert(widget);
        XtAddCallback(widget, XmNdestroyCallback, deregisterCallbacks, nullptr);
    }
    int callbackId = callbackId++;
    callbacks[{widget, callbackId}] = std::move(callback);
    return callbackId;
}

int CWidgetImplBase::registerEventHandler(
    Widget widget, std::function<void(XtPointer, XEvent*, Boolean*)> handler)
{
    if (registeredWidgets.find(widget) == registeredWidgets.end())
    {
        registeredWidgets.insert(widget);
        XtAddCallback(widget, XmNdestroyCallback, deregisterCallbacks, nullptr);
    }
    int handlerId = callbackId++;
    eventHandlers[{widget, handlerId}] = std::move(handler);
    return handlerId;
}

void CWidgetImplBase::deregisterCallbacks(
    Widget widget, XtPointer client_data, XtPointer call_data)
{
    std::erase_if(callbacks,
        [widget](const auto& pair)
        {
            return pair.first.first == widget;
        });
    std::erase_if(eventHandlers,
        [widget](const auto& pair)
        {
            return pair.first.first == widget;
        });
    registeredWidgets.erase(widget);
}
