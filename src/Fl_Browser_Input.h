//
// "$Id: Fl_Browser_Input.h 920 2020-07-09 13:48:10 $"
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

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Browser.H>

#ifndef _BROWSER_INPUT_H_
#define _BROWSER_INPUT_H_
class Fl_Browser_Input: public Fl_Input {
private:
	Fl_Browser *browser;
	int id;
public:
	Fl_Browser_Input(int X,int Y,int W,int H,const char* L=0);
	~Fl_Browser_Input() {}
	void resize(int X, int Y, int W, int H);
	int handle(int e);
	int add(const char *cmd);
	const char *first();
	const char *next();
	void close();
};
#endif  //_BROWSER_INPUT_H_