//
// "$Id: Fl_Browser_Input.h 1236 2018-08-24 13:48:10 $"
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

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Window.H>
#include <FL/Fl_Select_Browser.H>
#include <string>

#ifndef _BROWSER_INPUT_H_
#define _BROWSER_INPUT_H_

class Fl_Browser_Input: public Fl_Input {
private:
	Fl_Menu_Window *browserWin;
	Fl_Browser *browser;
	int id;
	int len;
	void update();
public:
	Fl_Browser_Input(int X,int Y,int W,int H,const char* L=0):Fl_Input(X,Y,W,H,L)
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
	~Fl_Browser_Input()
	{
		browserWin->hide();
	}
	int handle( int e );
	void resize( int X, int Y, int W, int H );
	void add( const char *cmd );
};
#endif  //acInput
