#include <tuple>
#include <utility>
#include <string>
#include <format>
#include "include/cmotif.h"

int main(int argc, char* argv[])
{
    XtAppContext app;

    CWidget toplevel = CWidget(XtVaAppInitialize(
        &app, "HelloWorld", NULL, 0, &argc, argv, NULL, NULL));

    CXmMainWindow mainWindow = CXmMainWindowBuilder(toplevel, "mainWindow")
                                   .setWidth(400)
                                   .setHeight(300)
                                   .build()
                                   .manageChild();

    CXmRowColumn menubar =
        CXmMenuBarBuilder(mainWindow, "menubar").build().manageChild();
    CXmRowColumn fileMenu = CXmPulldownMenuBuilder(menubar, "fileMenu").build();

    CXmPushButtonBuilder(fileMenu, "about")
        .setLabelString("About")
        .build()
        .addActivateCallback(
            [=](XtPointer)
            {
                CXmMessageBox dialogue =
                    CXmInformationDialogBuilder(toplevel, "aboutDialog")
                        .setDialogStyle(XmDIALOG_MODELESS)
                        .setMessageString("Hello World!")
                        .setDialogTitle("About")
                        .setCancelLabelString("")
                        .build()
                        .manageChild();
                XtUnmanageChild(dialogue.findWidgetByName("Cancel"));
                XtUnmanageChild(dialogue.findWidgetByName("Help"));
            })
        .manageChild();
    CXmPushButtonBuilder(fileMenu, "quit")
        .setLabelString("Quit")
        .setMnemonic(XStringToKeysym("Q"))
        .build()
        .addActivateCallback(
            [](XtPointer)
            {
                exit(0);
            })
        .manageChild();
    CXmCascadeButtonBuilder(menubar, "fileButton")
        .setLabelString("File")
        .setMnemonic(XStringToKeysym("F"))
        .setSubMenuId(fileMenu)
        .build()
        .manageChild();

    CXmForm layout = CXmFormBuilder(mainWindow, "layout").build().manageChild();

    CXmPushButton button1 = CXmPushButtonBuilder(layout, "button1")
                                .setLabelString("Button 1")
                                .setShowAsDefault(true)
                                .setTopAttachment(XmATTACH_FORM)
                                .setLeftAttachment(XmATTACH_FORM)
                                .setRightAttachment(XmATTACH_FORM)
                                .build()
                                .addActivateCallback(
                                    [](XtPointer)
                                    {
                                        printf("Button 1 pressed\n");
                                    })
                                .manageChild();

    CXmPushButton button2 =
        CXmPushButtonBuilder(layout, "button2")
            .setLabelString("Button 2")
            .setShowAsDefault(false)
            .setAlignment(XmALIGNMENT_CENTER)
            .setLeftAttachment(XmATTACH_FORM)
            .setRightAttachment(XmATTACH_FORM)
            .setTopAttachment(XmATTACH_WIDGET)
            .setTopWidget(button1)
            .build()
            .addActivateCallback(
                [](XtPointer)
                {
                    printf("Button 2 pressed\n");
                })
            .addEventHandler(
                [&](XEvent* event, Boolean* continue_to_dispatch)
                {
                    button2.setLabelString(std::make_unique<CXmString>(
                        std::format("width={}", button2.getWidth()).c_str()));
                },
                StructureNotifyMask)
            .manageChild();

    CXmTextBuilder(layout, "text")
        .setLeftAttachment(XmATTACH_FORM)
        .setRightAttachment(XmATTACH_FORM)
        .setTopAttachment(XmATTACH_WIDGET)
        .setTopWidget(button2)
        .build()
        .manageChild();

    XmMainWindowSetAreas(mainWindow, menubar, NULL, NULL, NULL, layout);
    XtRealizeWidget(toplevel);

    XtAppMainLoop(app);
}
