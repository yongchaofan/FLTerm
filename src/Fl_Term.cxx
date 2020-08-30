//
// "$Id: Fl_Term.cxx 37643 2020-08-31 10:08:20 $"
//
// Fl_Term -- A terminal simulator widget
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
#include <thread>
#include "Fl_Term.h"
#include <FL/fl_ask.H>
#include <FL/filename.H>

//defined in tiny2.cxx, returns false if editor is hiden
bool show_editor(int x, int y, int w, int h);
HOST *host_new(const char *hostname);

#ifndef WIN32 
#include <unistd.h>		// needed for usleep
#define Sleep(x) usleep((x)*1000)
#endif
#ifdef __APPLE__
#define FL_CMD FL_META
#else
#define FL_CMD FL_ALT
#endif

void host_cb(void *data, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)data;
	term->puts(buf, len);
}
char *host_cb1(void *data, const char *prompt, bool echo)
{
	Fl_Term *term = (Fl_Term *)data;
	return term->gets(prompt, echo);
}
int Fl_Term::connect(HOST *newhost, const char **preply )
{
	int rc = 0;
	if ( host->live() ) return rc;
	delete host;

	host = newhost;
	strncpy(sTitle, host->name(), 40);
	sTitle[40]=0;
	copy_label(sTitle);
	host->callback(host_cb, host_cb1, this);

	bGets = false;
	mark_prompt();
	host->connect();
	if ( preply!=NULL ) {	//waitfor prompt if called from script
		*preply = buff+recv0;	//no wait if called from edit line
		rc = waitfor_prompt();
	}
	return rc;
}
void Fl_Term::puts( const char *buf, int len )	//parse text received from host
{
	if ( len==0 ) {//Connected, send term size
		host->send_size(size_x, size_y);
		do_callback(this, (void *)sTitle);
	}
	else
		if ( len>0 ) {//data from host, display
			if ( host->type()==HOST_CONF )
				put_xml(buf, len);
			else
				append(buf, len);
		}
		else {//len<0 Disconnected, or failure
			if ( *buf ) {
				disp("\033[31m\r\n");
				disp(buf);
				disp("\033[37m, Press \033[33mEnter");
				disp("\033[37m to reconnect\r\n");
			}
			*sTitle = 0;
			do_callback(this, (void *)NULL);
		}
}
void Fl_Term::write(const char *buf, int len) //send text to host
{ 
	if ( host->live() ) {
		if ( !bGets ) {
			if ( bEcho ) append(buf, len);
			host->write(buf, len);
			return;
		}
		for ( int i=0; i<len&&bGets; i++ ) {
			switch(buf[i]) {
				case '\177':
				case '\b':
					if ( cursor>0 ) {
						cursor--;
						if ( !bPassword ) append("\010 \010", 3);
					}
					break;
				case '\r': 
					keys[cursor++]=0;
					append("\r\n", 2);
					bReturn=true;
				case '\t':
					break;
				default:
					keys[cursor]=buf[i];
					if ( ++cursor>63 ) cursor=63;
					if ( !bPassword ) append(buf+i, 1);
			}
		}
	}
	else {
		if ( *buf=='\r' ) host->connect();
	}
}
char *Fl_Term::gets(const char *prompt, int echo)	//get user input for host
{
	disp(prompt);
	cursor=0;
	bGets = true;
	bReturn = false;
	bPassword = !echo;
	int old_cursor = cursor;
	for ( int i=0; i<600&&bGets&&(!bReturn); i++ ) {
		if ( cursor>old_cursor ) { 
			old_cursor=cursor;
			i=0;
		}
		Sleep(100);
	}
	bGets = false;
	return bReturn?keys:NULL;
}
void Fl_Term::disconn()
{
	bGets = false;
	host->disconn();
}

