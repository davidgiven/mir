#pragma once
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
    static int registerEventHandler(
        Widget widget, std::function<void(XtPointer, XEvent*, Boolean*)> handler);
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

#include "cmotif_impl.h"
