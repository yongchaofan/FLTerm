//
// "$Id: Fl_Term.h 5222 2020-07-18 13:08:10 $"
//
// Fl_Term -- A terminal simulation widget
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
#include <FL/fl_draw.H>
#include "host.h"
#include <atomic>
#include <mutex>

#ifndef _FL_TERM_H_
#define _FL_TERM_H_
class Fl_Term : public Fl_Widget {
	char c_attr;		//current character attribute(color)
	char save_attr;		//saved character attribute, used with save_x/save_y
	char *buff;			//buffer for characters, one byte per char
	char *attr;			//buffer for attributes, one byte per char
	int buff_size; 		//current buffer size, doubles at more_chars()
	int *line;			//buffer for starting position of each line
	int line_size;		//current max number of lines, doubles at more_lines()
	int size_x; 		//screen width in number of characters
	int size_y;			//screen height in number of characters
	int cursor_x;		//index to buff and attr for current insert position
	int	cursor_y;		//index to line buffer for current row of text
	int save_x;			//save_x/save_y also used to save and restore cursor
	int save_y;			//previous cursor_y when switch to alternate screen
	int screen_y;		//the line at top of screen
	int roll_top;
	int roll_bot;		//the range of lines that will scroll in alterscreen
	int sel_left;
	int sel_right;		//begin and end of selection in scroll buffer
	float font_width;	//current font width
	int font_height;	//current font height
	int font_size;		//current font size, should equal to height
	int font_face;		//current font face
	std::atomic<bool> redraw_pending;
	std::mutex append_mtx;

	bool bEscape;		//escape sequence processing mode
	int ESC_idx;		//current index for ESC_code
	char ESC_code[32];	//cumulating the current escape sequence before process
	char tabstops[256];

	bool bInsert;		//insert mode, for inline editing for commands
	bool bGraphic;		//graphic character mode, for text mode drawing
	bool bCursor;		//display cursor or not
	bool bAppCursor;	//app cursor mode for vi
	bool bAltScreen;	//alternative screen for vi
	bool bScrollbar;	//show scrollbar when true
	bool bDragSelect;	//mouse dragged to select text, instead of scroll text
	bool bBracket;		//bracketed paste mode
	bool bWraparound;
	bool bOriginMode;

	int bTitle;			//title mode, changed through escape sequence
	int title_idx;
	char sTitle[256];	//window title set by host

	char sPrompt[32];	//wait for sPrompt before next command when scripting
	int iPrompt;		//length of sPrompt
	bool bPrompt;		//if sPrompt was found after the last append

	int iTimeOut;		//time out in seconds while waiting for sPrompt
	int recv0;			//cursor_x at the start of last command
	int xmlIndent;		//used by putxml
	int xmlTagIsOpen;	//used by putxml

	bool bDND;			//if a FL_PASTE is result of drag&drop
	bool bWait;			//waitfor() function is waiting for string in buffer
	bool bEcho;			//if local echo is active
	bool bScriptRun;
	bool bScriptPause;

	std::atomic<bool> bGets;//gets() function is waiting for return bing pressed
	std::atomic<bool> bReturn;//true if return has been pressed during gets()
	int cursor;			//gets receive buffer index
	char keys[64];		//gets receive buffer
	bool bPassword;		//if gets() is wating for password, no echo if yes

	FILE *fpLogFile;
	HOST *host;

protected:
	void draw();
	void next_line();
	void buff_clear(int offset, int len);
	void termsize(int cols, int rows);
	void screen_clear(int m0);
	void check_cursor_y();
	void append( const char *buf, int len );
	void put_xml(const char *buf, int len);
	const unsigned char *vt100_Escape(const unsigned char *buf, int cnt);
	const unsigned char *telnet_options(const unsigned char *buf, int cnt);

public:
	Fl_Term(int X,int Y,int W,int H,const char* L=0);
	~Fl_Term();
	void clear();
	int  handle(int e);
	void resize(int X, int Y, int W, int H);
	void textfont(Fl_Font fontface);
	void textsize(int fontsize);
	bool pending(){ return redraw_pending; }
	void pending(bool p) { redraw_pending=p; }
	const char *title() { return sTitle; }
	const char *hostname() { return host->name(); }

	int logg() { return fpLogFile!=NULL; }
	int echo() { return bEcho; }
	int sizeX() { return size_x; }
	int sizeY() { return size_y; }
	void echo(int e) { bEcho = e; }
	void logg(const char *fn);
	void save(const char *fn);
	void srch(const char *word);

	int connect(HOST *newhost, const char **preply);
	bool live() { return host->live(); }
	void puts(const char *buf, int len);
	void write(const char *buf, int len);
	char *gets(const char *prompt, int echo);
	void disconn();
	void disp(const char *buf) { append(buf, strlen(buf)); }
	void send(const char *buf) { write(buf, strlen(buf)); }

	void learn_prompt();
	int  mark_prompt();
	int  waitfor_prompt();
	int command(const char *cmd, const char **preply);


	void copier(char *files);
	void scripter(char *cmds);
	void run_script(const char *script);
	bool script_running() { return bScriptRun; }
	bool pause_script();
	void quit_script();
};
#endif