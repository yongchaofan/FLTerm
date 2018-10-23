//
// "$Id: Fl_Term.cxx 25753 2018-0918 10:08:20 $"
//
// Fl_Term -- A terminal simulator widget
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
#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include "Fl_Term.h"
Fl_Term::Fl_Term(int X,int Y,int W,int H,const char *L) : Fl_Widget(X,Y,W,H,L)
{
	bInsert = bAlterScreen = bAppCursor = false;
	bEscape = bGraphic = bTitle = false;
	bCursor = true;
	bLogging = false;
	bMouseScroll = false;
	ESC_idx = 0;
	term_cb = NULL;
	term_data_ = NULL;

	color(FL_BLACK);
	textsize(14);
	strcpy(sTitle, "flTerm");
	strcpy(sPrompt, "> ");
	iPrompt = 2;
	iTimeOut = 30;
	bPrompt = true;
	bEnter = true;
	bEnter1 = false;
	bDND = bLive = false;

	line = NULL;
	buff = attr = NULL;
	line_size = 8192;
	buff_size = line_size*64;
	line = (int *)malloc(line_size*sizeof(int));
	buff = (char *)malloc(buff_size);
	attr = (char *)malloc(buff_size);
	clear();
}
Fl_Term::~Fl_Term(){
	free(attr);
	free(buff);
	free(line);
};
void Fl_Term::clear()
{
	Fl::lock();
	if ( line!=NULL ) memset(line, 0, line_size*sizeof(int) );
	if ( buff!=NULL ) memset(buff, 0, buff_size);
	if ( attr!=NULL ) memset(attr, 0, buff_size);
	cursor_y = cursor_x = 0;
	screen_y = scroll_y = 0;
	sel_left = sel_right= 0;
	c_attr = 7;				//default black background, white foreground
	recv0 = 0;
	Fl::unlock();
}
void Fl_Term::resize( int X, int Y, int W, int H )
{
	Fl_Widget::resize(X,Y,W,H);
	size_x_ = w()/iFontWidth;
	size_y_ = (h()-4)/iFontHeight;
	roll_top = 0;
	roll_bot = size_y_-1;
	if ( !bAlterScreen ) {
		screen_y = cursor_y-size_y_+1;
		if ( screen_y<0 ) screen_y = 0;
		scroll_y = 0;
	}
	redraw();
	do_callback(NULL, 0);	//for callback to call host->send_size()
}
void Fl_Term::textsize( int pt )
{
	if ( pt!=0 ) font_size = pt;
	fl_font(FL_COURIER, font_size);
	iFontWidth = fl_width('a');
	iFontHeight = fl_height();
	resize(x(), y(), w(), h());
}

const unsigned int VT_attr[8]={	FL_BLACK, FL_RED, FL_GREEN, FL_YELLOW,
								FL_BLUE, FL_MAGENTA, FL_CYAN, FL_WHITE};
