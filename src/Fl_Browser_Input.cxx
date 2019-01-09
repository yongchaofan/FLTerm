//
// "$Id: Fl_Browser_Input.cxx 2568 2018-08-31 13:48:10 $"
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
#include <string.h>
Fl_Browser_Input::Fl_Browser_Input(int X,int Y,int W,int H,const char* L)
													: Fl_Input(X,Y,W,H,L)
{
	browserWin = new Fl_Menu_Window(1,1);
		browser = new Fl_Browser(0,0,1,1);
		browser->clear();
		browser->box(FL_UP_BOX);
	browserWin->end();
	browserWin->clear_border();
	browserWin->resizable(browser);
	id = 0;
}
void Fl_Browser_Input::resize( int X, int Y, int W, int H )
{
	Fl_Input::resize(X, Y, W, H);
	browserWin->resize( parent()->x(), parent()->y()+y()-84, w(), 84);
}
void Fl_Browser_Input::add( const char *cmd ) 
{
	if ( *cmd==0 ) return;
	if ( browser->size()==0 ) browser->add(cmd);
	int i=0;
	while ( ++i<=browser->size() ) {
		if ( strcmp(cmd, browser->text(i))==0 ) return;
		if ( strcmp(cmd, browser->text(i))>0 ) break;
	}
	browser->insert(i, cmd);
}
const char * Fl_Browser_Input::first( ) 
{
	id = 1;
	if ( id<=browser->size() )
		return browser->text(id);
	else
		return NULL;
}
const char * Fl_Browser_Input::next( )
{
	if ( id<browser->size() )
		return browser->text(++id);
	else
		return NULL;
}
int Fl_Browser_Input::handle( int e ) 
{
	int rc = Fl_Input::handle(e);
	if ( e==FL_KEYDOWN && Fl::event_state(FL_ALT|FL_CTRL|FL_META)==0 ) {
		switch ( Fl::event_key() ) {
		case FL_Left:
		case FL_Right:
		case FL_Delete:
		case FL_BackSpace:
		case FL_Enter: break;
		case FL_Up:	
			if ( id>1 ) id--;
			browserWin->show();
			browser->value(id);
			value(browser->text(id));
			take_focus();
			break;
		case FL_Down: 
			if ( id<browser->size() ) id++;
			browserWin->show();
			browser->value(id);
			value(browser->text(id));
			take_focus();
			break;
		default: {
			for ( int i=browser->size(); i>0&&position()>0; i-- ) 
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
	}
	return rc;
}