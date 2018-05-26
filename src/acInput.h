//
// "$Id: acInput.h 486 2018-05-25 23:48:10 $"
//
// Fl_Input widget extended with auto completion 
//
// Copyright 2017-2018 by Yongchao Fan.
//
// This library is free software distributed under GUN LGPL 3.0,
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
#include <string>
#include <vector>

#ifndef _AC_INPUT_H_
#define _AC_INPUT_H_

class acInput: public Fl_Input {
private:
	std::vector<char*> cmds;
	char keys[256];
	int id;
public:
	acInput(int X,int Y,int W,int H,const char* L=0):Fl_Input(X,Y,W,H,L)
	{
		cmds.clear();
		cmds.push_back(strdup("ssh 192.168.1.1"));
		keys[0] = 0;
		id = 0;
	}
	void add( const char *cmd );
	int handle( int e );
};
#endif  //acInput