Fl_Term::Fl_Term(int X,int Y,int W,int H,const char *L) : Fl_Widget(X,Y,W,H,L)
{
	bEcho = false;
	bScrollbar = false;
	host = new HOST();

	*sTitle = 0;
	strcpy(sPrompt, "> ");
	iPrompt = 2;
	iTimeOut = 30;
	bDND = false;
	bScriptRun = bScriptPause = false;
	fpLogFile = NULL;

	line = NULL;
	buff = attr = NULL;
	clear();

	textfont(FL_COURIER);
	textsize(16);
	size_x = w()/font_width;
	size_y = h()/font_height;
	roll_top = 0;
	roll_bot = size_y-1;
	color(FL_BLACK);
}
Fl_Term::~Fl_Term()
{
	delete host;
	free(attr);
	free(buff);
	free(line);
};
void Fl_Term::clear()
{
	Fl::lock();
	line_size = 4096;
	buff_size = 4096*64;
	buff = (char *)realloc(buff, buff_size);
	attr = (char *)realloc(attr, buff_size);
	line = (int * )realloc(line, line_size*sizeof(int));
	if ( line!=NULL ) memset(line, 0, line_size*sizeof(int) );
	if ( buff!=NULL ) memset(buff, 0, buff_size);
	if ( attr!=NULL ) memset(attr, 0, buff_size);
	cursor_y = cursor_x = 0;
	screen_y = 0;
	sel_left = sel_right= 0;
	c_attr = 7;//default black background, white foreground
	recv0 = 0;
	ESC_idx = 0;
	bInsert = bEscape = bGraphic = bTitle = false;
	bBracket = bAltScreen = bAppCursor = bOriginMode = false;
	bWraparound = true;
	bScrollbar = false;
	bCursor = true;
	bPrompt = true;
	memset(tabstops, 0, 256);
	for ( int i=0; i<256; i+=8 ) tabstops[i]=1;

	xmlIndent=0;
	xmlTagIsOpen=true;
	redraw_pending=true;
	Fl::unlock();
}
void Fl_Term::resize(int X, int Y, int W, int H)
{
	Fl_Widget::resize(X,Y,W,H);
	size_x = w()/font_width;
	size_y = h()/font_height;
	roll_top = 0;
	roll_bot = size_y-1;
	if ( screen_y< cursor_y-size_y+1 )
		screen_y = cursor_y-size_y+1;
	host->send_size(size_x, size_y);
	redraw();
}
void Fl_Term::textfont(Fl_Font fontface)
{
	font_face = fontface;
	fl_font(font_face, font_size);
	font_width = fl_width("abcdefghij")/10;
	font_height = fl_height();
}
void Fl_Term::textsize(int fontsize)
{
	font_size = fontsize;
	fl_font(font_face, font_size);
	font_width = fl_width("abcdefghij")/10;
	font_height = fl_height();
}
const unsigned int VT_attr[] = {
	0x00000000, 0xc0000000, 0x00c00000, 0xc0c00000,	//0,1,2,3
	0x2060c000, 0xc000c000, 0x00c0c000, 0xc0c0c000,	//4,5,6,7
	FL_BLACK, FL_RED, FL_GREEN, FL_YELLOW,
	FL_BLUE, FL_MAGENTA, FL_CYAN, FL_WHITE
};
void Fl_Term::draw()
{	
	pending(false);
	fl_color(color());
	fl_rectf(x(),y(),w(),h());
	fl_font(font_face, font_size);

	int sel_l=sel_left, sel_r=sel_right;
	if ( sel_l>sel_r ) {
		sel_l=sel_right; sel_r=sel_left;
	}

	int ly = screen_y;
	int dx, dy=y();
	for ( int i=0; i<size_y; i++ ) {
		dx = x()+1;
		dy += font_height;
		int j = line[ly+i];
		while( j<line[ly+i+1] ) {
			int n=j;
			while (  attr[n]==attr[j] ) {
				if ( ++n==line[ly+i+1]) break;
				if ( n==sel_r || n==sel_l ) break;
			}
			unsigned int font_color = VT_attr[(int)attr[j]&0x0f];
			unsigned int bg_color = VT_attr[(int)((attr[j]>>4)&0x0f)];
			int wi = fl_width(buff+j, n-j);
			if ( j>=sel_l && j<sel_r ) {
				fl_color(selection_color());
				fl_rectf(dx, dy-font_height+4, wi, font_height);
				fl_color(fl_contrast(font_color, selection_color()));
			}
			else {
				if ( bg_color!=color() ) {
					fl_color( bg_color );
					fl_rectf(dx, dy-font_height+4, wi, font_height);
				}
				fl_color( font_color );
			}
			int m = (buff[n-1]==0x0a) ? n-1 : n;	//don't draw LF, 
			//which will result in little squares on some platforms
			fl_draw( buff+j, m-j, dx, dy );
			dx += wi;
			j=n;
		}
	}
	dx = x()+fl_width(buff+line[cursor_y], cursor_x-line[cursor_y]);
	dy = y()+(cursor_y-screen_y)*font_height;
	bool editor = bCursor;
	if ( bAltScreen) editor=false;
	if ( host->status()==HOST_AUTHENTICATING ) editor=false;
	if ( !show_editor(editor?dx:-1, dy+4, w()-dx-8, font_height) ) {
		if ( bCursor ) {
			fl_color(FL_WHITE);		//draw a white bar as cursor
			fl_rectf(dx, dy+font_height, font_width, 4);
		}
	}
	
	if ( bScrollbar) {
		fl_color(FL_DARK3);			//draw scrollbar
		fl_rectf(x()+w()-8, y(), 8, y()+h());
		fl_color(FL_RED);			//draw slider
		int slider_y = h()*screen_y/cursor_y;
		fl_rectf(x()+w()-8, y()+slider_y-8, 8, 16);
	}
}
int Fl_Term::handle(int e)
{
	switch (e) {
		case FL_LEAVE: 	//copy only when mouse leaves the term
			if ( sel_left<sel_right )
				Fl::copy(buff+sel_left, sel_right-sel_left, 1);
			//fall through
		case FL_ENTER: return 1;
		case FL_FOCUS: redraw(); return 1;
		case FL_MOUSEWHEEL:
			if ( !bAltScreen ) {
				screen_y += Fl::event_dy();
				if ( screen_y<0 ) screen_y = 0;
				if ( screen_y>cursor_y ) screen_y = cursor_y;
				bScrollbar = (screen_y < cursor_y-size_y+1);
				redraw();
			}
			return 1;
		case FL_PUSH:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x=Fl::event_x()/font_width;
				int y=Fl::event_y()-Fl_Widget::y();
				if ( Fl::event_clicks()==1 ) {	//double click to select word
					y = y/font_height + screen_y;
					sel_left = line[y]+x;
					sel_right = sel_left;
					while ( --sel_left>line[y] )
						if ( buff[sel_left]==0x0a || buff[sel_left]==0x20 ) {
							sel_left++;
							break;
						}
					while ( ++sel_right<line[y+1])
						if ( buff[sel_right]==0x0a || buff[sel_right]==0x20 ) {
							break;
						}
					redraw();
					return 1;
				}
				if ( x>=size_x-2 && bScrollbar) {//push in scrollbar area
					if ( y>0 && y<h() ) screen_y = y*cursor_y/h();
					bDragSelect = false;
					redraw();
				}
				else {								//push to start draging
					y = y/font_height + screen_y;
					sel_left = line[y]+x;
					if ( sel_left>line[y+1] ) sel_left=line[y+1] ;
					while ( (buff[sel_left]&0xc0)==0x80 ) sel_left--;
					sel_right = sel_left;
					bDragSelect = true;
				}
			}
			return 1;
		case FL_DRAG:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x = Fl::event_x()/font_width;
				int y = Fl::event_y()-Fl_Widget::y();
				if ( !bDragSelect && y>0 && y<h()) {
					screen_y = y*cursor_y/h();
				}
				else {
					if ( y<0 ) {
						screen_y += y/8;
						if ( screen_y<0 ) screen_y = 0;
					}
					if ( y>h() ) {
						screen_y += (y-h())/8;
						if ( screen_y>cursor_y ) screen_y=cursor_y;
					}
					y = y/font_height + screen_y;
					if ( y<0 ) y=0;
					if ( !bAltScreen && y>cursor_y ) y = cursor_y;
					//cursor_y may not be the last line in AlterScreen mode
					sel_right = line[y]+x;
					if ( sel_right>line[y+1] ) sel_right=line[y+1];
					while ( (buff[sel_right]&0xc0)==0x80 ) sel_right++;
				}
				redraw();
			}
			return 1;
		case FL_RELEASE:
			switch ( Fl::event_button() ) {
			case FL_LEFT_MOUSE:				//left button drag to copy
				if ( sel_left>sel_right ) {
					int t=sel_left; sel_left=sel_right; sel_right=t;
				}
				if ( sel_left==sel_right ) redraw();//clear selection
				break;
			case FL_RIGHT_MOUSE:			//middle click to paste
				if ( sel_left<sel_right ) 	//from selection
					write(buff+sel_left, sel_right-sel_left);
				else 						//or from clipboard
					Fl::paste(*this, 1);
				break;
			}
			return 1;
		case FL_DND_RELEASE: bDND = true;
		case FL_DND_ENTER:
		case FL_DND_DRAG:
		case FL_DND_LEAVE:  return 1;
		case FL_PASTE:
			if ( bDND ) {		//drag and drop run as script
				run_script(Fl::event_text());
			}
			else {				//or paste to send to host
				if ( bBracket ) host->write( "\033[200~", 6 );
				write(Fl::event_text(),Fl::event_length());
				if ( bBracket ) host->write( "\033[201~", 6 );
			}
			bDND = false;
			return 1;
		case FL_SHORTCUT:
		case FL_KEYDOWN:
			if ( Fl::event_state(FL_CMD) ) break;	//don't handle CMD+key
