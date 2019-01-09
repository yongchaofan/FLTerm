//
// "$Id: Fl_Term.h 4370 2018-11-15 21:08:10 $"
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
#include <FL/fl_draw.H>

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
	int cursor_x;		//index to buff and attr for current insert position
	int	cursor_y;		//index to line buffer for current row of text
	int save_x;			//save_x/save_y also used to save and restore cursor
	int save_y;			//previous cursor_y when switch to alternate screen
	int screen_y;		//the line at top of screen
	int scroll_y;		//scroll offset for the current top line
	int roll_top;
	int roll_bot;		//the range of lines that will scroll in vi
	int sel_left;
	int sel_right;		//begin and end of selection on screen
	int iFontWidth;		//current font width in pixels
	int iFontHeight;	//current font height in pixels
	int font_size;		//current font weight
	int bMouseScroll;	//if mouse is dragged on scrollbar
	
	int bEscape;		//escape sequence processing mode
	int ESC_idx;		//current index for ESC_code
	char ESC_code[20];	//cumulating the current escape sequence before process

	int bInsert;		//insert mode, for inline editing for commands
	int bGraphic;		//graphic character mode, for text mode drawing
	int bCursor;		//display cursor or not
	int bAppCursor;		//app cursor mode for vi
	int bAlterScreen;	//alternative screen for vi
	
	int bTitle;			//title mode, changed through escape sequence
	int title_idx;
	char sTitle[256];	//window title set by host

	char sPrompt[32];	//wait for sPrompt before next command when scripting
	int iPrompt;		//length of sPrompt
	int bPrompt;		//if sPrompt was found after the last append

	int bEnter;			//set when enter key is pressed
	int bEnter1;		//set at second enter key press, for prompt detection
	int iTimeOut;		//time out in seconds while waiting for sPrompt
	int recv0;			//cursor_x at the start of last command
	int xmlIndent;		//used by putxml
	int xmlTagIsOpen;	//used by putxml
	
	int bDND;			//if a FL_PASTE is result of drag&drop
	int bLive;			//host reading thread is running
	int bWait;			//waitfor() function is waiting for string in buffer
	int bEcho;			//if local echo is active
	int bLogging;		//if logging is active 
	FILE *fpLogFile;
	
	int bScriptRunning;
	int bScriptPaused;

	term_callback *term_cb;

protected: 
	void draw();
	void next_line();
	void append( const char *newtxt, int len );
	const unsigned char *vt100_Escape( const unsigned char *sz, int cnt );
	const unsigned char *telnet_options( const unsigned char *p);

public:
	Fl_Term(int X,int Y,int W,int H,const char* L=0);
	~Fl_Term();
	int  handle( int e );
	void resize( int X, int Y, int W, int H );
	void clear();

	int size_x() { return size_x_; }
	int size_y() { return size_y_; }
	int live() { return bLive; }
	int logg() { return bLogging; }
	int echo() { return bEcho; }

	void echo(int e);
	void logg( const char *fn );
	void srch( const char *word );	

	void textsize( int pt=0 );
	void buffsize( int lines );
	void live(int c) { bLive = c; }
	const char *title() { return sTitle; }

	void callback(term_callback *cb, void *data) {
		term_cb = cb;
		user_data(data);
	}
	void do_callback(const char *buf, int len) {
		if ( term_cb!=NULL ) 
			term_cb(user_data(), buf, len);
	}
	void write(const char *buf, int len) { 
		if ( bEcho ) append(buf, len);
		do_callback(buf, len);
	}
	
	char *mark_prompt();
	int  waitfor_prompt();

	void disp(const char *buf);
	void send(const char *buf);
	int recv(char **preply);
	int cmd(const char *cmd, char **preply);
	
	void scripter(char *cmds);
	void run_script(char *script);
	void pause_script();
	void stop_script();

	void puts(const char *buf, int len){ append(buf, len); }
	void putxml(const char *msg, int len);
};
#endif