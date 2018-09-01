//
// "$Id: Fl_Term.cxx 22734 2018-08-31 10:08:20 $"
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
#include "Fl_Term.h"
Fl_Term::Fl_Term(int X,int Y,int W,int H,const char *L) : Fl_Widget(X,Y,W,H,L)
{
	bInsert = bAlterScreen = bAppCursor = false;
	bEscape = bGraphic = bTitle = false;
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
	line_size = 4096;
	buff_size = line_size*64;
	line = (int *)realloc(line, (line_size+1)*sizeof(int) );
	buff = (char *)realloc(buff, buff_size+256 );
	attr = (char *)realloc(attr, buff_size+256 );
	if ( line!=NULL ) memset(line, 0, (line_size+1)*sizeof(int) );
	if ( buff!=NULL ) memset(buff, 0, buff_size+256);
	if ( attr!=NULL ) memset(attr, 0, buff_size+256);
	buff_y = buff_x = 0;
	screen_y = scroll_y = 0;
	page_up_hold = page_down_hold = 0;
	sel_left = sel_right= 0;
	c_attr = 7;				//default black background, white foreground
	recv0 = 0;
	Fl::unlock();
}
void Fl_Term::resize( int X, int Y, int W, int H )
{
	Fl_Widget::resize(X,Y,W,H);
	size_x_ = w()/iFontWidth;
	size_y_ = h()/iFontHeight;
	screen_y = buff_y-size_y_+1;
	if ( screen_y<0 ) screen_y = 0;
	scroll_y = 0;
	redraw();
	do_callback(NULL, 0);	//for callback to call host->send_size()
}
void Fl_Term::textsize( int pt )
{
	font_size = pt;
	fl_font(FL_COURIER, font_size);
	iFontWidth = fl_width('a');
	iFontHeight = fl_height();
	resize(x(), y(), w(), h());
}

