#include <Xm/Xm.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <tuple>
#include <utility>
inline constexpr const char* FOO = "foo";
inline constexpr const char* BAR = "bar";

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

protected:
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
class XmPushButtonResources : public ResourceValuesBase<XmPushButtonResources, Pairs...>
{
public:
    XmPushButtonResources() = default;
    using ResourceValuesBase<XmPushButtonResources, Pairs...>::ResourceValuesBase;

    auto withLabelString(XmString value) const
    {
        return this->with(XmNlabelString, value);
    }

    auto withLabelString(const char* value) const
    {
        return this->with(
            XmNlabelString, XmStringCreateSimple((char*)value));
    }

    auto withShadowThickness(int value) const
    {
        return this->with(XmNshadowThickness, value);
    }

    auto withShowAsDefault(bool value) const
    {
        return this->with(XmNshowAsDefault, value);
    }

    auto withBar(const char* value) const
    {
        return this->with(BAR, value);
    }
};

template <typename... Pairs>
static Widget XmCreatePushButton(const char* name,
    Widget parent,
    const XmPushButtonResources<Pairs...>& resources)
{
    return std::apply(
        [&](const auto&... args)
        {
            return XmVaCreatePushButton(
                parent, (char*)name, args..., nullptr);
        },
        resources._pairs);
}

int main(int argc, char* argv[])
{
    XtAppContext app;
    Widget toplevel, button1,button2;

    toplevel =
        XtVaAppInitialize(&app, "HelloWorld", NULL, 0, &argc, argv, NULL, NULL);

    Widget mainWindow = XtVaCreateManagedWidget(
        "mainWindow", xmRowColumnWidgetClass, toplevel, NULL);

    button1 = XmCreatePushButton("button",
        mainWindow,
        XmPushButtonResources()
            .withLabelString("Hello, world!")
            .withShowAsDefault(true));
    XtManageChild(button1);

    button2 = XmCreatePushButton("button2",
        mainWindow,
        XmPushButtonResources()
            .withLabelString("Button 2"));
    XtManageChild(button2);

    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);
}
