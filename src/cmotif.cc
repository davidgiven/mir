#include "include/cmotif.h"

static int callbackId = 0;
static std::set<Widget> registeredWidgets;
static std::map<std::pair<Widget, int>, CWidgetImplBase::RegisteredCallback> callbacks;

void CWidgetImplBase::executeCallback(
    Widget w, XtPointer client_data, XtPointer call_data)
{
    auto cbid = reinterpret_cast<intptr_t>(client_data);
    auto it = callbacks.find({w, cbid});
    if (it != callbacks.end())
    {
        if (auto callback = std::get_if<Callback>(&it->second))
            (*callback)(call_data);
    }
}

void CWidgetImplBase::executeEventHandler(
    Widget w, XtPointer client_data, XEvent* event, Boolean* continue_to_dispatch)
{
    auto cbid = reinterpret_cast<intptr_t>(client_data);
    auto it = callbacks.find({w, cbid});
    if (it != callbacks.end())
    {
        if (auto handler = std::get_if<EventHandler>(&it->second))
            (*handler)(event, continue_to_dispatch);
    }
}

int CWidgetImplBase::registerCallback(
    Widget widget, RegisteredCallback callback)
{
    if (registeredWidgets.find(widget) == registeredWidgets.end())
    {
        registeredWidgets.insert(widget);
        XtAddCallback(widget, XmNdestroyCallback, deregisterCallbacks, nullptr);
    }
    int cbid = callbackId++;
    callbacks[{widget, cbid}] = std::move(callback);
    return cbid;
}

void CWidgetImplBase::deregisterCallbacks(
    Widget widget, XtPointer client_data, XtPointer call_data)
{
    std::erase_if(callbacks,
        [widget](const auto& pair)
        {
            return pair.first.first == widget;
        });
    registeredWidgets.erase(widget);
}
