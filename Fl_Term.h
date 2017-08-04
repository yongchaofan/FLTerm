//
// "$Id: Fl_Term.h 3988 2017-08-04 13:48:10 $"
//
// Fl_Term -- A terminal simulation widget
//
// Copyright 2017 by Yongchao Fan.
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
#include <FL/Fl_Ask.H>
#include <FL/Fl_Draw.H>

#ifdef __APPLE__ 
	#define TERMFONT "Monaco"
#else
	#define TERMFONT "Consolas"
#endif

#define HOST_IDLE			0
#define HOST_START			1
#define HOST_CONNECTING		2
#define HOST_AUTHENTICATING 4
#define HOST_CONNECTED		8

#ifndef _FL_TERM_H_
#define _FL_TERM_H_
class Fl_Term;
class Fl_Host {
protected:
	Fl_Term *term;
public: 
	virtual int set_term(Fl_Term *pTerm)	=0;
	virtual void start( const char *host )	=0;
	virtual	int state()						=0;

	virtual	int connect()					=0;
	virtual	void read()						=0;
	virtual	void write( const char *cmd )	=0;
	virtual void send_size(int sx, int sy)	=0;
	virtual	void disconn()					=0;	
};
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
	int scroll_y;			//scroll offset for the current line at top of screen
	int screen_y;			//the line at top of screen
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
	int iTimeOut;			//time out in seconds while waiting for sPrompt
	int recv0;				//cursor_x at the start of last command
	char script[4096];		//buffer for pasted or drag&dropped script
	
	int bLogging;			//if logging is active 
	FILE *fpLogFile;
	char keyword[256];		//search keyword

protected: 
	Fl_Host *host;
	void more_lines();
	void more_chars();
	const char *vt100_Escape( const char *sz );

public:
	Fl_Term(int X,int Y,int W,int H,const char* L=0);
	~Fl_Term();
	void draw();
	int  handle( int e );
	void resize( int X, int Y, int W, int H );
	void set_fontsize( int pt );

	void append( const char *newtxt, int len );
	void clear();
	void logg( const char *fn=NULL );
	void save( const char *fn=NULL );
	void srch( const char *word, int dirn=-1 );	

	void exec( const char *cmd );
	int  command( const char *cmd, char **response );
static	void *scripter( void *pv );

	int  get_size_x() { return size_x; }
	int  get_size_y() { return size_y; }
	char *get_script() { return script; }
	void write( const char *cmd ) { if ( host!=NULL ) host->write(cmd); }	
	void set_host(Fl_Host *pHost) { host = pHost; }
	Fl_Host *get_host(){ return host; } 	
};
#endif