void Fl_Term::draw()
{
	fl_color(color());
	fl_rectf(x(),y(),w(),h());
	fl_font(FL_COURIER, font_size);

	int sel_l = sel_left;
	int sel_r = sel_right;
	if ( sel_l>sel_r ) {
		sel_l = sel_right;
		sel_r = sel_left;
	}

	int ly = screen_y+scroll_y;
	if ( ly<0 ) ly=0;
	int dy = y()+iFontHeight;
	for ( int i=0; i<size_y_; i++ ) {
		int dx = x()+1;
		int j = line[ly+i];
		while( j<line[ly+i+1] ) {
			int n=j;
			while (  attr[n]==attr[j] ) {
				if ( ++n==line[ly+i+1]) break;
				if ( n==sel_r || n==sel_l ) break;
			}
			unsigned int font_color = VT_attr[(int)attr[j]&7];
			unsigned int bg_color = VT_attr[(int)((attr[j]&0x70)>>4)];
			int wi = fl_width(buff+j, n-j);
			if ( j>=sel_l && j<sel_r ) {
				fl_color(selection_color());
				fl_rectf(dx, dy-iFontHeight+4, wi, iFontHeight);
				fl_color(fl_contrast(font_color, selection_color()));
			}
			else {
				if ( bg_color!=color() ) {
					fl_color( bg_color );
					fl_rectf(dx, dy-iFontHeight+4, wi, iFontHeight);
				}
				fl_color( font_color );
			}
			int k = n;
			if ( buff[k-1]==0x0a ) k--;	//for winXP
			fl_draw( buff+j, k-j, dx, dy );
			dx += wi;
			j=n;
		}
		dy += iFontHeight;
	}
	if ( scroll_y || bMouseScroll) {
		fl_color(FL_DARK3);		//draw scrollbar
		fl_rectf(x()+w()-8, y(), 8, y()+h());
		fl_color(FL_RED);		//draw slider
		int slider_y = h()*(cursor_y+scroll_y)/cursor_y;
		fl_rectf(x()+w()-8, y()+slider_y-8, 8, 16);
	}
	if ( bCursor && Fl::focus()==this && active() ) {
		fl_color(FL_WHITE);		//draw a white bar as cursor
		int wi = fl_width(buff+line[cursor_y], cursor_x-line[cursor_y]);
		fl_rectf(x()+wi+1, y()+(cursor_y-ly+1)*iFontHeight+1, 8, 3);
	}
}
int Fl_Term::handle( int e ) {
	switch (e) {
		case FL_FOCUS: redraw(); return 1;
		case FL_MOUSEWHEEL:
			if ( !bAlterScreen ) {
				scroll_y += Fl::event_dy();
				if ( scroll_y<-screen_y ) scroll_y = -screen_y;
				if ( scroll_y>0 ) scroll_y = 0;
				redraw();
			}
			return 1;
		case FL_PUSH:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x=Fl::event_x()/iFontWidth;
				int y=Fl::event_y()-Fl_Widget::y();
				if ( Fl::event_clicks()==1 ) {	//double click to select word
					y = y/iFontHeight + screen_y+scroll_y;
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
				if ( x>=size_x_-2 && scroll_y!=0 ) {//push in scrollbar area
					bMouseScroll = true;
					scroll_y = y*cursor_y/h()-cursor_y;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
					redraw();
				}
				else {								//push to start draging
					y = y/iFontHeight + screen_y+scroll_y;
					sel_left = line[y]+x;
					if ( sel_left>line[y+1] ) sel_left=line[y+1] ;
					while ( (buff[sel_left]&0xc0)==0x80 ) sel_left--;
					sel_right = sel_left;
				}
			}
			return 1;
		case FL_DRAG:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x = Fl::event_x()/iFontWidth;
				int y = Fl::event_y()-Fl_Widget::y();
				if ( bMouseScroll) {
					scroll_y = y*cursor_y/h()-cursor_y;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
					if ( scroll_y>0 ) scroll_y=0;
				}
				else {
					if ( y<0 ) {
						scroll_y += y/8;
						if ( scroll_y<-cursor_y ) scroll_y = -cursor_y;
					}
					if ( y>h() ) {
						scroll_y += (y-h())/8;
						if ( scroll_y>0 ) scroll_y=0;
					}
					y = y/iFontHeight + screen_y+scroll_y;
					if ( y<0 ) y=0;
					if ( !bAlterScreen && y>cursor_y ) y = cursor_y;
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
			case FL_LEFT_MOUSE:		//left button drag to copy
				take_focus();
				bMouseScroll = false;
				if ( sel_left>sel_right ) {
					int t=sel_left; sel_left=sel_right; sel_right=t;
				}
				if ( sel_left<sel_right )
					Fl::copy(buff+sel_left, sel_right-sel_left, 1);
				break;
			case FL_MIDDLE_MOUSE:	//middle click
				break;
			case FL_RIGHT_MOUSE: 	//right click to paste
				Fl::paste(*this, 1);
				break;
			}
			return 1;
		case FL_DND_RELEASE: bDND = true;
		case FL_DND_ENTER:
		case FL_DND_DRAG:
		case FL_DND_LEAVE:  return 1;
		case FL_PASTE:
			do_callback(Fl::event_text(), bDND?-1:Fl::event_length());
			bDND = false;
			return 1;
		case FL_KEYDOWN:
			if ( Fl::event_state(FL_ALT)==0 ) {
#ifdef __APPLE__
				int del;
				if ( Fl::compose(del) ) {
					int y = (cursor_y-screen_y+1)*iFontHeight;
					int x = (cursor_x-line[cursor_y])*iFontWidth;
					Fl::insertion_point_location(x,y,iFontHeight);
					for ( int i=0; i<del; i++ ) do_callback("\177",1);
				}
#endif
				int key = Fl::event_key();
				switch (key) {
				case FL_Page_Up:
					if ( !bAlterScreen ) {
						scroll_y-=size_y_;
						if ( scroll_y<-screen_y ) scroll_y=-screen_y;
						redraw();
					}
					break;
				case FL_Page_Down:
					if ( !bAlterScreen ) {
						scroll_y+=size_y_;
						if ( scroll_y>0 ) scroll_y = 0;
						redraw();
					}
					break;
				case FL_Up:
					do_callback(bAppCursor?"\033OA":"\033[A",3);
					break;
				case FL_Down:
					do_callback(bAppCursor?"\033OB":"\033[B",3);
					break;
				case FL_Right:
					do_callback(bAppCursor?"\033OC":"\033[C",3);
					break;
				case FL_Left:
					do_callback(bAppCursor?"\033OD":"\033[D",3);
					break;
				case FL_BackSpace:
					do_callback("\177", 1);
					break;
				case FL_Enter:
					do_callback("\015", 1);
					scroll_y = 0;
					bEnter = true;
					break;
				default:
					if ( bEnter ) {	//to detect Prompt after each Enter
						bEnter = false;
						bEnter1= true;
					}
					do_callback(Fl::event_text(), Fl::event_length());
					scroll_y = 0;
				}
				return 1;
			}
	}
	return Fl_Widget::handle(e);
}
void Fl_Term::buffsize(int new_line_size)
{
	char *old_buff = buff;
	char *old_attr = attr;
	int *old_line = line;
	int new_buff_size = new_line_size*64;

	Fl::lock();
	buff = (char *)realloc(buff, new_buff_size);
	attr = (char *)realloc(attr, new_buff_size);
	line = (int *)realloc(line, new_line_size*sizeof(int));
	if ( buff!=NULL && attr!=NULL && line!=NULL ) {
		if ( new_line_size>line_size ) {
			memset(line+line_size, 0, (new_line_size-line_size)*sizeof(int));
			memset(buff+buff_size, 0, new_buff_size-buff_size);
			memset(attr+buff_size, 0, new_buff_size-buff_size);
			buff_size = new_buff_size;
			line_size = new_line_size;
		}
		else {
			buff_size = new_buff_size;
			line_size = new_line_size;
			clear();
		}
	}
	else {					//clear buffer if failed to double
		if ( attr==NULL ) attr = old_attr;
		if ( buff==NULL ) buff = old_buff;
		if ( line==NULL ) line = old_line;
	}
	Fl::unlock();
}
void Fl_Term::next_line()
{
	line[++cursor_y]=cursor_x;
	if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;
	
	if ( cursor_x>=buff_size-1024 || cursor_y==line_size-2 ) {
		Fl::lock();
		int i, len = line[1024];
		recv0 -= len;
		cursor_x -= len;
		cursor_y -= 1024;
		memmove(buff, buff+len, buff_size-len);
		memset(buff+cursor_x, 0, buff_size-cursor_x);
		memmove(attr, attr+len, buff_size-len);
		memset(attr+cursor_x, 0, buff_size-cursor_x);
		for ( i=0; i<cursor_y+2; i++ ) line[i] = line[i+1024]-len;
		while ( i<line_size ) line[i++]=0;
		screen_y -= 1024;
		scroll_y = 0;
		Fl::unlock();
	}
	if ( scroll_y<0 ) scroll_y--;
	if ( screen_y<cursor_y-size_y_+1 ) screen_y = cursor_y-size_y_+1;
}
void Fl_Term::append( const char *newtext, int len ){
	const unsigned char *p = (const unsigned char *)newtext;
	const unsigned char *zz = p+len;

	if ( bEnter1 ) { //capture prompt for scripting after Enter key and pressed
		if ( len==1 ) {//and the echo is just one letter
			sPrompt[0] = buff[cursor_x-2];
			sPrompt[1] = buff[cursor_x-1];
			iPrompt = 2;
		}
		bEnter1 = false;
	}
	if ( bLogging ) fwrite( newtext, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p, zz-p );
	while ( p < zz ) {
		unsigned char c=*p++;
		if ( bTitle ) {
			if ( c==0x07 ) {
				bTitle = false;
				sTitle[title_idx]=0;
			}
			else
				sTitle[title_idx++] = c;
			continue;
		}
		switch ( c ) {
		case 	0: 	break;
		case 0x07: 	break;
		case 0x08:  if ( (buff[cursor_x--]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x--;
					break;
		case 0x09: 	do { 
						attr[cursor_x]=c_attr;
						buff[cursor_x++]=' '; 
					} while ( (cursor_x-line[cursor_y])%8!=0 );
					break;
		case 0x0a:	if ( bAlterScreen ) { 	//LF on alter screen is not stored
						if ( cursor_y==screen_y+roll_bot ) // scroll up one line
							vt100_Escape((unsigned char *)"D", 1);
						else {
							int x = cursor_x-line[cursor_y];
							cursor_x = line[++cursor_y]+x;
						}
					}
					else {	//not on alter screen, store LF at end of line
						cursor_x = line[cursor_y+1];
//if the next line is not empty, assum it's a ESC[2J cleared buffer like in top
						if ( line[cursor_y+2]!=0 ) cursor_x--;
						attr[cursor_x] = c_attr;
						buff[cursor_x++] = 0x0a;
						next_line();		//hard line feed
					}
					break;
		case 0x0d: 	if ( cursor_x-line[cursor_y]==size_x_+1 && *p!=0x0a ) 
						next_line();		//soft line feed
					else
						cursor_x = line[cursor_y]; 
					break;
		case 0x1b: 	p = vt100_Escape( p, zz-p );
					break;
		case 0xe2:  if ( bAlterScreen ) {//utf8 box drawing hack
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
		default:	if ( bGraphic ) switch ( c ) {//charset 2 box drawing
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
					if ( bInsert ) 			//insert one space
						vt100_Escape((unsigned char *)"[1@",3);	
					if ( cursor_x-line[cursor_y]>=size_x_ ) {
						int char_cnt = 0;
						for ( int i=line[cursor_y]; i<cursor_x; i++ )
							if ( (buff[i]&0xc0)!=0x80 ) char_cnt++;
						if ( char_cnt==size_x_ ) {
							if ( bAlterScreen ) 
								cursor_x--;	//don't overflow in vi
							else
								next_line();
						}
					} 
					attr[cursor_x] = c_attr;
					buff[cursor_x++] = c;
					if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;
		}
	}
	if ( !bPrompt )
		bPrompt = (strncmp(sPrompt, buff+cursor_x-iPrompt, iPrompt)==0);
	Fl::awake( this );
}

const unsigned char *Fl_Term::vt100_Escape( const unsigned char *sz, int cnt )
{
	const unsigned char *zz = sz+cnt;
/*
for ( const unsigned char *p=sz; p<zz&&p<sz+20; p++ )
	if ( *p<32 )
		fprintf(stderr, "\\%02x", *p);
	else
		fprintf(stderr, "%c",*p);
fprintf(stderr, "\n");
fflush(stderr);
*/
	bEscape = true;
	while ( sz<zz && bEscape ){
		ESC_code[ESC_idx++] = *sz++;
		switch( ESC_code[0] ) {
		case '[':
			if ( isalpha(ESC_code[ESC_idx-1])
				|| ESC_code[ESC_idx-1]=='@'
				|| ESC_code[ESC_idx-1]=='`' ) {
				bEscape = false;
				int n0=1, n1=1, n2=1;
				if ( ESC_idx>1 ) {
					char *p = strchr(ESC_code,';');
					if ( p != NULL ) {
						char *p1 = strchr(p+1, ';');
						if ( p1!=NULL ) {
							n0 = atoi(ESC_code+1);
							n1 = atoi(p+1);
							n2 = atoi(p1+1);
						}
						else {
							n0 = 0;
							n1 = atoi(ESC_code+1);
							n2 = atoi(p+1);
						}
					}
					else
						if ( isdigit(ESC_code[1]) ) {
							n0 = atoi(ESC_code+1);
							n1 = 0; 
							n2 = 0;
						}
				}
				int x;
				switch ( ESC_code[ESC_idx-1] ) {
				case 'A': //cursor up n0 times
					x = cursor_x-line[cursor_y];
					cursor_y -=n0;
					cursor_x = line[cursor_y]+x;
					break;
				case 'd'://line position absolute
					if ( n0>size_y_ ) n0 = size_y_;
					x = cursor_x-line[cursor_y];
					cursor_y = screen_y+n0-1;
					cursor_x = line[cursor_y]+x;
					break;
				case 'e': //line position relative
				case 'B': //cursor down n0 times
					x = cursor_x-line[cursor_y];
					cursor_y += n0;
					cursor_x = line[cursor_y]+x;
					break;
				case 'I': //cursor forward n0 tab stops
					n0 *= 8;	
					//fall through
				case 'a': //character position relative
				case 'C': //cursor forward n0 times
					while ( n0-->0 ) {
						if ( (buff[++cursor_x]&0xc0)==0x80 )
							while ( (buff[++cursor_x]&0xc0)==0x80 );
					}
					break;
				case 'Z': //cursor backward n0 tab stops
					n0 *= 8;
					//fall through
				case 'D': //cursor backward n0 times
					while ( n0-->0 ) {
						if ( (buff[--cursor_x]&0xc0)==0x80 )
							while ( (buff[--cursor_x]&0xc0)==0x80 );
					}
					break;
				case 'E': //cursor to begining of next line n0 times
					cursor_y += n0;
					cursor_x = line[cursor_y];
					break;
				case 'F': //cursor to begining of previous line n0 times
					cursor_y -= n0;
					cursor_x = line[cursor_y];
					break;
				case '`': //character position absolute
				case 'G': //cursor to n0th position from left
					n1 = cursor_y-screen_y+1;
					n2 = n0;				//fall through to 'H'
				case 'f': //horizontal and vertical position forced
					if ( n1==0 ) n1=1;
					if ( n2==0 ) n2=1;		//fall through to 'H'
				case 'H': //cursor to line n1, postion n2
					if ( n1>size_y_ ) n1 = size_y_;
					if ( n2>size_x_ ) n2 = size_x_;
					cursor_y = screen_y+n1-1;
					cursor_x = line[cursor_y];
					while ( --n2>0 ) {
						cursor_x++;
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x++;
					}
					break;
				case 'J': //[J kill till end, 1J begining, 2J entire screen
					if ( ESC_code[ESC_idx-2]=='[' ) {
					//[J, tinycore use this for CLI editing
						if ( !bAlterScreen ) {
							int i=cursor_y+1; line[i]=cursor_x; 
							while (++i<=screen_y+size_y_) line[i]=0;
							break;
						}
						else //tinycore use this to clear alterScreen 
							screen_y = cursor_y;
					}
					/*[2J, mostly used after [?1049h to clear screen
					  and when screen size changed during vi or raspi-config
					  flashwave TL1 use it without [?1049h for splash screen 
					  freeBSD use it without [?1049h* for top and vi 		*/
					cursor_y = screen_y; cursor_x = line[cursor_y];
					for ( int i=0; i<size_y_; i++ ) { 
						memset(buff+cursor_x, ' ', size_x_);
						memset(attr+cursor_x,   0, size_x_);
						cursor_x += size_x_;
						next_line();
					}
					cursor_y = --screen_y; cursor_x = line[cursor_y];
					break;
				case 'K': {//[K erase till line end, 1K begining, 2K entire line
					if ( ESC_code[ESC_idx-2]=='[' ) n0 = 0;
					int a=0, z=0;
					if ( line[cursor_y+1]==0 ) line[cursor_y+1] = cursor_x;
					switch ( n0 ) {
						case 0:	a=cursor_x; z=line[cursor_y+1]; break;
						case 1: z=cursor_x; a=line[cursor_y]; break;
						case 2: a=line[cursor_y]; z=line[cursor_y+1]; break;
					}
					memset(buff+a, ' ', z-a);
					memset(attr+a,   0, z-a);
					}
					break;
				case 'L': //insert n0 lines
					for ( int i=roll_bot; i>cursor_y-screen_y; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x_ );
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x_ );
					}
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[cursor_y+i], ' ', size_x_);
						memset(attr+line[cursor_y+i],   0, size_x_);
					}
					break;
				case 'M': //delete n0 lines
					for ( int i=cursor_y-screen_y; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i+n0], size_x_);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i+n0], size_x_);
					}
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[screen_y+roll_bot-i], ' ', size_x_);
						memset(attr+line[screen_y+roll_bot-i],   0, size_x_);
					}
					break;
				case '@': //insert n0 spaces
					for ( int i=line[cursor_y+1]; i>=cursor_x; i-- ){
						buff[i+n0]=buff[i]; 
						attr[i+n0]=attr[i];
					}
					if ( !bAlterScreen ) {
						line[cursor_y+1]+=n0;
						if ( line[cursor_y+2]!=0 ) line[cursor_y+2]+=n0;
						memset(buff+cursor_x, ' ', n0);
						memset(attr+cursor_x,   0, n0);
					}
					break;
				case 'P': //delete n0 characters
					for ( int i=cursor_x; i<line[cursor_y+1]-n0; i++ ) {
						buff[i]=buff[i+n0];
						attr[i]=attr[i+n0];
					}
					if ( !bAlterScreen ) {
					//when editing command input longer than a single line
						line[cursor_y+1]-=n0;
						if ( line[cursor_y+2]>0 ) line[cursor_y+2]-=n0;
						memset(buff+line[cursor_y+1], ' ', n0);
						memset(attr+line[cursor_y+1],   0, n0);
					}
					break;
				case 'X': //erase n0 characters
					for ( int i=0; i<n0; i++) {
						buff[cursor_x+i]=' ';
						attr[cursor_x+i]=0;
					}
					break;
				case 'S': // scroll up n0 lines
					for ( int i=roll_top; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i+n0], size_x_);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i+n0], size_x_);
					}
					memset(buff+line[screen_y+roll_bot-n0+1], ' ', n0*size_x_);
					memset(attr+line[screen_y+roll_bot-n0+1],   0, n0*size_x_);
					break;
				case 'T': // scroll down n0 lines
					for ( int i=roll_bot; i>=roll_top+n0; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x_);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x_);
					}
					memset(buff+line[screen_y+roll_top], ' ', n0*size_x_);
					memset(attr+line[screen_y+roll_top],   0, n0*size_x_);
					break;
				case 'h':
					if ( ESC_code[1]=='4' ) bInsert=true;
					if ( ESC_code[1]=='?' ) {
						n0 = atoi(ESC_code+2);
						if ( n0==1 ) bAppCursor=true;
						if ( n0==25 ) bCursor=true;
						if ( n0==1049 ) {	//?1049h enter alternate screen
							bAlterScreen = true;
							screen_y = cursor_y; cursor_x = line[cursor_y];
/* 	should clear alterScreen here, but all apps(vi, raspi-config etc) use 
	ESC[2J or ESC[J to clear the screen following ESC[?1049h any way 	*/
						}
					}
					break;
				case 'l':
					if ( ESC_code[1]=='4' ) bInsert=false;
					if ( ESC_code[1]=='?' ) {
						n0 = atoi(ESC_code+2);
						if ( n0==1 )  bAppCursor=false;
						if ( n0==25 ) bCursor=false;
						if ( n0==1049 ) { 	//?1049l exit alternate screen
							bAlterScreen = false;
							cursor_y = screen_y; cursor_x = line[cursor_y];
							for ( int i=1; i<=size_y_+1; i++ ) 
								line[cursor_y+i] = 0;
							screen_y = cursor_y-size_y_+1;
							if ( screen_y<0 ) screen_y = 0;
						}
					}
					break;
				case 'm': //text style, color attributes
					if ( n0==0 && n2!=1 ) n0 = n2;	//ESC[0;0m	ESC[01;34m
					switch ( int(n0/10) ) {			//ESC[34m
					case 0: if ( n0%10==7 ) {c_attr = 0x70; break;}
					case 1:
					case 2: c_attr = 7; break;
					case 3: if ( n0==39 ) n0 = 37;	//39 default foreground
							c_attr = (c_attr&0xf0)+n0%10; break;
					case 4: if ( n0==49 ) n0 = 40;	//49 default background
							c_attr = (c_attr&0x0f)+((n0%10)<<4); break;
					}
					break;
				case 'r': roll_top=n1-1; roll_bot=n2-1; break;
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
		case 'D': // scroll up one line
			for ( int i=roll_top; i<roll_bot; i++ ) {
				memcpy(buff+line[screen_y+i],buff+line[screen_y+i+1],size_x_);
				memcpy(attr+line[screen_y+i],attr+line[screen_y+i+1],size_x_);
			}
			memset(buff+line[screen_y+roll_bot], ' ', size_x_);
			memset(attr+line[screen_y+roll_bot],   0, size_x_);
			bEscape = false;
			break;
		case 'F': //cursor to lower left corner
			cursor_y = screen_y+size_y_-1;
			cursor_x = line[cursor_y];
			bEscape = false;
			break;
		case 'M': // scroll down one line
			for ( int i=roll_bot; i>roll_top; i-- ) {
				memcpy(buff+line[screen_y+i],buff+line[screen_y+i-1],size_x_);
				memcpy(attr+line[screen_y+i],attr+line[screen_y+i-1],size_x_);
			}
			memset(buff+line[screen_y+roll_top], ' ', size_x_);
			memset(attr+line[screen_y+roll_top],   0, size_x_);
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
		case '(': //character sets, 0 for line drawing
			if ( ESC_idx==2 ) {
				bGraphic = (ESC_code[1]=='0');
				bEscape = false;
			}
			break;
		default: bEscape = false;
		}
		if ( ESC_idx==20 ) bEscape = false;
		if ( !bEscape ) { ESC_idx=0; memset(ESC_code, 0, 20); }
	}
	return sz;
}

