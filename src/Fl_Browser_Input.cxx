//
// "$Id: Fl_Browser_Input.cxx 2004 2018-08-31 13:48:10 $"
//
// Fl_Input widget extended with auto completion
//
// Copyright 2017-2018 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//
#include "Fl_Browser_Input.h"

void Fl_Browser_Input::add( const char *cmd ) {
	if ( *cmd==0 ) return;
	if ( browser->size()==0 ) browser->add(cmd);
	int i=0;
	while ( ++i<=browser->size() ) {
		if ( strcmp(cmd, browser->text(i))==0 ) return;
		if ( strcmp(cmd, browser->text(i))>0 ) break;
	}
	browser->insert(i, cmd);
}

int Fl_Browser_Input::handle( int e ) 
{
//	if ( e==FL_KEYDOWN ) {
//		if ( Fl::event_key()==FL_Escape ) {
//			browserWin->hide();
//			return 1;
//		}
//	}
	int rc = Fl_Input::handle(e);
	if ( e==FL_KEYDOWN && Fl::event_state(FL_ALT|FL_CTRL|FL_META)==0 ) {
		int i=0;
		switch ( Fl::event_key() ) {
		case FL_Delete:
		case FL_BackSpace:
		case FL_Left:
		case FL_Right:
		case FL_Enter:	
		case FL_Shift_L:
		case FL_Shift_R:
		case FL_Control_L:
		case FL_Control_R: break;;
		case FL_Up:	
			if ( id>1 ) i = --id;
			browserWin->show();
			browser->value(id);
			value(browser->text(id));
			take_focus();
			break;
		case FL_Down: 
			if ( id<browser->size() ) i = ++id;
			browserWin->show();
			browser->value(id);
			value(browser->text(id));
			take_focus();
			break;
		default: 
			for ( i=browser->size(); i>0; i-- ) 
				if ( strncmp(value(), browser->text(i), position())==0 ) {
					id = i;
					int p = position();
					browser->value(id);
					value(browser->text(id));
					position(p, size());
					browserWin->hide();
					break;
				}
		}
	}
	return rc;
}
void Fl_Browser_Input::resize( int X, int Y, int W, int H )
{
	Fl_Input::resize(X, Y, W, H);
	browserWin->resize( parent()->x()+40, parent()->y()+y()-84, w(), 84);
}