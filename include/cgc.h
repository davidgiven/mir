#pragma once
#include <X11/Xlib.h>

class CGC
{
public:
    CGC(Display* display,
        Window window,
        unsigned long valuemask,
        XGCValues& values):
        _display(display),
        _window(window)
    {
        _gc = XCreateGC(display, window, valuemask, &values);
    }

    ~CGC()
    {
        XFreeGC(_display, _gc);
    }

    int drawArc(int x,
        int y,
        unsigned int width,
        unsigned int height,
        int angle1,
        int angle2)
    {
        return XDrawArc(
            _display, _window, _gc, x, y, width, height, angle1, angle2);
    }

    int drawArcs(XArc* arcs, int narcs)
    {
        return XDrawArcs(_display, _window, _gc, arcs, narcs);
    }

    int drawImageString(int x, int y, const char* string, int length)
    {
        return XDrawImageString(_display, _window, _gc, x, y, string, length);
    }

    int drawImageString16(int x, int y, const XChar2b* string, int length)
    {
        return XDrawImageString16(_display, _window, _gc, x, y, string, length);
    }

    int drawLine(int x1, int y1, int x2, int y2)
    {
        return XDrawLine(_display, _window, _gc, x1, y1, x2, y2);
    }

    int drawLines(XPoint* points, int npoints, int mode)
    {
        return XDrawLines(_display, _window, _gc, points, npoints, mode);
    }

    int drawPoint(int x, int y)
    {
        return XDrawPoint(_display, _window, _gc, x, y);
    }

    int drawPoints(XPoint* points, int npoints, int mode)
    {
        return XDrawPoints(_display, _window, _gc, points, npoints, mode);
    }

    int drawRectangle(int x, int y, unsigned int width, unsigned int height)
    {
        return XDrawRectangle(_display, _window, _gc, x, y, width, height);
    }

    int drawRectangles(XRectangle* rectangles, int nrectangles)
    {
        return XDrawRectangles(_display, _window, _gc, rectangles, nrectangles);
    }

    int drawSegments(XSegment* segments, int nsegments)
    {
        return XDrawSegments(_display, _window, _gc, segments, nsegments);
    }

    int drawString(int x, int y, const char* string, int length)
    {
        return XDrawString(_display, _window, _gc, x, y, string, length);
    }

    int drawString16(int x, int y, const XChar2b* string, int length)
    {
        return XDrawString16(_display, _window, _gc, x, y, string, length);
    }

    int drawText(int x, int y, XTextItem* items, int nitems)
    {
        return XDrawText(_display, _window, _gc, x, y, items, nitems);
    }

    int drawText16(int x, int y, XTextItem16* items, int nitems)
    {
        return XDrawText16(_display, _window, _gc, x, y, items, nitems);
    }

    int fillArc(int x,
        int y,
        unsigned int width,
        unsigned int height,
        int angle1,
        int angle2)
    {
        return XFillArc(
            _display, _window, _gc, x, y, width, height, angle1, angle2);
    }

    int fillArcs(XArc* arcs, int narcs)
    {
        return XFillArcs(_display, _window, _gc, arcs, narcs);
    }

    int fillPolygon(XPoint* points, int npoints, int shape, int mode)
    {
        return XFillPolygon(
            _display, _window, _gc, points, npoints, shape, mode);
    }

    int fillRectangle(int x, int y, unsigned int width, unsigned int height)
    {
        return XFillRectangle(_display, _window, _gc, x, y, width, height);
    }

    int fillRectangles(XRectangle* rectangles, int nrectangles)
    {
        return XFillRectangles(_display, _window, _gc, rectangles, nrectangles);
    }

    int putImage(XImage* image,
        int src_x,
        int src_y,
        int dest_x,
        int dest_y,
        unsigned int width,
        unsigned int height)
    {
        return XPutImage(_display,
            _window,
            _gc,
            image,
            src_x,
            src_y,
            dest_x,
            dest_y,
            width,
            height);
    }

    void mbDrawText(int x, int y, XmbTextItem* text_items, int nitems)
    {
        XmbDrawText(_display, _window, _gc, x, y, text_items, nitems);
    }

    void wcDrawText(int x, int y, XwcTextItem* text_items, int nitems)
    {
        XwcDrawText(_display, _window, _gc, x, y, text_items, nitems);
    }

    void utf8DrawText(int x, int y, XmbTextItem* text_items, int nitems)
    {
        Xutf8DrawText(_display, _window, _gc, x, y, text_items, nitems);
    }

    int setArcMode(int arc_mode)
    {
        return XSetArcMode(_display, _gc, arc_mode);
    }

    int setBackground(unsigned long background)
    {
        return XSetBackground(_display, _gc, background);
    }

    int setClipMask(Pixmap pixmap)
    {
        return XSetClipMask(_display, _gc, pixmap);
    }

    int setClipOrigin(int clip_x_origin, int clip_y_origin)
    {
        return XSetClipOrigin(_display, _gc, clip_x_origin, clip_y_origin);
    }

    int setClipRectangles(int clip_x_origin,
        int clip_y_origin,
        XRectangle* rectangles,
        int n,
        int ordering)
    {
        return XSetClipRectangles(_display,
            _gc,
            clip_x_origin,
            clip_y_origin,
            rectangles,
            n,
            ordering);
    }

    int setDashes(int dash_offset, const char* dash_list, int n)
    {
        return XSetDashes(_display, _gc, dash_offset, dash_list, n);
    }

    int setFillRule(int fill_rule)
    {
        return XSetFillRule(_display, _gc, fill_rule);
    }

