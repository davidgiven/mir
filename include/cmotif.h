#pragma once
#include <array>
#include <concepts>
#include <tuple>
#include <utility>
#include <variant>
#include <memory>
#include <Xm/Xm.h>
#include "Xm/VendorS.h"
#include "Xm/ArrowBG.h"
#include "Xm/ArrowB.h"
#include "Xm/BulletinB.h"
#include "Xm/ButtonBox.h"
#include "Xm/CascadeBG.h"
#include "Xm/CascadeB.h"
#include "Xm/ColorS.h"
#include "Xm/Column.h"
#include "Xm/ComboBox.h"
#include "Xm/Command.h"
#include "Xm/Container.h"
#include "Xm/DialogS.h"
#include "Xm/Display.h"
#include "Xm/Display.h"
#include "Xm/DragC.h"
#include "Xm/DragOverS.h"
#include "Xm/DrawingA.h"
#include "Xm/DrawnB.h"
#include "Xm/DropDown.h"
#include "Xm/DropSMgr.h"
#include "Xm/DropTrans.h"
#include "Xm/FileSB.h"
#include "Xm/Form.h"
#include "Xm/Frame.h"
#include "Xm/Gadget.h"
#include "Xm/GrabShell.h"
#include "Xm/Hierarchy.h"
#include "Xm/IconBox.h"
#include "Xm/IconButton.h"
#include "Xm/IconG.h"
#include "Xm/IconH.h"
#include "Xm/LabelG.h"
#include "Xm/Label.h"
#include "Xm/List.h"
#include "Xm/MainW.h"
#include "Xm/Manager.h"
#include "Xm/MenuShell.h"
#include "Xm/MessageB.h"
#include "Xm/MultiList.h"
#include "Xm/Notebook.h"
#include "Xm/Outline.h"
#include "Xm/Primitive.h"
#include "Xm/Protocols.h"
#include "Xm/PushBG.h"
#include "Xm/PushB.h"
#include "Xm/RowColumn.h"
#include "Xm/Scale.h"
#include "Xm/Screen.h"
#include "Xm/Screen.h"
#include "Xm/ScrollBar.h"
#include "Xm/ScrolledW.h"
#include "Xm/SelectioB.h"
#include "Xm/SeparatoG.h"
#include "Xm/Separator.h"
#include "Xm/SSpinB.h"
#include "Xm/SlideC.h"
#include "Xm/SpinB.h"
#include "Xm/TabBox.h"
#include "Xm/TabBox.h"
#include "Xm/TabStack.h"
#include "Xm/Text.h"
#include "Xm/ToggleBG.h"
#include "Xm/ToggleB.h"
#include "Xm/Tree.h"
#include <functional>
#include <set>
#include <cstdint>
#include <map>

extern WidgetClass xmWorldClass;
extern WidgetClass xmTearOffButtonWidgetClass;
extern WidgetClass xmShellExtObjectClass;
extern WidgetClass xmClipWindowWidgetClass;
extern WidgetClass xmProtocolObjectClass;
extern WidgetClass xmDesktopClass;
extern WidgetClass xmTabCanvasWidgetClass;
extern WidgetClass xmI18ListWidgetClass;
extern WidgetClass xmDialogShellExtObjectClass;
extern WidgetClass xmExtObjectClass;
extern WidgetClass xmColorObjClass;

struct XmIconGCacheObjPart;
struct XmLabelGCacheObjPart;

typedef void (*InputCreateProc)(Widget, ArgList, Cardinal);
typedef void (*OutputCreateProc)(Widget, ArgList, Cardinal);

typedef union _XmConvertSelectionRec
{
    XtConvertSelectionIncrProc sel_incr;
    XtConvertSelectionProc sel;
} XmConvertSelectionRec;

class CXmString
{
public:
    CXmString(const char* str):
        _xmstring(XmStringCreateLocalized((char*)str))
    {
    }

    CXmString(const std::string& str):
        _xmstring(XmStringCreateLocalized((char*)str.c_str()))
    {
    }

    ~CXmString() { XmStringFree(_xmstring); }

    CXmString(const CXmString&) = delete;
    CXmString& operator=(const CXmString&) = delete;

    CXmString(CXmString&& other) noexcept:
        _xmstring(std::exchange(other._xmstring, nullptr)) {}

    CXmString& operator=(CXmString&& other) noexcept
    {
        if (this != &other) {
            XmStringFree(_xmstring);
            _xmstring = std::exchange(other._xmstring, nullptr);
        }
        return *this;
    }

    operator XmString() const
    {
        return _xmstring;
    }

private:
    XmString _xmstring;
};

template <template <typename...> class Derived,
    typename CWidgetClassT,
    typename Parent,
    typename... Args>
class CWidgetBuilderBase;

class CWidget;

class CWidgetImplBase
{
public:
    using Callback = std::function<void(XtPointer)>;
    using EventHandler = std::function<void(XEvent*, Boolean*)>;
    using RegisteredCallback = std::variant<Callback, EventHandler>;

