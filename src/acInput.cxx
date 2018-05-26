//
// "$Id: Fl_Host.h 1825 2018-05-08 23:48:10 $"
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
#include "acInput.h"

void acInput::add( const char *cmd ) {
	std::vector<char *>::iterator it;
	int i=cmds.size()-1;
	if ( strcmp(cmd, cmds[i])>0 ) 
		cmds.push_back(strdup(cmd));
	else
	for ( it=cmds.begin(),i=0; it<cmds.end(); it++, i++ ) {
		if ( strcmp(cmd, cmds[i])==0 ) break;
		if ( strcmp(cmd, cmds[i]) < 0 ) {
			cmds.insert(it, strdup(cmd));
			break;
		}
	}
}
int acInput::handle( int e ) {
	int key = Fl::event_key();
	int len = position();
	if ( len>255 ) len=255;
	switch (e) {
		case FL_FOCUS: return 1;
		case FL_KEYDOWN: if ( Fl::event_state(FL_ALT)==0 ) {
			switch (key) {
				case FL_Up: while( --id >=0 ) {
						if ( strncmp(keys, cmds[id], len)==0 || keys[0]==0 ) {
							value(cmds[id]);
							position(len, strlen(cmds[id]));
							break;
						}
					}
					if ( id<0 ) id++;
					return 1;
				case FL_Down: while( ++id < (int)cmds.size() ) {
						if ( strncmp(keys, cmds[id], len)==0 || keys[0]==0 ) {
							value(cmds[id]);
							position(len, strlen(cmds[id]));
							break;
						}
					}
					if ( id==(int)cmds.size() ) id--;
					return 1;
				}
			}
			else 
				return 0;
			break;
		case FL_KEYUP: if ( len==0	) keys[0]=0;				
			else switch (key) {
			case FL_BackSpace:
			case FL_Delete: keys[len] = 0;
			case FL_Left:  
			case FL_Right: 
			case FL_Up: 
			case FL_Down:
			case FL_Enter:
			case FL_Shift_L:
			case FL_Shift_R:
			case FL_Control_L:
			case FL_Control_R: break;
			default: if ( len<size() ) break;
				{
					strncpy(keys, value(), 255);
					for ( id=0; id<(int)cmds.size(); id++ ) {
						if ( strncmp(keys, cmds[id], len)==0 ){
							value(cmds[id]);
							position(len, strlen(cmds[id]));
							break;
						}
					}
				}
			}
			break;
	}
	return Fl_Input::handle(e);
}
