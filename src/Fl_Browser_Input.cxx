//
// "$Id: Fl_Browser_Input.cxx 3656 2020-07-09 13:48:10 $"
//
// Fl_Input widget extended with auto completion
//
// Copyright 2017-2020 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm2/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm2/issues/new
//
#include "Fl_Browser_Input.h"
#ifdef __APPLE__
#define FL_CMD FL_META
#else
#define FL_CMD FL_ALT
#endif
Fl_Browser_Input::Fl_Browser_Input(int X,int Y,int W,int H,const char* L):
														Fl_Input(X,Y,W,H,L)
{
	browser = new Fl_Browser(0,0,1,1);
	browser->clear();
	browser->textsize(16);
	browser->box(FL_FLAT_BOX);
	browser->has_scrollbar(Fl_Browser_::VERTICAL);
	browser->hide();
	id = 0;
}
void Fl_Browser_Input::resize( int X, int Y, int W, int H )
{
	Fl_Input::resize(X, Y, W, H);
	browser->resize(X, Y+((Y<104) ? h()+1 : -104), w(), 104);
}
int Fl_Browser_Input::add( const char *cmd )
{
	if ( strlen(cmd)<4 ) return 0;
	int i=0;
	while ( ++i<=browser->size() ) {
		int cmp = strcmp(cmd, browser->text(i));
		if ( cmp==0 ) return id=i;
		if ( cmp<0 ) break;
	}
	browser->insert(i, cmd);
	return id=i;
}
const char * Fl_Browser_Input::first( )
{
	if ( browser->size()>0 )
		return browser->text(id=1);
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
void Fl_Browser_Input::close()
{
	browser->hide();
}
int Fl_Browser_Input::handle( int e )
{
	if ( e!=FL_KEYDOWN ) return Fl_Input::handle(e);
	char cmd[2]={ 0, 0 };
	int key = Fl::event_key();
	switch ( key ) {
		case FL_Page_Up:
		case FL_Page_Down: 
			return 0;	//let Fl_Term handle it
		case FL_Tab: 	
			cmd[0] = key; cut();
			do_callback(this, (void *)value());
			do_callback(this, (void *)cmd);
			value("");
			//fall through		
		case FL_Escape: 
			browser->hide();
			return 1;	//prevent ESCAPE from close the program
		case FL_BackSpace:
		case FL_Delete:	//CMD+Delete delete entry from dictionary
			if ( Fl::event_state(FL_CMD) ) {
				browser->remove(id--);
				if ( id<0 ) id=0;
				browser->value(id);
				browser->middleline(id);
				return 1;
			}
	}
	int rc = Fl_Input::handle(e);
	if ( Fl::event_state(FL_CTRL) ) {
		char cmd[2]={ 0, 0 };
		if ( key>='a' && key<='z' ) *cmd=key-'a'+1;//A-Z, 1-26
		if ( key>218&&key<222 ) *cmd = key-192; //[\], 27, 28, 29
		if ( key=='^' ) *cmd = 30;				//^, 30
		if ( *cmd ) do_callback(this, (void *)cmd);
		return rc;
	}
	switch ( key ) {
		case FL_BackSpace: 
			if ( size()==0 ) do_callback(this, (void *)"\b");
			//fall through
		case FL_Delete:
		case FL_Enter: 
			browser->hide();
			break;
		case FL_Up:
		case FL_Down:
			if ( ((Fl_Widget *)browser)->visible() ) {
				if ( key==FL_Up && id>1 ) id--;
				if ( key==FL_Down && id<browser->size() ) id++;
				browser->value(id);
			}
			else {
				browser->show();
				browser->redraw();
			}
			value(browser->text(id));
			position(size());
			take_focus();
			break;
		default:
			if ( Fl::event_state(FL_ALT|FL_CTRL|FL_META)==0
				&& size()>0 && position()==size() ) { //search dictionary
				for ( int i=1; i<=browser->size(); i++ ) {
					int cmp = strncmp(value(), browser->text(i), position());
					if ( cmp>0 ) continue;
					if ( cmp==0 ) {		//match found
						id = i;
						int p = position();
						browser->select(id);
						browser->middleline(id);
						value(browser->text(id));
						position(p, size());
					}
					else {//cmp<0, 		no matches
						browser->hide();
					}
					break;
				}
			}
	}
	return rc;
}