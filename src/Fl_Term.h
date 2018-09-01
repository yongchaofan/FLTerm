//
// "$Id: Fl_Term.h 4175 2018-08-31 21:08:10 $"
//
// Fl_Term -- A terminal simulation widget
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
#include <FL/Fl_Draw.H>

#ifndef _FL_TERM_H_
#define _FL_TERM_H_
typedef void ( term_callback )(void *, const char *, int);
class Fl_Term : public Fl_Widget {
	char c_attr;		//current character attribute(color)
	char *buff;			//buffer for characters, one byte per char
	char *attr;			//buffer for attributes, one byte per char
	int buff_size; 		//current buffer size, doubles at more_chars()
	int *line;			//buffer for starting position of each line
	int line_size;		//current max number of lines, doubles at more_lines()
	int size_x_; 		//screen width in number of characters
	int size_y_;		//screen height in number of characters
	int buff_x;			//index to buff and attr for current insert position
	int	buff_y;			//index to line buffer for current row of text
	int save_y;			//previous buff_y when switch to alternate screen
	int screen_y;		//the line at top of screen
	int scroll_y;		//scroll offset for the current top line
	int roll_top;
	int roll_bot;		//the range of lines that will scroll in vi
	int sel_left;
	int sel_right;		//begin and end of selection on screen
	int iFontWidth;		//current font width in pixels
	int iFontHeight;	//current font height in pixels
	int font_size;		//current font weight
	int bMouseScroll;	//if mouse if dragged on scrollbar
	int page_up_hold; 	//control of scroll speed
	int page_down_hold;
	
	int bEscape;		//escape sequence processing mode
	int bInsert;		//insert mode, for inline editing for commands
	int bGraphic;		//graphic character mode, for text mode drawing
	int bAlterScreen;	//alternative screen for vi
	int bAppCursor;		//app cursor mode for vi
	int bTitle;			//title mode, changed through escape sequence
	int ESC_idx;		//current index for ESC_code
	char ESC_code[20];	//cumulating the current escape sequence before process
	
	char sTitle[64];	//window title sent by host
	char sPrompt[32];	//wait for sPrompt before next command when scripting
	int iPrompt;		//length of sPrompt
	int bPrompt;		//if sPrompt was found after the last append
	int bEnter;			//set when enter key is pressed
	int bEnter1;		//set at second enter key press, for prompt detection
	int iTimeOut;		//time out in seconds while waiting for sPrompt
	int recv0;			//cursor_x at the start of last command
	
	int bDND;			//if a FL_PASTE is result of drag&drop
	int bLive;			//host reading thread is running
	int bWait;			//waitfor() function is waiting for string in buffer
	int bLogging;		//if logging is active 
	FILE *fpLogFile;

	term_callback *term_cb;
	void *term_data_;

protected: 
	void draw();
	void next_line();
	void append( const char *newtxt, int len );
	const char *vt100_Escape( const char *sz, int cnt );

public:
	Fl_Term(int X,int Y,int W,int H,const char* L=0);
	~Fl_Term();
	int  handle( int e );
	void resize( int X, int Y, int W, int H );
	void clear();

	int textsize() { return font_size; }
	int size_x() { return size_x_; }
	int size_y() { return size_y_; }
	int live() { return bLive; }
	int logging() { return bLogging; }

	void save( const char *fn );
	void logging( const char *fn );
	void srch( const char *word, int dirn=-1 );	

	void textsize( int pt );
	void live(int c) { bLive = c; }
	void timeout(int t) { iTimeOut = t; }
	void prompt(char *p);

	void puts(const char *buf){ append(buf, strlen(buf)); }
	void puts(const char *buf, int len){ append(buf, len); }
	void putxml(const char *msg, int len);
	void mark_prompt();
	int  wait_prompt( char **preply );
	int  waitfor(const char *word);
	void callback(term_callback *cb, void *data) {
		term_cb = cb;
		term_data_ = data;
	}
	void do_callback(const char *buf, int len) {
		if ( term_cb!=NULL ) term_cb(term_data_, buf, len);
	}
	void *term_data() { return term_data_; }
	const char *title() { return sTitle; }
};
#endif