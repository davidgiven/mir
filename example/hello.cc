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

    CXmMainWindow mainWindow = CXmMainWindowBuilder(toplevel, "mainWindow")
                                   .setWidth(400)
                                   .setHeight(300)
                                   .build()
                                   .manageChild();

    auto menubar =
        CXmMenuBarBuilder(mainWindow, "menubar").build().manageChild();
    auto fileMenu = CXmPulldownMenuBuilder(menubar, "fileMenu").build();

    CXmPushButtonBuilder(fileMenu, "quitButton")
        .setLabelString(XmStringCreateLocalized((char*)"Quit"))
        .build()
        .addActivateCallback(
            [](XtPointer)
            {
                exit(0);
            })
        .manageChild();
    CXmCascadeButtonBuilder(menubar, "fileButton")
        .setLabelString(XmStringCreateLocalized((char*)"File"))
        .setSubMenuId(fileMenu)
        .build()
        .manageChild();

    auto layout = CXmFormBuilder(mainWindow, "layout")
                      .build()
                      .manageChild();

    CXmPushButtonBuilder(layout, "button1")
        .setLabelString(XmStringCreateLocalized((char*)"Button 1"))
        .setShowAsDefault(true)
        .build()
        .addActivateCallback(
            [](XtPointer)
            {
                printf("Button 1 pressed\n");
            })
        .manageChild();

    CXmPushButton button2 =
        CXmPushButtonBuilder(layout, "button2")
            .setLabelString(XmStringCreateLocalized((char*)"Button 2"))
            .setShowAsDefault(false)
            .setAlignment(XmALIGNMENT_CENTER)
            .build()
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

    XmMainWindowSetAreas(mainWindow, menubar, NULL, NULL, NULL, layout);
    XtRealizeWidget(toplevel);

    // printf("button 2 width = %d\n", button2.getWidth());

    XtAppMainLoop(app);
}
