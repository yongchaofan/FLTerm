//
// "$Id: Fl_Term.h 3830 2018-06-18 13:48:10 $"
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
#include "Hosts.h"
#include <FL/Fl.H>
#include <FL/Fl_Draw.H>
#include <thread>

#ifdef __APPLE__ 
	#define TERMFONT "Monaco"
#else
	#define TERMFONT "Consolas"
#endif

#ifndef _FL_TERM_H_
#define _FL_TERM_H_
class Fl_Term : public Fl_Widget {
	char c_attr;			//current character attribute(color)
	char *buff, *attr;		//buffer for characters and attributes, one byte per char
	int buff_size; 			//current buffer size, doubles by roll() when exceeded
	int *line;				//buffer for lines, records starting position of each line
	int line_size;			//current max number of lines, doubles by roll() when exceeded
	int size_x, size_y;		//current screen size in number of characters
	int cursor_x;			//index to buff and attr for current insert position
	int	cursor_y;			//index to line buffer for current row of text
	int save_y;				//saves the previous cursor_y when switch to alternate screen
	int screen_y;			//the line at top of screen
	int scroll_y;			//scroll offset for the current line at top of screen
	int bMouseScroll;		//if mouse if dragged on scrollbar
	int page_up_hold, page_down_hold;		//control of scroll speed
	int roll_top, roll_bot;	//the range of lines that will scroll in vi
	int sel_left, sel_right;//begin and end of selection on screen
	int iFontWidth, iFontHeight;
	int font_size;
	
	int bEscape;			//escape sequence processing mode
	int bInsert;			//insert mode, for inline editing for commands
	int bGraphic;			//graphic character mode, for text mode drawing
	int bAlterScreen;		//alternative screen for vi
	int bAppCursor;			//app cursor mode for vi
	int bTitle;				//title mode, changed through escape sequence
	int ESC_idx;			//current index for ESC_code
	char ESC_code[20];		//cumulating the current escape sequence before process
	
	char sPrompt[32];		//wait for sPrompt before next command when scripting
	int iPrompt;			//length of sPrompt
	int bPrompt;			//if sPrompt was found after the last append
	int bEnter;				//set when enter key is pressed, cleared at next key press 
	int bEnter1;
	int iTimeOut;			//time out in seconds while waiting for sPrompt
	int recv0;				//cursor_x at the start of last command
	
	int bLogging;			//if logging is active 
	FILE *fpLogFile;
	int cursor;
	int bGets, bWait;
	int bReturn;
	int bPassword;
	char keys[256];

	std::thread readerThread;
	int bReaderRunning;
	int bDND;
	char script[4096];
	
protected: 
	Fan_Host *host;
	void draw();
	void reader();
	void scper();
	void sftper();
	void scripter();
	void more_lines();
	void more_chars();
	void append( const char *newtxt, int len );
	const char *vt100_Escape( const char *sz );

public:
	Fl_Term(int X,int Y,int W,int H,const char* L=0);
	~Fl_Term();
	int  handle( int e );
	void resize( int X, int Y, int W, int H );
	void textsize( int pt );
	void clear();
	int cursorx();

	void set_host(Fan_Host *pHost);
	void start_reader();
	void stop_reader();
	void run_script(const char *text);
	int active() { return bReaderRunning; }

	int  logging() { return bLogging; }
	void logg( const char *fn );
	void save( const char *fn );
	void srch( const char *word, int dirn=-1 );	

	int  waitfor(const char *word);
	int  command( const char *cmd, char **response );
	char *gets(const char *prompt, int echo);
	void write(const char *buf);
	void print(const char *fmt, ...);
};
#endif