#include <Xm/Xm.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <tuple>
#include <utility>
#include "include/cmotif.h"

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

    CXmPushButton::create(mainWindow, "button1")
        .setLabelString(XmStringCreateLocalized((char*)"Button 1"))
        .setShadowThickness(5)
        .setShowAsDefault(true)
        .addActivateCallback([](XtPointer) {
            printf("Button 1 pressed\n");
        })
        .manageChild();

    CXmPushButton::create(mainWindow, "button2")
        .setLabelString(XmStringCreateLocalized((char*)"Button 2"))
        .setShadowThickness(5)
        .setShowAsDefault(false)
        .addActivateCallback([](XtPointer) {
            printf("Button 2 pressed\n");
        })
        .manageChild();

    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);
}
