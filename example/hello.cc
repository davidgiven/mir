#include <tuple>
#include <utility>
#include <string>
#include <format>
#include "include/cmotif.h"
#include "include/cgc.h"

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
            [=](XmPushButtonCallbackStruct*)
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
            [](XmPushButtonCallbackStruct*)
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
                                    [](XmPushButtonCallbackStruct*)
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
                [](XmPushButtonCallbackStruct*)
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

    CXmText text = CXmTextBuilder(layout, "text")
                       .setLeftAttachment(XmATTACH_FORM)
                       .setRightAttachment(XmATTACH_FORM)
                       .setTopAttachment(XmATTACH_WIDGET)
                       .setTopWidget(button2)
                       .build()
                       .manageChild();

    Colormap colormap = layout.get<Colormap>(XtNcolormap);
    XColor red{
        .red = 65535, .green = 0, .blue = 0, .flags = DoRed | DoGreen | DoBlue};
    if (!XAllocColor(XtDisplay(layout), colormap, &red))
    {
        fprintf(stderr, "Could not allocate red in the widget colormap\n");
        return 1;
    }

    CXmList list = CXmListBuilder(layout, "list")
                       .setLeftAttachment(XmATTACH_FORM)
                       .setRightAttachment(XmATTACH_FORM)
                       .setTopAttachment(XmATTACH_WIDGET)
                       .setTopWidget(text)
                       .setHeight(100)
                       .setSelectionPolicy(XmMULTIPLE_SELECT)
                       .build()
                       .addItem("item one", 0)
                       .addItem("item two", 0)
                       .addItem("item three", 0)
                       .addItem("item four", 0)
                       .addItem("item five", 0)
                       .addItem("item six", 0)
                       .manageChild();

    CXmDrawingArea drawingArea =
        CXmDrawingAreaBuilder(layout, "drawingArea")
            .setLeftAttachment(XmATTACH_FORM)
            .setRightAttachment(XmATTACH_FORM)
            .setTopAttachment(XmATTACH_WIDGET)
            .setTopWidget(list)
            .setBottomAttachment(XmATTACH_FORM)
            .build()
            .addExposeCallback(
                [&](XmDrawingAreaCallbackStruct* data)
                {
                    Display* display = data->event->xany.display;
                    Window window = data->window;

                    XClearWindow(display, window);
                    CGC gc = CGCBuilder(display, window)
                                 .setForeground(red.pixel)
                                 .build();
                    gc.drawLine(0,
                        0,
                        static_cast<int>(drawingArea.getWidth()) - 1,
                        static_cast<int>(drawingArea.getHeight()) - 1);
                })
            .manageChild();

    mainWindow.setAreas(menubar, NULL, NULL, NULL, layout);
    toplevel.realize();

    static const XSetWindowAttributes attributes{.bit_gravity = ForgetGravity};
    XChangeWindowAttributes(XtDisplay(drawingArea),
        XtWindow(drawingArea),
        CWBitGravity,
        (XSetWindowAttributes*)&attributes);

    XtAppMainLoop(app);
}