#ifdef __APPLE__
			int del;
			if ( Fl::compose(del) ) {
				int y = (cursor_y-screen_y+1)*font_height;
				int x = (cursor_x-line[cursor_y])*font_width;
				Fl::insertion_point_location(x,y,font_height);
				for ( int i=0; i<del; i++ ) write("\177",1);
			}
#endif
			int key = Fl::event_key();
			switch (key) {
			case FL_Page_Up:
				if ( !bAltScreen ) {
					bScrollbar = true;
					screen_y -= size_y-1;
					if ( screen_y<0 ) screen_y = 0;
					redraw();
				}
				break;
			case FL_Page_Down:
				if ( !bAltScreen ) {
					screen_y += size_y-1;
					if ( screen_y>cursor_y-size_y ) bScrollbar = false;
					if ( screen_y>cursor_y ) screen_y = cursor_y;
					redraw();
				}
				break;
			case FL_Up:	  host->write(bAppCursor?"\033OA":"\033[A",3); break;
			case FL_Down: host->write(bAppCursor?"\033OB":"\033[B",3); break;
			case FL_Right:host->write(bAppCursor?"\033OC":"\033[C",3); break;
			case FL_Left: host->write(bAppCursor?"\033OD":"\033[D",3); break;
			case FL_BackSpace: write("\177", 1); break;
			case FL_Pause: pause_script(); break;
			case FL_Enter:
			default:
				write(Fl::event_text(), Fl::event_length());
				if ( screen_y < cursor_y-size_y+1 )
					screen_y = cursor_y-size_y+1;
			}
			return 1;
	}
	return Fl_Widget::handle(e);
}
void Fl_Term::next_line()
{
	line[++cursor_y]=cursor_x;
	if ( screen_y==cursor_y-size_y ) screen_y++;
	if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;

	if ( cursor_x>buff_size-1024 || cursor_y>line_size-3 ) {
		Fl::lock();
		if ( line_size<65536 ) {	//double buffer size till 64k lines
			char *old_buff = buff;
			char *old_attr = attr;
			int *old_line = line;
			buff = (char *)realloc(buff, buff_size*2);
			attr = (char *)realloc(attr, buff_size*2);
			line = (int  *)realloc(line, (line_size*2)*sizeof(int));
			if ( buff!=NULL && attr!=NULL && line!=NULL ) {
				memset(line+line_size, 0, line_size*sizeof(int));
				memset(buff+buff_size, 0, buff_size);
				memset(attr+buff_size, 0, buff_size);
				buff_size*=2;
				line_size*=2;
			}
			else {//clear buffer if failed to double
				if ( attr==NULL ) attr = old_attr;
				if ( buff==NULL ) buff = old_buff;
				if ( line==NULL ) line = old_line;
				clear();
			}
		}
		else {						//erase half of buffer at 64k lines
			int middle = line[32768];
			for ( int i=32768; i<cursor_y+2; i++ ) line[i]-=middle;
			memmove(line, line+32768, 32768*sizeof(int));
			memset(line+32768, 0, 32768*sizeof(int));
			screen_y-=32768; if ( screen_y<0 ) screen_y=0;
			cursor_y-=32768;
			cursor_x-=middle;
			recv0-=middle; if ( recv0<0 ) recv0=0;
			memmove(attr, attr+middle, line[cursor_y+1]);
			memset(attr+line[cursor_y+1], 0, 65536*64-line[cursor_y+1]);
			memmove(buff, buff+middle, line[cursor_y+1]);
			memset(buff+line[cursor_y+1], 0, 65536*64-line[cursor_y+1]);
		}
		Fl::unlock();
	}
}
void Fl_Term::append( const char *newtext, int len )
{
	const unsigned char *p = (const unsigned char *)newtext;
	const unsigned char *zz = p+len;
	
	append_mtx.lock();	//only one thread can append to buffer at a time
	if ( fpLogFile!=NULL ) fwrite( newtext, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p, zz-p );
	while ( p < zz ) {
		unsigned char c=*p++;
		if ( bTitle ) {
			if ( c==0x07 ) {
				bTitle = false;
				sTitle[title_idx]=0;
				do_callback( this, (void *)sTitle );
			}
			else {
				if ( title_idx<128 ) sTitle[title_idx++] = c;
			}
			continue;
		}
		switch ( c ) {
			case 0x00:
			case 0x0e:
			case 0x0f:	break;
			case 0x07:	fl_beep(FL_BEEP_DEFAULT); break;
			case 0x08:
				if ( cursor_x>line[cursor_y] ) {
					if ( (buff[cursor_x--]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[cursor_x]&0xc0)==0x80 )
							cursor_x--;
				}
				break;
			case 0x09:{
				int l;
				do {
					attr[cursor_x]=c_attr;
					buff[cursor_x++]=' ';
				 	l=cursor_x-line[cursor_y];
				} while ( l<=size_x && tabstops[l]==0 );
			}
					break;
			case 0x0a:
			case 0x0b:
			case 0x0c:
				if ( bAltScreen || line[cursor_y+2]!=0 ) { //IND to next line
						vt100_Escape((unsigned char *)"D", 1);
				}
				else {	//LF and newline
					cursor_x = line[cursor_y+1]	;
					attr[cursor_x] = c_attr;
					buff[cursor_x++] = 0x0a;
					next_line();
				}
				break;
			case 0x0d:
				if ( cursor_x-line[cursor_y]==size_x+1 && *p!=0x0a )
					next_line();//soft line feed
				else
					cursor_x = line[cursor_y];
				break;
			case 0x1b:
				p = vt100_Escape(p, zz-p);
				break;
			case 0xff:
				p = telnet_options(p-1, zz-p+1);
				break;
		case 0xe2:
			if ( bAltScreen ) {//utf8 box drawing hack
				c = ' ';
				if ( *p++==0x94 ) {
					switch ( *p ) {
						case 0x80:
						case 0xac:
						case 0xb4:
						case 0xbc: c='_'; break;
						case 0x82:
						case 0x94:
						case 0x98:
						case 0x9c:
						case 0xa4: c='|'; break;
						}
					}
					p++;
				}//fall through
		default:
			if ( bGraphic ) {
				switch ( c ){//charset 2 box drawing
					case 'q': c='_'; break;
					case 'x': c='|';
					case 't':
					case 'u':
					case 'm':
					case 'j': c='|'; break;
					case 'l':
					case 'k': c=' '; break;
					default: c = '?';
				}
			}
			if ( bInsert )		//insert one space
				vt100_Escape((unsigned char *)"[1@",3);
			if ( cursor_x-line[cursor_y]>=size_x ) {
				int char_cnt = 0;
				for ( int i=line[cursor_y]; i<cursor_x; i++ )
					if ( (buff[i]&0xc0)!=0x80 ) char_cnt++;
				if ( char_cnt==size_x ) {
					if ( bWraparound  )
					next_line();
					else
						cursor_x--;
				}
			}
			attr[cursor_x] = c_attr;
			buff[cursor_x++] = c;
			if ( line[cursor_y+1]<cursor_x )
				line[cursor_y+1]=cursor_x;
		}
	}
	if ( !bPrompt && cursor_x>iPrompt ) {
		char *p=buff+cursor_x-iPrompt;
		if ( strncmp(p, sPrompt, iPrompt)==0 ) bPrompt=true;
	}
	pending(true);
	append_mtx.unlock();
}
void Fl_Term::buff_clear(int offset, int len)
{
	memset(buff+offset, ' ', len);
	memset(attr+offset,   7, len);
}
/*[2J, mostly used after [?1049h to clear screen
  and when screen size changed during vi or raspi-config
  flashwave TL1 use it without [?1049h for splash screen
  freeBSD use it without [?1049h* for top and vi
*/
void Fl_Term::screen_clear(int m0)
{
	int lines = size_y;
	if ( m0==2 ) screen_y = cursor_y;
	if ( m0==1 ) {
		lines = cursor_y-screen_y;
		buff_clear(line[cursor_y], cursor_x-line[cursor_y]+1);
		cursor_y = screen_y;
	}
	if ( m0==0 ) {
		buff_clear(cursor_x, line[cursor_y+1]-cursor_x);
		lines = screen_y+size_y-cursor_y;
	}
	cursor_x = line[cursor_y];
	int cy = cursor_y;
	for ( int i=0; i<lines; i++ ) {
		buff_clear(cursor_x, size_x);
		cursor_x += size_x;
		next_line();
	}
	cursor_y = cy;
	if ( m0==2 || m0==0 ) screen_y--;
	cursor_x = line[cursor_y];
}
void Fl_Term::check_cursor_y()
{
	if ( cursor_y< screen_y )
		cursor_y = screen_y;
	if ( cursor_y> screen_y+size_y-1 )
		cursor_y = screen_y+size_y-1;
	if ( bOriginMode ) {
		if ( cursor_y<screen_y+roll_top )
			cursor_y = screen_y+roll_top;
		if ( cursor_y>screen_y+roll_bot )
			cursor_y = screen_y+roll_bot;
	}
}
void Fl_Term::termsize(int cols, int rows)
{
	if ( size_x!=cols || size_y!=rows ) {
		size_x=cols; size_y=rows;
		screen_clear(2);
		do_callback(this, (void *)sTitle);	//trigger window resizing
	}
}
const unsigned char *Fl_Term::vt100_Escape(const unsigned char *sz, int cnt)
{
	const unsigned char *zz = sz+cnt;
	bEscape = true;
	while ( sz<zz && bEscape ){
		if ( *sz>31 )
			ESC_code[ESC_idx++] = *sz++;
		else {
			switch ( *sz++ ) {
			case 0x08:	//BS
					if ( (buff[cursor_x--]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x--;
					break;
			case 0x0b: {//VT
					int x = cursor_x-line[cursor_y];
					cursor_x = line[++cursor_y]+x;
					break;
					}
			case 0x0d:	//CR
					cursor_x = line[cursor_y];
					break;
			}
		}
		switch( ESC_code[0] ) {
		case '[':
			if ( isalpha(ESC_code[ESC_idx-1])
				|| ESC_code[ESC_idx-1]=='@'
				|| ESC_code[ESC_idx-1]=='`' ) {
				bEscape = false;
				int m0=0;	//used by [PsJ and [PsK
				int n0=1;	//used by most, e.g. [PsA [PsB
				int n1=1;	//n1;n0 used by [Ps;PtH [Ps;Ptr
				if ( isdigit(ESC_code[1]) ) {
					m0 = n0 = atoi(ESC_code+1);
					if ( n0==0 ) n0=1;
				}
				char *p = strchr(ESC_code,';');
				if ( p != NULL ) {
					n1 = n0;
					n0 = atoi(p+1);
					if ( n0==0 ) n0=1;	//ESC[0;0f == ESC[1;1f
				}
				int x;
				switch ( ESC_code[ESC_idx-1] ) {
				case 'A': //cursor up n0 times
					x = cursor_x-line[cursor_y];
					cursor_y -=n0;
					check_cursor_y();
					cursor_x = line[cursor_y]+x;
					break;
				case 'd'://line position absolute
					x = cursor_x-line[cursor_y];
					if ( n0>size_y ) n0 = size_y;
					cursor_y = screen_y+n0-1;
					cursor_x = line[cursor_y]+x;
					break;
				case 'e': //line position relative
				case 'B': //cursor down n0 times
					x = cursor_x-line[cursor_y];
					cursor_y += n0;
					check_cursor_y();
					cursor_x = line[cursor_y]+x;
					break;
				case '`': //character position absolute
				case 'G': //cursor to n0th position from left
					cursor_x = line[cursor_y];
					//fall through
				case 'a': //character position relative
				case 'C': //cursor forward n0 times
					while ( n0-->0 && cursor_x<line[cursor_y]+size_x-1 ) {
						if ( (buff[++cursor_x]&0xc0)==0x80 )
							while ( (buff[++cursor_x]&0xc0)==0x80 );
					}
					break;
				case 'D': //cursor backward n0 times
					while ( n0-->0 && cursor_x>line[cursor_y] ) {
						if ( (buff[--cursor_x]&0xc0)==0x80 )
							while ( (buff[--cursor_x]&0xc0)==0x80 );
					}
					break;
				case 'E': //cursor to begining of next line n0 times
					cursor_y += n0;
					check_cursor_y();
					cursor_x = line[cursor_y];
					break;
				case 'F': //cursor to begining of previous line n0 times
					cursor_y -= n0;
					check_cursor_y();
					cursor_x = line[cursor_y];
					break;
				case 'f': //horizontal/vertical position forced, apt install
					for ( int i=cursor_y+1; i<screen_y+n1; i++ )
						if ( i<line_size && line[i]<cursor_x )
							line[i] = cursor_x;
					//fall through
				case 'H': //cursor to line n1, postion n0
					if ( !bAltScreen && n1>size_y ) {
						cursor_y = (screen_y++) + size_y;
					}
					else {
						cursor_y = screen_y+n1-1;
						if ( bOriginMode ) cursor_y+=roll_top;
						check_cursor_y();
					}
					cursor_x = line[cursor_y];
					while ( --n0>0 ) {
						cursor_x++;
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x++;
					}
					break;
				case 'J': //[0J kill till end, 1J begining, 2J entire screen
					if ( isdigit(ESC_code[1]) || bAltScreen ) {
						screen_clear(m0);
					}
					else {//clear in none alter screen, used in apt install
						line[cursor_y+1] = cursor_x;
						for (int i=cursor_y+2; i<=screen_y+size_y+1; i++)
							if ( i<line_size ) line[i] = 0;
					}
					break;
				case 'K': {//[K erase till line end, 1K begining, 2K entire line
						int a=line[cursor_y];
						int z=line[cursor_y+1];
						if ( m0==0 ) a = cursor_x;
						if ( m0==1 ) z = cursor_x+1;
						if ( z>a ) buff_clear(a, z-a);
					}
					break;
				case 'L': //insert n0 lines
					if ( n0 > screen_y+roll_bot-cursor_y )
						n0 = screen_y+roll_bot-cursor_y+1;
					else
						for ( int i=screen_y+roll_bot; i>=cursor_y+n0; i-- ) {
							memcpy( buff+line[i], buff+line[i-n0], size_x );
							memcpy( attr+line[i], attr+line[i-n0], size_x );
						}
					cursor_x = line[cursor_y];
					buff_clear(cursor_x, size_x*n0);
					break;
				case 'M': //delete n0 lines
					if ( n0 > screen_y+roll_bot-cursor_y )
						n0 = screen_y+roll_bot-cursor_y+1;
					else
						for ( int i=cursor_y; i<=screen_y+roll_bot-n0; i++ ) {
							memcpy( buff+line[i], buff+line[i+n0], size_x);
							memcpy( attr+line[i], attr+line[i+n0], size_x);
						}
					cursor_x = line[cursor_y];
					buff_clear(line[screen_y+roll_bot-n0+1], size_x*n0);
					break;
				case 'P': //delete n0 characters
					for ( int i=cursor_x+n0; i<line[cursor_y+1]; i++ ) {
						buff[i-n0]=buff[i];
						attr[i-n0]=attr[i];
					}
					buff_clear(line[cursor_y+1]-n0, n0);
					if ( !bAltScreen ) {
						line[cursor_y+1]-=n0;
						if ( line[cursor_y+1]<line[cursor_y] )
							line[cursor_y+1] =line[cursor_y];
					}
					break;
				case '@': //insert n0 spaces
					for ( int i=line[cursor_y+1]-n0-1; i>=cursor_x; i-- ){
						buff[i+n0]=buff[i];
						attr[i+n0]=attr[i];
					}
					if ( !bAltScreen ) {
						line[cursor_y+1]+=n0;
						if ( line[cursor_y+1]>line[cursor_y]+size_x )
							line[cursor_y+1] =line[cursor_y]+size_x;
					}//fall through
				case 'X': //erase n0 characters
					buff_clear(cursor_x, n0);
					break;
				case 'I': //cursor forward n0 tab stops
					break;
				case 'Z': //cursor backward n0 tab stops
					break;
				case 'S': // scroll up n0 lines
					for ( int i=roll_top; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i+n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i+n0], size_x);
					}
					buff_clear(line[screen_y+roll_bot-n0+1], n0*size_x);
					break;
				case 'T': // scroll down n0 lines
					for ( int i=roll_bot; i>=roll_top+n0; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x);
					}
					buff_clear(line[screen_y+roll_top], n0*size_x);
					break;
				case 'c': // send device attributes
					send("\033[?1;2c");		//vt100 with options
					break;
				case 'g': // set tabstops
					if ( m0==0 ) { //clear current tab
						tabstops[cursor_x-line[cursor_y]] = 0;
					}
					if ( m0==3 ) { //clear all tab stops
						memset(tabstops, 0, 256);
					}
					break;
				case 'h':
					if ( ESC_code[1]=='4' ) bInsert=true;
					if ( ESC_code[1]=='?' ) {
						switch( atoi(ESC_code+2) ) {
						case 1: bAppCursor = true; 	break;
						case 3:	termsize(132, 25);  break;
						case 6: bOriginMode = true; break;
						case 7: bWraparound = true; break;
						case 25:	bCursor = true; break;
						case 2004: bBracket = true; break;
						case 1049: bAltScreen = true;//?1049h alternate screen
								screen_clear(2);
						}
					}
					break;
				case 'l':
					if ( ESC_code[1]=='4' ) bInsert=false;
					if ( ESC_code[1]=='?' ) {
						switch( atoi(ESC_code+2) ) {
						case 1: bAppCursor = false; break;
						case 3:	termsize(80, 25);   break;
						case 6: bOriginMode= false; break;
						case 7: bWraparound= false; break;
						case 25:	bCursor= false; break;
						case 2004: bBracket= false; break;
						case 1049: bAltScreen= false;//?1049l alternate screen
								cursor_y = screen_y;
								cursor_x = line[cursor_y];
								for ( int i=1; i<=size_y+1; i++ )
									line[cursor_y+i] = 0;
								screen_y = cursor_y-size_y+1;
								if ( screen_y<0 ) screen_y = 0;
						}
					}
					break;
				case 'm': {//text style, color attributes
						char *p = ESC_code;
						while ( p!=NULL ) {
							m0 = atoi(++p);
							switch ( m0/10 ) {
							case 0: if ( m0==0 ) c_attr = 7;	//normal
									if ( m0==1 ) c_attr|=0x08;	//bright
									if ( m0==7 ) c_attr =0x70;	//negative
									break;
							case 2: c_attr = 7; 				//normal
									break;
							case 3: if ( m0==39 ) m0 = 7;//default foreground
									c_attr = (c_attr&0xf8)+m0%10;
									break;
							case 4: if ( m0==49 ) m0 = 0;//default background
									c_attr = (c_attr&0x0f)+((m0%10)<<4);
									break;
							case 9: c_attr = (c_attr&0xf0) + m0%10 + 8;
									break;
							case 10:c_attr = (c_attr&0x0f) + ((m0%10+8)<<4);
									break;
							}
							p = strchr(p, ';');
						}
					}
					break;
				case 'r': //set margins and move cursor to home
					if ( n1==1 && n0==1 ) n0=size_y;	//ESC[r
					roll_top=n1-1; roll_bot=n0-1;
					cursor_y = screen_y;
					if ( bOriginMode ) cursor_y+=roll_top;
					cursor_x = line[cursor_y];
					break;
				case 's': //save cursor
					save_x = cursor_x-line[cursor_y];
					save_y = cursor_y-screen_y;
					break;
				case 'u': //restore cursor
					cursor_y = save_y+screen_y;
					cursor_x = line[cursor_y]+save_x;
					break;
				}
			}
			break;
		case '7': //save cursor
			save_x = cursor_x-line[cursor_y];
			save_y = cursor_y-screen_y;
			save_attr = c_attr;
			bEscape = false;
			break;
		case '8': //restore cursor
			cursor_y = save_y+screen_y;
			cursor_x = line[cursor_y]+save_x;
			c_attr = save_attr;
			bEscape = false;
			break;
		case 'F': //cursor to lower left corner
			cursor_y = screen_y+size_y-1;
			cursor_x = line[cursor_y];
			bEscape = false;
			break;
		case 'E': //move to next line
			cursor_x = line[++cursor_y];
			bEscape = false;
			break;
		case 'D': //move/scroll up one line
			if ( cursor_y<screen_y+roll_bot ) {	//move
				int x = cursor_x-line[cursor_y];
				cursor_x = line[++cursor_y]+x;
			}
			else {								//scroll
				int len = line[screen_y+roll_bot+1]-line[screen_y+roll_top+1];
				int x = cursor_x-line[cursor_y];
				memcpy(buff+line[screen_y+roll_top], 
						buff+line[screen_y+roll_top+1], len);
				memcpy(attr+line[screen_y+roll_top], 
						attr+line[screen_y+roll_top+1], len);
				len = line[screen_y+roll_top+1]-line[screen_y+roll_top];
				for ( int i=roll_top+1; i<=roll_bot; i++ )
					line[screen_y+i] = line[screen_y+i+1]-len;
				buff_clear(line[screen_y+roll_bot], 
					line[screen_y+roll_bot+1]-line[screen_y+roll_bot]);
				cursor_x = line[cursor_y]+x;
			}
			bEscape = false;
			break;
		case 'M': //move/scroll down one line
			if ( cursor_y>screen_y+roll_top ) {	// move
				int x = cursor_x-line[cursor_y];
				cursor_x = line[--cursor_y]+x;
			}
			else {								//scroll
				for ( int i=roll_bot; i>roll_top; i-- ) {
					memcpy(buff+line[screen_y+i],buff+line[screen_y+i-1],size_x);
					memcpy(attr+line[screen_y+i],attr+line[screen_y+i-1],size_x);
				}
				buff_clear(line[screen_y+roll_top], size_x);
			}
			bEscape = false;
			break;
		case 'H': //set tabstop
			tabstops[cursor_x-line[cursor_y]] = 1;
			bEscape = false;
			break;
		case ']': //set window title
			if ( ESC_code[ESC_idx-1]==';' ) {
				if ( ESC_code[1]=='0' ) {
					bTitle = true;
					title_idx = 0;
				}
				bEscape = false;
			}
			break;
		case ')':
		case '(': //character sets, 0 for line drawing
			if ( ESC_idx==2 ) {
				bGraphic = (ESC_code[1]=='0');
				bEscape = false;
			}
			break;
		case '#':
			if ( ESC_idx==2 ) {
				if ( ESC_code[1]=='8' )
					memset(buff+line[screen_y], 'E', size_x*size_y);
				bEscape = false;
			}
			break;
		default: bEscape = false;
		}
		if ( ESC_idx==31 ) bEscape = false;
		if ( !bEscape ) { ESC_idx=0; memset(ESC_code, 0, 32); }
	}
	return sz;
}
void Fl_Term::logg(const char *fn)
{
	if ( fpLogFile!=NULL ) {
		fclose( fpLogFile );
		fpLogFile = NULL;
		disp("\r\n\033[32m***Log file closed***\033[37m\r\n");
	}
	else {
		fpLogFile = fl_fopen(fn, "wb");
		if ( fpLogFile != NULL ) {
			disp("\r\n\033[32m***");
			disp(fn);
			disp(" opened for logging***\033[37m\r\n");
		}
	}
}
void Fl_Term::save(const char *fn)
{
	FILE *fp = fl_fopen(fn, "wb");
	if ( fp!=NULL ) {
		for (int i=0; i<cursor_x; i+=8192) {
			int len = i+8192<cursor_x ? 8192 : cursor_x-i;
			fwrite(buff+i, 1, len, fp);
		}
		fclose(fp);
		char msg[256];
		sprintf(msg, "\r\n\033[32m***%d bytes saved to %s***\03337m\r\n",
				cursor_x, fn);
		disp(msg);
	}
}
void Fl_Term::srch(const char *sstr)
{
	int l = strlen(sstr);
	char *p = buff+sel_left;
	if ( sel_left==sel_right ) p = buff+cursor_x;
	while ( --p>=buff+l ) {
		int i;
		for ( i=l-1; i>=0; i-- )
			if ( sstr[i]!=p[i-l] ) break;
		if ( i==-1 ) {
			sel_left = p-l-buff;
			sel_right = sel_left+l;
			while ( line[screen_y]>sel_left ) screen_y--;
			break;
		}
	}
	if ( p<buff+l ) sel_left = sel_right = 0;
	redraw();
}
void Fl_Term::learn_prompt()
{//capture prompt for scripting
	if ( cursor_x>1 ) {
		sPrompt[0] = buff[cursor_x-2];
		sPrompt[1] = buff[cursor_x-1];
		sPrompt[2] = 0;
		iPrompt = 2;
	}
}
int Fl_Term::mark_prompt()
{
	bPrompt = false;
	return recv0=cursor_x;
}
int Fl_Term::waitfor_prompt()
{
	int oldlen = recv0;
	for ( int i=0; i<iTimeOut*10 && !bPrompt; i++ ) {
		Sleep(100);
		if ( cursor_x>oldlen ) { i=0; oldlen=cursor_x; }
	}
	bPrompt = true;
	return cursor_x - recv0;
}
int Fl_Term::command(const char *cmd, const char **preply)
{
	int rc = 0;
	if ( *cmd!='!' ) {
		if ( live() ) {
			mark_prompt();
			send(cmd);
			send("\r");
			rc = waitfor_prompt();
			if ( preply!=NULL ) *preply = buff+recv0;
		}
		else {
			disp(cmd);
			disp("\n");
		}
	}
	else {
		const char *p = strchr(++cmd, ' ');	//p points to cmd parameter
		if ( p==NULL ) p="";
		while (*p==' ') p++;
		
		if ( strncmp(cmd,"Clear",5)==0 ) clear();
		else if ( strncmp(cmd,"Log",3)==0 ) logg( p );
		else if ( strncmp(cmd,"Wait",4)==0 ) Sleep(atoi(p)*1000);
		else if ( strncmp(cmd,"Disp",4)==0 ) {
			mark_prompt();
			disp(p);
		}
		else if ( strncmp(cmd,"Send",4)==0 ) {
			mark_prompt();
			send(p);
		}
		else if ( strncmp(cmd,"Recv",4)==0 ) {
			if ( preply!=NULL ) *preply = buff+recv0;
			rc = cursor_x-recv0;
			recv0 = cursor_x;
		}
		else if ( strncmp(cmd,"Copy",4)==0 ) {
			Fl::copy(buff, cursor_x, 1);
		}
		else if ( strncmp(cmd,"Hostname",8)==0 ) {
			if ( preply!=NULL && live() ) {
				*preply = (char *)host->name();
				rc = strlen(*preply);
			}
		}
		else if ( strncmp(cmd,"Selection",9)==0) {
			if ( preply!=NULL ) *preply = buff+sel_left;
			rc = sel_right-sel_left;
		}
		else if ( strncmp(cmd,"Timeout",7)==0 ) iTimeOut = atoi(p);
		else if ( strncmp(cmd,"Prompt", 6)==0 ) {
			if ( cmd[6]==' ' ) {
				strncpy(sPrompt, cmd+7, 31);
				sPrompt[31] = 0;
				fl_decode_uri(sPrompt);
				iPrompt = strlen(sPrompt);
			}
			else 
				learn_prompt();
			if ( preply!=NULL ) *preply = sPrompt;
			rc = iPrompt;
		}
		else if ( strncmp(cmd,"scp",3)==0
				||strncmp(cmd,"tun",3)==0 
				||strncmp(cmd,"xmodem",6)==0 ) {
			mark_prompt();
			host->command(cmd);
			if ( preply!=NULL ) {
				*preply = buff+recv0;
				rc = waitfor_prompt();
			}
		}
		else {
			HOST *host = host_new(cmd);
			if ( host!=NULL )
				rc = connect(host, preply);
		}
	}
	return rc;
}
void Fl_Term::put_xml(const char *buf, int len)
{
	const char *p=buf, *q;
	const char spaces[256]="\r\n                                               \
                                                                              ";
	if ( strncmp(buf, "<?xml ", 6)==0 ) {
		xmlIndent = 0;
		xmlTagIsOpen = true;
	}
	while ( *p!=0 && *p!='<' ) p++;
	if ( p>buf ) append(buf, p-buf);
	while ( *p!=0 && p<buf+len ) {
		while (*p==0x0d || *p==0x0a || *p=='\t' || *p==' ') p++;
		if ( *p==']' && p+6<=buf+len) {//end of message
			if ( strncmp(p, "]]>]]>", 6)==0 ) {
				append("]]>]]>\n\033[37m", 12);
				p+=6;
			}
		}
		else if ( *p=='<' ) { //tag
			if ( p[1]=='/' ) {
				if ( !xmlTagIsOpen ) {
					xmlIndent -= 2;
					append(spaces, xmlIndent);
				}
				xmlTagIsOpen = false;
			}
			else {
				if ( xmlTagIsOpen ) xmlIndent+=2;
				append(spaces, xmlIndent);
				xmlTagIsOpen = true;
			}
			append("\033[32m",5);
			q = strchr(p, '>');
			if ( q==NULL ) q = p+strlen(p);
			const char *r = strchr(p, ' ');
			if ( r!=NULL && r<q ) {
				append(p, r-p);
				append("\033[34m",5);
				append(r, q-r);
			}
			else
				append(p, q-p);
			append("\033[32m>",6);
			p = q;
			if ( *q=='>' ) {
				p++;
				if ( q[-1]=='/' ) xmlTagIsOpen = false;
			}
		}
		else {		//data
			append("\033[33m",5);
			q = strchr(p, '<');
			if ( q==NULL ) q = p+strlen(p);
			append(p, q-p);
			p = q;
		}
	}
}
void Fl_Term::copier(char *files)
{
	bScriptRun = true; bScriptPause = false;
	char dst[256]="";
	if ( host->type()==HOST_SSH ) {
		const char *p1, *p2;
		command("pwd", &p2);
		p1 = strchr(p2, 0x0a);
		if ( p1!=NULL ) {
			p2 = p1+1;
			p1 = strchr(p2, 0x0a);
			if ( p1!=NULL ) {
				strncpy(dst, p2, p1-p2);
				dst[p1-p2]='/';
				if ( p1-p2<255 ) 
					dst[p1-p2+1]=0;
				else
					dst[255]=0;
			}
		}
	}
	char *p = files;
	while ( bScriptRun && p!=NULL ) {
		if ( bScriptPause ){
			Sleep(100);
		}
		else {
			char *p1 = strchr(p, 0x0a);
			if ( p1!=NULL ) *p1++ = 0;
			host->send_file(p, dst);
			p = p1;
		}
	}
	free(files);
	bScriptRun = bScriptPause = false;
	host->write("\r",1);
}
void Fl_Term::scripter(char *cmds)
{
	char *p1=cmds, *p0;
	const char *reply;
	bScriptRun = true; bScriptPause = false;
	while ( bScriptRun && p1!=NULL ) {
		if ( bScriptPause ) {
			Sleep(100); 
		}
		else {
			p0 = p1;
			p1 = strchr(p0, 0x0a);
			if ( p1!=NULL ) *p1++ = 0;
			command(p0, &reply);
		}
	}
	free(cmds);
	bScriptRun = bScriptPause = false;
}
void Fl_Term::run_script(const char *s)	//called on drag&drop
{
	if ( bScriptRun ) {
		fl_alert("another script is still running");
		return;
	}

	char *script = strdup(s);
	int rc = -1;
	if ( live() ) {
		learn_prompt();
		char *p0 = script;
		char *p1=strchr(p0, 0x0a);
		if ( p1!=NULL ) *p1=0;
		struct stat sb;			//is this a list of files?
		rc = fl_stat(p0, &sb);
		if ( p1!=NULL ) *p1=0x0a;
	}
	if ( rc!=-1 ) {				//connected and files dropped
		std::thread scripterThread(&Fl_Term::copier, this, script);
		scripterThread.detach();
	}
	else {						//disconnected or script dropped
		if ( host->type()==HOST_CONF ) {
			host->write(script, strlen(script));
			append(script, strlen(script));
			free(script);
		}
		else {
			std::thread scripterThread(&Fl_Term::scripter, this, script);
			scripterThread.detach();
		}
	}
}
bool Fl_Term::pause_script()
{
	bScriptPause = !bScriptPause;
	return bScriptPause;
}
void Fl_Term::quit_script()
{
	bScriptRun = bScriptPause = false;
}