const unsigned int VT_attr[8]={FL_BLACK, FL_RED, FL_GREEN, FL_YELLOW,
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
	int lx = buff_x-line[buff_y];
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
			if ( buff[k-1]==0x0a ) k--;
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
		int slider_y = h()*(buff_y+scroll_y)/buff_y;
		fl_rectf(x()+w()-8, y()+slider_y-6, 8, 12);
		fl_line( x()+w()-6, y()+slider_y-8, x()+w()-3, y()+slider_y-8 );
		fl_line( x()+w()-7, y()+slider_y-7, x()+w()-2, y()+slider_y-7 );
		fl_line( x()+w()-7, y()+slider_y+6, x()+w()-2, y()+slider_y+6 );
		fl_line( x()+w()-6, y()+slider_y+7, x()+w()-3, y()+slider_y+7 );
	}
	if ( Fl::focus()==this && active() ) {
		fl_color(FL_WHITE);		//draw a white bar as cursor
		int wi = fl_width(buff+line[buff_y], buff_x-line[buff_y]); 
		fl_rectf(x()+wi+1, y()+(buff_y-ly+1)*iFontHeight+1,8,3);
	}
}
int Fl_Term::handle( int e ) {
	switch (e) {
		case FL_FOCUS: redraw(); return 1;
		case FL_MOUSEWHEEL:
			scroll_y += Fl::event_dy();
			if ( scroll_y<-screen_y ) scroll_y = -screen_y;
			if ( scroll_y>0 ) scroll_y = 0;
			redraw();
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
						if ( !isalnum(buff[sel_left]) ) {
							sel_left++;
							break;
						}
					while ( ++sel_right<line[y+1])
						if ( !isalnum(buff[sel_right]) )
							break;
					redraw();
					return 1;
				}
				if ( x>=size_x_-2 && scroll_y!=0 ) {//push in scrollbar area
					bMouseScroll = true;
					scroll_y = y*buff_y/h()-buff_y;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
					redraw();
				}
				else {								//push to start draging
					y = y/iFontHeight + screen_y+scroll_y;
					sel_left = line[y]+x;
					if ( sel_left>line[y+1] ) sel_left=line[y+1] ;
					sel_right = sel_left;
				}
			}
			return 1;
		case FL_DRAG:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x = Fl::event_x()/iFontWidth;
				int y = Fl::event_y()-Fl_Widget::y();
				if ( bMouseScroll) {
					scroll_y = y*buff_y/h()-buff_y;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
					if ( scroll_y>0 ) scroll_y=0;
				}
				else {
					if ( y<0 ) {
						scroll_y += y/8;
						if ( scroll_y<-buff_y ) scroll_y = -buff_y;
					}
					if ( y>h() ) {
						scroll_y += (y-h())/8;
						if ( scroll_y>0 ) scroll_y=0;
					}
					y = y/iFontHeight + screen_y+scroll_y;
					if ( y<0 ) y=0;
					if ( !bAlterScreen && y>buff_y ) y = buff_y;
					//buff_y may not be the last line in AlterScreen mode
					sel_right = line[y]+x;
					if ( sel_right>line[y+1] ) sel_right=line[y+1];
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
		case FL_KEYUP:
			if ( Fl::event_state(FL_ALT)==0 ) {
				int key = Fl::event_key();
				switch ( key ) {
					case FL_Page_Up:
						page_up_hold = 0;
						break;
					case FL_Page_Down:
						page_down_hold = 0;
						break;
				}
			}
			break;
		case FL_KEYDOWN:
			if ( Fl::event_state(FL_ALT)==0 ) {
				int key = Fl::event_key();
				switch (key) {
				case FL_Page_Up:
						scroll_y-=size_y_*(1<<(page_up_hold/16))-1;
						page_up_hold++;
						if ( scroll_y<-screen_y ) scroll_y=-screen_y;
						redraw();
						break;
				case FL_Page_Down:
						scroll_y+=size_y_*(1<<(page_down_hold/16))-1;
						page_down_hold++;
						if ( scroll_y>0 ) scroll_y = 0;
						redraw();
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
				default:if ( key==FL_Enter ) {
							bEnter = true;
						}
						else {
							if ( bEnter ) {	//to detect Prompt after each Enter
								bEnter = false;
								bEnter1= true;
							}
						}
						do_callback(Fl::event_text(), Fl::event_length());
						scroll_y = 0;
				}
				return 1;
			}
	}
	return Fl_Widget::handle(e);
}
void Fl_Term::next_line()
{
	line[++buff_y] = buff_x;
	if ( buff_x>=buff_size ) {	//if text buffer is filled, try to double it
		char *old_buff = buff;
		char *old_attr = attr;
		Fl::lock();
		buff = (char *)realloc(buff, buff_size*2+256);
		attr = (char *)realloc(attr, buff_size*2+256);
		if ( buff!=NULL && attr!=NULL ) {
			buff_size *= 2;
		}
		else {					//clear buffer if failed to double
			if ( attr==NULL ) free(old_attr);
			if ( buff==NULL ) free(old_buff);
			attr = buff = NULL;
			clear();
		}
		Fl::unlock();
	}
	if ( buff_y==line_size ) {	//if line buffer is filled, try to double it
		int *old_line = line;
		Fl::lock();
		line = (int *)realloc(line, (line_size*2+1)*sizeof(int));
		if ( line!=NULL ) {
			line_size *= 2;
			for ( int i=buff_y+1; i<line_size+1; i++ ) line[i]=0;
		}
		else {					//clear buffer if failed to double
			free(old_line);
			clear();
		}
		Fl::unlock();
	}
	if ( line[buff_y+1]<buff_x ) line[buff_y+1] = buff_x;
	if ( screen_y<buff_y-size_y_+1 ) screen_y = buff_y-size_y_+1;
	if ( scroll_y<0 ) scroll_y--;
}
void Fl_Term::append( const char *newtext, int len ){
	const char *p = newtext;
	const char *zz = newtext+len;
	
	if ( bEnter1 ) { //capture prompt for scripting after Enter key and pressed
		if ( len==1 ) {//and the echo is just one letter
			sPrompt[0] = buff[buff_x-2];
			sPrompt[1] = buff[buff_x-1];
			iPrompt = 2;
		}
		bEnter1 = false;
	}
	if ( bLogging ) fwrite( newtext, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p, zz-p );
	while ( p < zz ) {
		char c=*p++;
		switch ( (unsigned char)c ) {
		case 	0: 	break;
		case 0x07: 	if ( bTitle ) {
						bTitle=false;
						buff[buff_x]=0;
						buff_x=line[buff_y];
						line[buff_y+1]=buff_x;
						strncpy(sTitle, buff+buff_x, 63);
						sTitle[63] = 0;
					}
					break;
		case 0x08: 	--buff_x; 
					break;
		case 0x09: 	do { buff[buff_x++]=' '; }
					while ( (buff_x-line[buff_y])%8!=0 );
					break;
		case 0x1b: 	p = vt100_Escape( p, zz-p ); 
					break;
		case 0x0d: 	if ( bAlterScreen || (*p!=0x0a&&*p!=0x0d) )
						buff_x = line[buff_y]; 
					break;
		case 0x0a: 	if ( bAlterScreen ) {
						if ( buff_y==screen_y+roll_bot ) {
							vt100_Escape("D", 1);// scroll up one line
						}
						else {
							int x = buff_x-line[buff_y];
							buff_x = line[++buff_y]+x;
						}
					}
					else {
						buff_x = line[buff_y+1];
						attr[buff_x] = c_attr;
						buff[buff_x++] = c;
						next_line();
					}
					break;
		case 0xe2: if ( (unsigned char)(*p)==0x94 ) {//utf8 box drawing 
						switch ( (unsigned char )(p[1]) ) {
						case 0x80: 
						case 0xac:
						case 0xb4:
						case 0xbc: c='_'; break;
						case 0x82:
						case 0x94:
						case 0x98: 
						case 0x9c:  
						case 0xa4: c='|'; break;
						case 0x8c:
						case 0x90: c=' '; break;
						default: c='?';
						}
						p+=2;
					}
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
					if ( bInsert ) vt100_Escape("[1@",3);	//insert one space
					if ( buff_x-line[buff_y]==size_x_ ) {
						if ( bAlterScreen )
							buff_x--;		//don't overflow a line in vi
						else
							next_line();
					}
					attr[buff_x] = c_attr;
					buff[buff_x++] = c;
					if ( line[buff_y+1]<buff_x ) line[buff_y+1]=buff_x;
		}
	}
	if ( !bPrompt )
		bPrompt = (strncmp(sPrompt, buff+buff_x-iPrompt, iPrompt)==0);
	Fl::awake( this );
}

