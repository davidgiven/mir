#include <tuple>
#include <utility>
#include <string>
#include <format>
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
        .setShowAsDefault(true)
        .addActivateCallback(
            [](XtPointer)
            {
                printf("Button 1 pressed\n");
            })
        .manageChild();

    CXmPushButton button2 =
        CXmPushButton::create(mainWindow, "button2")
            .setLabelString(XmStringCreateLocalized((char*)"Button 2"))
            .setShowAsDefault(false)
            .setAlignment(XmALIGNMENT_CENTER)
            .addActivateCallback(
                [](XtPointer)
                {
                    printf("Button 2 pressed\n");
                })
            .addEventHandler(
                [&](XtPointer, XEvent* event, Boolean* continue_to_dispatch)
                {
                    button2.setLabelString(XmStringCreateLocalized(
                        (char*)(std::format("width={}", button2.getWidth())
                                .c_str())));
                },
                StructureNotifyMask)
            .manageChild();

    XtRealizeWidget(toplevel);

    printf("button 2 width = %d\n", button2.getWidth());

    XtAppMainLoop(app);
}
