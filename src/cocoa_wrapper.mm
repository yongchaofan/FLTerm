#import <Cocoa/Cocoa.h>
#include <FL/x.H>
#include <FL/Fl_Window.H>

void setTransparency(Fl_Window *w, double alpha) {
    [fl_xid(w) setAlphaValue:alpha];
}