const char *Fl_Term::vt100_Escape( const char *sz, int cnt )
{
	const char *zz = sz+cnt;
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
						n1 = atoi(ESC_code+1);
						n2 = atoi(p+1);
					}
					else if ( isdigit(ESC_code[1]) )
							n0 = atoi(ESC_code+1);
				}
				int x;
				switch ( ESC_code[ESC_idx-1] ) {
				case 'A': //cursor up n0 times
					x = buff_x-line[buff_y];
					buff_y -=n0;
					buff_x = line[buff_y]+x;
					break;
				case 'B': //cursor down n0 times
					x = buff_x-line[buff_y];
					buff_y += n0;
					buff_x = line[buff_y]+x;
					break;
				case 'C': //cursor forward n0 times
					buff_x += n0; 
					break;
				case 'D': //cursor backward n0 times
					buff_x -= n0; 
					break;
				case 'E': //cursor to begining of next line n0 times
					buff_y += n0; 
					buff_x = line[buff_y];
					break;
				case 'F': //cursor to begining of previous line n0 times
					buff_y -= n0; 
					buff_x = line[buff_y];
					break;
				case 'G': //cursor to n0th position from left
					buff_x = line[buff_y]+n0-1; 
					break;
				case 'H': //cursor to line n1, postion n2
					if ( n1>size_y_ ) n1 = size_y_;
					if ( n2>size_x_ ) n2 = size_x_;
					buff_y = screen_y+n1-1;
					buff_x = line[buff_y]+n2-1;
					break;
				case 'I': //cursor forward n0 tab stops
					buff_x += n0*8;
					break;
				case 'Z': //cursor backward n0 tab stops
					buff_x -= n0*8;
					break;
				case 'J': {//[J kill till end, 1J begining, 2J entire screen
					if ( ESC_code[ESC_idx-2]=='[' ) {
						n0 = 0;
						if ( !bAlterScreen ) {
							line[buff_y+1] = buff_x; break;
						}
					}
					int a=0, z=0, s=screen_y;
					switch ( n0 ) {
						case 0: a=buff_y;   z=screen_y+size_y_; break;			
						case 1: a=screen_y; z=buff_y+1; break;			
						case 2: a=screen_y; z=screen_y+size_y_; break;
						}
						char empty[256];
						memset(empty,' ',256);
						buff_y = a; buff_x = line[buff_y];
						while (buff_y<z) {
							append(empty, size_x_);
							next_line();
						}
						buff_y = a; buff_x = line[buff_y];
						screen_y = s;
					}
					break;
				case 'K': {//[K erase till line end, 1K begining, 2K entire line
					if ( ESC_code[ESC_idx-2]=='[' ) n0 = 0;
					int i=0, j=0;
					switch ( n0 ) {
						case 0:	i=buff_x; j=line[buff_y+1]; break;
						case 1: j=buff_x; i=line[buff_y]; break;
						case 2: i=line[buff_y]; j=line[buff_y+1]; break;
						}
						memset(buff+i, ' ', j-i);
						memset(attr+i,   0, j-i);
					}
					break;
				case 'L': //insert n0 lines
					for ( int i=roll_bot; i>buff_y-screen_y; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x_ );
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x_ );
					}
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[buff_y+i], ' ', size_x_);
						memset(attr+line[buff_y+i],   0, size_x_);
					}
					break;
				case 'M': //delete n0 lines
					for ( int i=buff_y-screen_y; i<=roll_bot-n0; i++ ) {
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
					for ( int i=line[buff_y+1]; i>=buff_x; i-- ){
						buff[i+n0]=buff[i]; attr[i+n0]=attr[i];
					}
					memset(buff+buff_x, ' ', n0);
					line[buff_y+1]+=n0;
					break;
				case 'P': //delete n0 characters
					for ( int i=buff_x; i<line[buff_y+1]-n0; i++ ) {
						buff[i]=buff[i+n0]; 
						attr[i]=attr[i+n0]; 
					}
					if ( !bAlterScreen ) //for freebsd vi 'x' operation
						line[buff_y+1]-=n0;
					break;
				case 'X': //erase n0 characters
					for ( int i=0; i<n0; i++) {
						buff[buff_x+i]=' '; 
						attr[buff_x+i]=0;
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
//				case '`': //character position absolute
//				case 'a': //character position relative
//				case 'd':{//line position absolute
//					if ( n0>size_y_ ) n0 = size_y_;
//					int x = buff_x-line[buff_y];
//					buff_y = screen_y+n0-1;
//					buff_x = line[buff_y]+x;
//					}
//					break;
//				case 'e': //line position relative
//				case 'f': //horizontal and vertical position forced
				case 'h':
					if ( ESC_code[1]=='4' ) bInsert=true;
					if ( ESC_code[1]=='?' ) {
						n0 = atoi(ESC_code+2);
						if ( n0==1 ) bAppCursor=true;
						if ( n0==1 || n0==1049 ) {
							bAlterScreen = true;
							save_y = screen_y =  buff_y;
							roll_top = 0; roll_bot = size_y_-1;
						}
					}
					break;
				case 'l':
					if ( ESC_code[1]=='4' ) bInsert=false;
					if ( ESC_code[1]=='?' ) {
						n0 = atoi(ESC_code+2);
						if ( n0==1 )  bAppCursor=false;
						if ( n0==1 || n0==1049 ) { 	//?1049h alternate screen
							bAlterScreen = false;
							buff_y = save_y;
							buff_x = line[buff_y];
							for ( int i=1; i<=size_y_+1; i++ )
								if (buff_y+i<line_size) line[buff_y+i] = 0;
							screen_y = buff_y-size_y_+1;
							if ( screen_y<0 ) screen_y = 0;
						}
					}
					break;
				case 'm': //text style, color attributes
					if ( n0==1 && n2!=1 ) n0 = n2; 	//ESC[0;0m	ESC[01;34m
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
				}
			}
			break;
		case 'D': // scroll up one line
			for ( int i=roll_top; i<roll_bot; i++ ) {
				memcpy(buff+line[screen_y+i], buff+line[screen_y+i+1], size_x_);
				memcpy(attr+line[screen_y+i], attr+line[screen_y+i+1], size_x_);
			}
			memset(buff+line[screen_y+roll_bot], ' ', size_x_);
			memset(attr+line[screen_y+roll_bot],   0, size_x_);
			bEscape = false;
			break;
		case 'F': //cursor to lower left corner
			buff_y = screen_y+size_y_-1;
			buff_x = line[buff_y];
			bEscape = false;
			break;
		case 'M': // scroll down one line
			for ( int i=roll_bot; i>roll_top; i-- ) {
				memcpy(buff+line[screen_y+i], buff+line[screen_y+i-1], size_x_);
				memcpy(attr+line[screen_y+i], attr+line[screen_y+i-1], size_x_);
			}
			memset(buff+line[screen_y+roll_top], ' ', size_x_);
			memset(attr+line[screen_y+roll_top],   0, size_x_);
			bEscape = false;
			break;
		case ']': //set window title
			if ( ESC_code[ESC_idx-1]==';' ) {
				if ( ESC_code[1]=='0' ) bTitle = true;
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
	for ( int i=buff_y+scroll_y+dirn; i>0&&i<buff_y; i+=dirn ) {
		int len = strlen(word);
		char *p, tmp=buff[line[i+1]+len-1];
		buff[line[i+1]+len-1]=0;
		p = strstr(buff+line[i], word);
		buff[line[i+1]+len-1]=tmp;
		if ( p!=NULL ) {
			scroll_y = i-buff_y;
			sel_left = p-buff;
			sel_right = sel_left+strlen(word);
			redraw();
			break;
		}
	}
}
void Fl_Term::logging( const char *fn )
{
	if ( bLogging ) {
		fclose( fpLogFile );
		bLogging = false;
		puts("\n\033[32m***Log file closed***\033[37m\n");
	}
	else {
		fpLogFile = fl_fopen( fn, "wb" );
		if ( fpLogFile != NULL ) {
			bLogging = true;
			puts("\n\033[32m***");
			puts(fn);
			puts(" opened for logging***\033[37m\n");
		}
	}
}
void Fl_Term::save(const char *fn)
{
	FILE *fp = fl_fopen( fn, "w" );
	if ( fp != NULL ) {
		fwrite(buff, 1, buff_x, fp);
		fclose( fp );
		puts("\n\033[32m***buffer saved***\033[37m\n");
	}
}
int Fl_Term::waitfor(const char *word)
{
	char *p = buff+recv0;
	bWait = true;
	for ( int i=0; i<iTimeOut*10&&bWait; i++ ) {
		buff[buff_x]=0;
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
	recv0 = buff_x;
}
int Fl_Term::wait_prompt( char **preply )
{
	if ( preply!=NULL ) *preply = buff+recv0;
	int oldlen = recv0;
	for ( int i=0; i<iTimeOut && !bPrompt; i++ ) {
		sleep(1);
		if ( buff_x>oldlen ) { i=0; oldlen=buff_x; }
	}
	bPrompt = true;
	return buff_x - recv0;
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