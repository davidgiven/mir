#pragma once
#include <Xm/Xm.h>

class CWidget
{
public:
    CWidget(Widget w): _widget(w) {}

    operator Widget() const
    {
        return _widget;
    }

private:
    Widget _widget;
};

#include "cmotif_impl.h"