    template <typename Builder>
    class ConstraintsBuilder
    {
    public:
        explicit ConstraintsBuilder(Builder&) {}
        template <template <typename...> class Derived,
            typename CWidgetClassT,
            typename Parent,
            typename... Args>
        explicit ConstraintsBuilder(
            CWidgetBuilderBase<Derived, CWidgetClassT, Parent, Args...>&)
        {
        }
    };

    CWidgetImplBase(Widget w): _widget(w) {}

    operator Widget() const
    {
        return _widget;
    }

    CWidget findWidgetByName(const char* name);

protected:
    static void executeCallback(
        Widget w, XtPointer client_data, XtPointer call_data);
    static void executeEventHandler(Widget w,
        XtPointer client_data,
        XEvent* event,
        Boolean* continue_to_dispatch);
    static int registerCallback(
        Widget widget, RegisteredCallback callback);
    static void deregisterCallbacks(
        Widget widget, XtPointer client_data, XtPointer call_data);

public:
    Widget _widget;
};

template <typename Derived>
class CWidgetImpl : public CWidgetImplBase
{
public:
    using CWidgetImplBase::CWidgetImplBase;

    template <typename T>
    Derived& set(const char* resourceName, T value)
    {
        XtVaSetValues(_widget, resourceName, value, nullptr);
        return static_cast<Derived&>(*this);
    }

    template <typename T>
    T get(const char* resourceName){
        T value;
        XtVaGetValues(_widget, resourceName, &value, nullptr);
        return value;
    }

    Derived& manageChild()
    {
        XtManageChild(_widget);
        return static_cast<Derived&>(*this);
    }

    Derived& unmanageChild()
    {
        XtUnmanageChild(_widget);
        return static_cast<Derived&>(*this);
    }
    Derived& realize()
    {
        XtRealizeWidget(_widget);
        return static_cast<Derived&>(*this);
    }

    Derived& addCallback(
        const char* callback_name, std::function<void(XtPointer)> callback)
    {
        int cbid = registerCallback(_widget, std::move(callback));
        XtAddCallback(_widget,
            callback_name,
            executeCallback,
            reinterpret_cast<XtPointer>(cbid));

        return static_cast<Derived&>(*this);
    }

    Derived& addEventHandler(
        std::function<void(XEvent*, Boolean*)> handler,
        EventMask event_mask)
    {
        int cbid = registerCallback(_widget, std::move(handler));
        XtAddEventHandler(_widget,
            event_mask,
            false,
            executeEventHandler,
            reinterpret_cast<XtPointer>(cbid));
        return static_cast<Derived&>(*this);
    }

};

class CWidget : public CWidgetImpl<CWidget>
{
public:
    using CWidgetImpl<CWidget>::CWidgetImpl;
};

inline CWidget CWidgetImplBase::findWidgetByName(const char* name)
{
    return CWidget(XtNameToWidget(_widget, name));
}

template <template <typename...> class Derived,
    typename CWidgetClassT,
    typename Parent,
    typename... Args>
class CWidgetBuilderBase
{
public:
    template <std::same_as<Parent> ParentT>
    CWidgetBuilderBase(const ParentT& parent, const char* name):
        _parent(parent),
        _name(name)
    {
    }

    // Public so derived builders can construct the next specialization.
    template <std::same_as<Parent> ParentT>
    explicit CWidgetBuilderBase(
        const ParentT& parent, const char* name, std::tuple<Args...> args):
        _parent(parent),
        _name(name),
        _pairs(std::move(args))
    {
    }

    // Shared helper: appends one key/value pair and returns the next Derived
    // type.
    template <typename K, typename V>
    Derived<Parent, Args..., K, V> set(K key, V value)
    {
        return Derived<Parent, Args..., K, V>(_parent, _name,
            std::tuple_cat(std::move(_pairs), std::make_tuple(key, std::move(value))));
    }

    // Turns the tuple of key/value pairs into an ArgList.
    auto buildArgList() const
    {
        static_assert(sizeof...(Args) % 2 == 0,
            "builder arguments must be resource/value pairs");
        return [this]<std::size_t... I>(std::index_sequence<I...>)
        {
            std::array<Arg, sizeof...(I)> arglist{};
            auto argValue = [](const auto& value) {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, std::unique_ptr<CXmString>>)
                    return static_cast<XmString>(*value);
                else
                    return value;
            };
            (XtSetArg(arglist[I],
                 const_cast<char*>(std::get<2 * I>(_pairs)),
                 argValue(std::get<2 * I + 1>(_pairs))),
                ...);
            return arglist;
        }(std::make_index_sequence<sizeof...(Args) / 2>{});
    }

public:
    Parent _parent;
    const char* _name;
    std::tuple<Args...> _pairs;
};


#include "cmotif_impl.h"
