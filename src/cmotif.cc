#include "include/cmotif.h"

int CWidgetImplBase::_callbackId = 0;
std::set<Widget> CWidgetImplBase::_registeredWidgets;
std::map<std::pair<Widget, int>, std::function<void(XtPointer)>>
    CWidgetImplBase::_callbacks;

void CWidgetImplBase::executeCallback(
    Widget w, XtPointer client_data, XtPointer call_data)
{
    auto cbid = reinterpret_cast<intptr_t>(client_data);
    auto it = _callbacks.find({w, cbid});
    if (it != _callbacks.end())
    {
        it->second(call_data);
    }
}

int CWidgetImplBase::registerCallback(
    Widget widget, std::function<void(XtPointer)> callback)
{
    if (_registeredWidgets.find(widget) == _registeredWidgets.end())
    {
        _registeredWidgets.insert(widget);
        XtAddCallback(widget, XmNdestroyCallback, deregisterCallbacks, nullptr);
    }
    int callbackId = _callbackId++;
    _callbacks[{widget, callbackId}] = std::move(callback);
    return callbackId;
}

void CWidgetImplBase::deregisterCallbacks(
    Widget widget, XtPointer client_data, XtPointer call_data)
{
    std::erase_if(_callbacks,
        [widget](const auto& pair)
        {
            return pair.first.first == widget;
        });
    _registeredWidgets.erase(widget);
}
