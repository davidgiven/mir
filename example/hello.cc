#include <Xm/Xm.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <tuple>
#include <utility>
#include "include/cmotif.h"

template <template <typename...> class Chain, typename... Args>
class ResourceValuesBase
{
public:
    ResourceValuesBase() = default;

    // Public so derived classes can construct a Chain from a tuple.
    explicit ResourceValuesBase(std::tuple<Args...> args):
        _pairs(std::move(args))
    {
    }

    // Shared helper: appends one key/value pair and returns the next Chain
    // type.
    template <typename K, typename V>
    Chain<Args..., K, V> with(K key, V value) const
    {
        return Chain<Args..., K, V>(
            std::tuple_cat(_pairs, std::make_tuple(key, value)));
    }

public:
    std::tuple<Args...> _pairs;
};

template <typename... Pairs>
class XmPushButtonResources :
    public ResourceValuesBase<XmPushButtonResources, Pairs...>
{
public:
    XmPushButtonResources() = default;
    using ResourceValuesBase<XmPushButtonResources,
        Pairs...>::ResourceValuesBase;

    auto withLabelString(XmString value) const
    {
        return this->with(XmNlabelString, value);
    }

    auto withLabelString(const char* value) const
    {
        return this->with(
            XmNlabelString, XmStringCreateLocalized((char*)value));
    }

    auto withShadowThickness(int value) const
    {
        return this->with(XmNshadowThickness, value);
    }

    auto withShowAsDefault(bool value) const
    {
        return this->with(XmNshowAsDefault, value);
    }

    auto withActivateCallback(XtCallbackProc value, XtPointer client_data) const
    {
        XtCallbackRec* callback_list =
            (XtCallbackRec*)XtCalloc(2, sizeof(XtCallbackRec));
        callback_list[0].callback = value;
        callback_list[0].closure = client_data;
        callback_list[1].callback = nullptr;
        callback_list[1].closure = nullptr;
        return this->with(XmNactivateCallback, callback_list);
    }
};

template <template <typename...> class Resources>
class WrappedWidget
{
public:
    WrappedWidget(Widget w): _widget(w) {}

    operator Widget() const
    {
        return _widget;
    }

    template <typename... Pairs>
    WrappedWidget<Resources> setValues(const Resources<Pairs...>& resources)
    {
        std::apply(
            [&](const auto&... args)
            {
                XtVaSetValues(_widget, args..., nullptr);
            },
            resources._pairs);
        return *this;
    }

private:
    Widget _widget;
};

template <typename... Pairs>
static WrappedWidget<XmPushButtonResources> XmCreatePushButton(const char* name,
    Widget parent,
    const XmPushButtonResources<Pairs...>& resources)
{
    return std::apply(
        [&](const auto&... args)
        {
            return WrappedWidget<XmPushButtonResources>(
                XmVaCreatePushButton(parent, (char*)name, args..., nullptr));
        },
        resources._pairs);
}

template <typename Type>
class CallbacksBase
{
public:
    CallbacksBase(Widget w): _widget(w) {}

protected:
    Widget _widget;
};

class XmPushButtonCallbacks : public CallbacksBase<XmPushButtonCallbacks>
{
public:
    using CallbacksBase<XmPushButtonCallbacks>::CallbacksBase;

    auto withActivateCallback(XtCallbackProc value, XtPointer client_data) const
    {
        XtAddCallback(_widget, XmNactivateCallback, value, client_data);
    }
};

int main(int argc, char* argv[])
{
    XtAppContext app;

    auto toplevel =
        XtVaAppInitialize(&app, "HelloWorld", NULL, 0, &argc, argv, NULL, NULL);

    Widget mainWindow = XtVaCreateManagedWidget(
        "mainWindow", xmRowColumnWidgetClass, toplevel, NULL);

    Widget button1 = XmCreatePushButton("button",
        mainWindow,
        XmPushButtonResources()
            .withLabelString("Hello, world!")
            .withShowAsDefault(true));
    XtManageChild(button1);

    auto button2 = XmCreatePushButton("button2",
        mainWindow,
        XmPushButtonResources().withLabelString("Button 2"));
    XmPushButtonCallbacks(button2).withActivateCallback(
        [](Widget w, XtPointer client_data, XtPointer call_data)
        {
            printf("Button 2 pressed\n");
        },
        nullptr);
    XtManageChild(button2);
    button2.setValues(
        XmPushButtonResources().withLabelString("Updated Button 2"));
    XtManageChild(button2);

    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);
}