void Fl_Term::srch( const char *word, int dirn )
{
	for ( int i=cursor_y+scroll_y+dirn; i>0&&i<cursor_y; i+=dirn ) {
		int len = strlen(word);
		char *p, tmp=buff[line[i+1]+len-1];
		buff[line[i+1]+len-1]=0;
		p = strstr(buff+line[i], word);
		buff[line[i+1]+len-1]=tmp;
		if ( p!=NULL ) {
			scroll_y = i-cursor_y;
			sel_left = p-buff;
			sel_right = sel_left+strlen(word);
			redraw();
			break;
		}
	}
}
void Fl_Term::logg( const char *fn )
{
	if ( bLogging ) {
		fclose( fpLogFile );
		bLogging = false;
		puts("\r\n\033[32m***Log file closed***\033[37m\r\n");
	}
	else {
		fpLogFile = fl_fopen( fn, "wb" );
		if ( fpLogFile != NULL ) {
			bLogging = true;
			puts("\r\n\033[32m***");
			puts(fn);
			puts(" opened for logging***\033[37m\r\n");
		}
	}
}
void Fl_Term::copyall()
{
	Fl::copy(buff, cursor_x, 1);
}
int Fl_Term::waitfor(const char *word)
{
	char *p = buff+recv0;
	bWait = true;
	for ( int i=0; i<iTimeOut*10&&bWait; i++ ) {
		buff[cursor_x]=0;
		if ( strstr(p, word)!=NULL ) return 1;
		sleep(1);
	}
	return 0;
}
void Fl_Term::prompt(char *p)
{
	strncpy(sPrompt, p, 31);
	iPrompt = strlen(sPrompt);
}
void Fl_Term::mark_prompt()
{
	bPrompt = false;
	recv0 = cursor_x;
}
int Fl_Term::wait_prompt( char **preply )
{
	if ( preply!=NULL ) *preply = buff+recv0;
	int oldlen = recv0;
	for ( int i=0; i<iTimeOut && !bPrompt; i++ ) {
		sleep(1);
		if ( cursor_x>oldlen ) { i=0; oldlen=cursor_x; }
	}
	bPrompt = true;
	return cursor_x - recv0;
}
void Fl_Term::putxml(const char *msg, int len)
{
	int indent=0, previousIsOpen=true;
	const char *p=msg, *q;
	const char spaces[256]="\r\n                                               \
                                                                              ";
	while ( *p!=0 && *p!='<' ) p++;
	if ( p>msg ) append(msg, p-msg);
	while ( *p!=0 && p<msg+len ) {
		while (*p==0x0d || *p==0x0a || *p=='\t' || *p==' ' ) p++;
		if ( *p=='<' ) { //tag
			if ( p[1]=='/' ) {
				if ( !previousIsOpen ) {
					indent -= 2;
					append(spaces, indent);
				}
				previousIsOpen = false;
			}
			else {
				if ( previousIsOpen ) indent+=2;
				append(spaces, indent);
				previousIsOpen = true;
			}
			q = strchr(p, '>');
			if ( q!=NULL ) {
				append("\033[32m",5);
				const char *r = strchr(p, ' ');
				if ( r!=NULL && r<q ) {
					append(p, r-p);
					append("\033[35m",5);
					append(r, q-r);
				}
				else
					append(p, q-p);
				append("\033[32m>",6);
				if ( q[-1]=='/' ) previousIsOpen = false;
				p = q+1;
			}
			else
				break;
		}
		else {			//data
			q = strchr(p, '<');
			if ( q!=NULL ) {
				append("\033[33m",5);
				append(p, q-p);
				p = q;
			}
			else { //not xml or incomplete xml
				append(p, strlen(p));
				break;
			}
		}
	}
	append("\033[37m",5);
}