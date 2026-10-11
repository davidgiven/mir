#pragma once
#include <array>
#include <tuple>
#include <utility>
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

class CWidgetImplBase
{
public:
    CWidgetImplBase(Widget w): _widget(w) {}

    operator Widget() const
    {
        return _widget;
    }

protected:
    static void executeCallback(
        Widget w, XtPointer client_data, XtPointer call_data);
    static void executeEventHandler(Widget w,
        XtPointer client_data,
        XEvent* event,
        Boolean* continue_to_dispatch);
    static int registerCallback(
        Widget widget, std::function<void(XtPointer)> callback);
    static int registerEventHandler(Widget widget,
        std::function<void(XtPointer, XEvent*, Boolean*)> handler);
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

    Derived& manageChild()
    {
        XtManageChild(_widget);
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
        std::function<void(XtPointer, XEvent*, Boolean*)> handler,
        EventMask event_mask)
    {
        int cbid = registerEventHandler(_widget, std::move(handler));
        XtAddEventHandler(_widget,
            event_mask,
            false,
            executeEventHandler,
            reinterpret_cast<XtPointer>(cbid));
        return static_cast<Derived&>(*this);
    }
};

template <template <typename, WidgetClass*, typename...> class Chain,
    typename CWidgetClassT,
    WidgetClass* WidgetClassP,
    typename... Args>
class CWidgetBuilderBase
{
public:
    CWidgetBuilderBase(Widget parent, const char* name):
        _parent(parent),
        _name(name)
    {
    }

    // Public so derived classes can construct a Chain from a tuple.
    explicit CWidgetBuilderBase(
        Widget parent, const char* name, std::tuple<Args...> args):
        _parent(parent),
        _name(name),
        _pairs(std::move(args))
    {
    }

    // Shared helper: appends one key/value pair and returns the next Chain
    // type.
    template <typename K, typename V>
    Chain<CWidgetClassT, WidgetClassP, Args..., K, V> set(K key, V value) const
    {
        return Chain<CWidgetClassT, WidgetClassP, Args..., K, V>(_parent,
            _name,
            std::tuple_cat(_pairs, std::make_tuple(key, value)));
    }

    // Turns the tuple of key/value pairs into an ArgList.
    auto buildArgList() const
    {
        static_assert(sizeof...(Args) % 2 == 0,
            "builder arguments must be resource/value pairs");
        return [this]<std::size_t... I>(std::index_sequence<I...>)
        {
            std::array<Arg, sizeof...(I)> arglist{};
            (XtSetArg(arglist[I], const_cast<char*>(std::get<2 * I>(_pairs)),
                std::get<2 * I + 1>(_pairs)), ...);
            return arglist;
        }(std::make_index_sequence<sizeof...(Args) / 2>{});
    }

    CWidgetClassT build()
    {
        auto arglist = buildArgList();
        return CWidgetClassT(XtCreateWidget(this->_name,
            *WidgetClassP, this->_parent, arglist.data(),
            static_cast<Cardinal>(arglist.size())));
    }

public:
    Widget _parent;
    const char* _name;
    std::tuple<Args...> _pairs;
};

#include "cmotif_impl.h"

class CXmMenuBarBuilder : public CXmRowColumnBuilder
{
public:
    using CXmRowColumnBuilder::CXmRowColumnBuilder;

    CXmRowColumn build()
    {
        auto arglist = this->buildArgList();
        return CXmRowColumn(XmCreateMenuBar(this->_parent,
            (char*)this->_name, arglist.data(),
            static_cast<Cardinal>(arglist.size())));
    }
};

class CXmPulldownMenuBuilder : public CXmRowColumnBuilder
{
public:
    using CXmRowColumnBuilder::CXmRowColumnBuilder;

    CXmRowColumn build()
    {
        auto arglist = this->buildArgList();
        return CXmRowColumn(XmCreatePulldownMenu(this->_parent,
            (char*)this->_name, arglist.data(),
            static_cast<Cardinal>(arglist.size())));
    }
};