    int setFillStyle(int fill_style)
    {
        return XSetFillStyle(_display, _gc, fill_style);
    }

    int setFont(Font font)
    {
        return XSetFont(_display, _gc, font);
    }

    int setForeground(unsigned long foreground)
    {
        return XSetForeground(_display, _gc, foreground);
    }

    int setFunction(int function)
    {
        return XSetFunction(_display, _gc, function);
    }

    int setGraphicsExposures(Bool graphics_exposures)
    {
        return XSetGraphicsExposures(_display, _gc, graphics_exposures);
    }

    int setLineAttributes(
        unsigned int line_width, int line_style, int cap_style, int join_style)
    {
        return XSetLineAttributes(
            _display, _gc, line_width, line_style, cap_style, join_style);
    }

    int setPlaneMask(unsigned long plane_mask)
    {
        return XSetPlaneMask(_display, _gc, plane_mask);
    }

    int setState(unsigned long foreground,
        unsigned long background,
        int function,
        unsigned long plane_mask)
    {
        return XSetState(
            _display, _gc, foreground, background, function, plane_mask);
    }

    int setStipple(Pixmap stipple)
    {
        return XSetStipple(_display, _gc, stipple);
    }

    int setSubwindowMode(int subwindow_mode)
    {
        return XSetSubwindowMode(_display, _gc, subwindow_mode);
    }

    int setTSOrigin(int ts_x_origin, int ts_y_origin)
    {
        return XSetTSOrigin(_display, _gc, ts_x_origin, ts_y_origin);
    }

    int setTile(Pixmap tile)
    {
        return XSetTile(_display, _gc, tile);
    }

private:
    Display* _display;
    Window _window;
    GC _gc;
};

class CGCBuilder
{
public:
    CGCBuilder(Display* display, Window window):
        _display(display),
        _window(window),
        _valueMask(0)
    {
    }

    CGC build()
    {
        return CGC(_display, _window, _valueMask, _values);
    }

    CGCBuilder& setFunction(int value)
    {
        _values.function = value;
        _valueMask |= GCFunction;
        return *this;
    }

    CGCBuilder& setPlaneMask(unsigned long value)
    {
        _values.plane_mask = value;
        _valueMask |= GCPlaneMask;
        return *this;
    }

    CGCBuilder& setForeground(unsigned long value)
    {
        _values.foreground = value;
        _valueMask |= GCForeground;
        return *this;
    }

    CGCBuilder& setBackground(unsigned long value)
    {
        _values.background = value;
        _valueMask |= GCBackground;
        return *this;
    }

    CGCBuilder& setLineWidth(int value)
    {
        _values.line_width = value;
        _valueMask |= GCLineWidth;
        return *this;
    }

    CGCBuilder& setLineStyle(int value)
    {
        _values.line_style = value;
        _valueMask |= GCLineStyle;
        return *this;
    }

    CGCBuilder& setCapStyle(int value)
    {
        _values.cap_style = value;
        _valueMask |= GCCapStyle;
        return *this;
    }

    CGCBuilder& setJoinStyle(int value)
    {
        _values.join_style = value;
        _valueMask |= GCJoinStyle;
        return *this;
    }

    CGCBuilder& setFillStyle(int value)
    {
        _values.fill_style = value;
        _valueMask |= GCFillStyle;
        return *this;
    }

    CGCBuilder& setFillRule(int value)
    {
        _values.fill_rule = value;
        _valueMask |= GCFillRule;
        return *this;
    }

    CGCBuilder& setArcMode(int value)
    {
        _values.arc_mode = value;
        _valueMask |= GCArcMode;
        return *this;
    }

    CGCBuilder& setTile(Pixmap value)
    {
        _values.tile = value;
        _valueMask |= GCTile;
        return *this;
    }

    CGCBuilder& setStipple(Pixmap value)
    {
        _values.stipple = value;
        _valueMask |= GCStipple;
        return *this;
    }

    CGCBuilder& setTsXOrigin(int value)
    {
        _values.ts_x_origin = value;
        _valueMask |= GCTileStipXOrigin;
        return *this;
    }

    CGCBuilder& setTsYOrigin(int value)
    {
        _values.ts_y_origin = value;
        _valueMask |= GCTileStipYOrigin;
        return *this;
    }

    CGCBuilder& setFont(Font value)
    {
        _values.font = value;
        _valueMask |= GCFont;
        return *this;
    }

    CGCBuilder& setSubwindowMode(int value)
    {
        _values.subwindow_mode = value;
        _valueMask |= GCSubwindowMode;
        return *this;
    }

    CGCBuilder& setGraphicsExposures(Bool value)
    {
        _values.graphics_exposures = value;
        _valueMask |= GCGraphicsExposures;
        return *this;
    }

    CGCBuilder& setClipXOrigin(int value)
    {
        _values.clip_x_origin = value;
        _valueMask |= GCClipXOrigin;
        return *this;
    }

    CGCBuilder& setClipYOrigin(int value)
    {
        _values.clip_y_origin = value;
        _valueMask |= GCClipYOrigin;
        return *this;
    }

    CGCBuilder& setClipMask(Pixmap value)
    {
        _values.clip_mask = value;
        _valueMask |= GCClipMask;
        return *this;
    }

    CGCBuilder& setDashOffset(int value)
    {
        _values.dash_offset = value;
        _valueMask |= GCDashOffset;
        return *this;
    }

    CGCBuilder& setDashes(char value)
    {
        _values.dashes = value;
        _valueMask |= GCDashList;
        return *this;
    }

private:
    Display* _display;
    Window _window;
    unsigned long _valueMask;
    XGCValues _values;
};