#define TNO_IAC		0xff
#define TNO_DONT	0xfe
#define TNO_DO		0xfd
#define TNO_WONT	0xfc
#define TNO_WILL	0xfb
#define TNO_SUB		0xfa
#define TNO_SUBEND	0xf0
#define TNO_ECHO	0x01
#define TNO_AHEAD	0x03
#define TNO_STATUS	0x05
#define TNO_LOGOUT	0x12
#define TNO_WNDSIZE 0x1f
#define TNO_TERMTYPE 0x18
#define TNO_NEWENV	0x27
unsigned char TERMTYPE[]={//vt100
	0xff, 0xfa, 0x18, 0x00, 0x76, 0x74, 0x31, 0x30, 0x30, 0xff, 0xf0
};
const unsigned char *Fl_Term::telnet_options(const unsigned char *p, int cnt)
{
	const unsigned char *q = p+cnt;
	while ( *p==0xff && p<q ) {
		unsigned char negoreq[]={0xff,0,0,0, 0xff, 0xf0};
		switch ( p[1] ) {
			case TNO_WONT:
			case TNO_DONT:
				p+=3;
				break;
			case TNO_DO:
				negoreq[1]=TNO_WONT; negoreq[2]=p[2];
				if ( p[2]==TNO_TERMTYPE || p[2]==TNO_NEWENV
					|| p[2]==TNO_ECHO || p[2]==TNO_AHEAD ) {
					negoreq[1]=TNO_WILL;
					if ( *p==TNO_ECHO ) bEcho = true;
				}
				host->write((const char *)negoreq, 3);
				p+=3;
				break;
			case TNO_WILL:
				negoreq[1]=TNO_DONT; negoreq[2]=p[2];
				if ( p[2]==TNO_ECHO || p[2]==TNO_AHEAD ) {
					negoreq[1]=TNO_DO;
					if ( p[2]==TNO_ECHO ) bEcho = false;
				}
				host->write((const char *)negoreq, 3);
				p+=3;
				break;
			case TNO_SUB:
				negoreq[1]=TNO_SUB; negoreq[2]=p[2];
				if ( p[2]==TNO_TERMTYPE ) {
					host->write((const char *)TERMTYPE, sizeof(TERMTYPE));
				}
				if ( p[2]==TNO_NEWENV ) {
					host->write((const char *)negoreq, 6);
				}
				while (*p!=0xff && p<q ) p++;
				break;
			case TNO_SUBEND:
				p+=2;
		}
	}
	return p+1;
}