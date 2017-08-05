//
// "$Id: Fl_Term.cxx 16354 2017-08-04 13:48:10 $"
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

#include <unistd.h>
#include <ctype.h>
#include <pthread.h>
#include "Fl_Term.h"

Fl_Term::Fl_Term(int X,int Y,int W,int H,const char *L) : Fl_Widget(X,Y,W,H,L) 
{
	bInsert = bAlterScreen = bAppCursor = false;
	bEscape = bGraphic = bTitle = false;
	bLogging = false;
	ESC_idx = 0;

	Fl::set_font(FL_COURIER, TERMFONT);
	strcpy(sPrompt, "> ");
	bPrompt = false;
	iPrompt = 2;
	iTimeOut = 30;	
	keyword[0] = 0;
	
	line = NULL;
	buff = attr = NULL;
	clear();
	set_fontsize(14);
}
void Fl_Term::clear()
{
	Fl::lock();
	line_size = 1024;
	buff_size = line_size*64;
	line = (int *)realloc( line, (line_size+1)*sizeof(int) );
	buff = (char *)realloc( buff, buff_size+256 );
	attr = (char *)realloc( attr, buff_size+256 );
	memset(line, 0, (line_size+1)*sizeof(int) );
	memset(buff, 0, buff_size+256);
	memset(attr, 0, buff_size+256);
	cursor_y = cursor_x = 0;
	screen_y = -1;
	scroll_y = 0;
	c_attr = 0;
	sel_left = 0;
	sel_right= 0;
	recv0 = 0;
	Fl::unlock();
	redraw();
}
void Fl_Term::set_fontsize( int pt ) 
{
	font_size = pt;
	iFontHeight = font_size; size_y = (h()-8)/iFontHeight;
	iFontWidth = font_size/2+1; size_x = w()/iFontWidth;
}	
Fl_Term::~Fl_Term(){
	free(attr);
	free(buff);
	free(line);
};

const int VT_attr[8]={FL_WHITE, FL_RED, FL_GREEN, FL_YELLOW, 
					 FL_BLUE, FL_MAGENTA, FL_CYAN, FL_BLACK};
void Fl_Term::draw()
{
	fl_color(color());
	fl_rectf(x(),y(),w(),h());
	fl_font(FL_COURIER, font_size);

	int lx = cursor_x-line[cursor_y];
	int ly = screen_y+1+scroll_y;
	if ( ly<0 ) ly=0;
	int dy = y()+iFontHeight;
	for ( int i=0; i<size_y; i++ ) {
		int dx = x()+1;
		int j = line[ly+i];
		while( j<line[ly+i+1] ) {
			int n=j;
			while (  attr[n]==attr[j] ) {
				if ( ++n==line[ly+i+1]) break;
				if ( n==sel_right || n==sel_left ) break;
			}
			int font_color = VT_attr[(int)attr[j]%8];
			if ( j>=sel_left && j<sel_right ) {
				fl_color(selection_color());
				fl_rectf(dx, dy-iFontHeight+3, (n-j)*iFontWidth, iFontHeight);
				fl_color(fl_contrast(font_color, selection_color()));
			}
			else 
				fl_color( font_color );
				
			int k = n;
			if ( buff[k-1]==0x0a ) k--;
			fl_draw( buff+j, k-j, dx, dy );
			dx += iFontWidth*(k-j);
			j=n;
		}
		dy += iFontHeight;
	}
	fl_color(FL_WHITE);		//draw a white block as cursor
	fl_rectf(x()+lx*iFontWidth+1, y()+(cursor_y-ly+1)*iFontHeight-4,8,8);
}

int Fl_Term::handle( int e ) {
	switch (e) {
		case FL_FOCUS: return 1;
		case FL_MOUSEWHEEL: 
			scroll_y += Fl::event_dy();
			if ( scroll_y<-cursor_y ) scroll_y = -cursor_y;
			if ( scroll_y>0 ) scroll_y = 0;	
			redraw();
			return 1;
		case FL_KEYDOWN: {
			int key = Fl::event_key();
			switch (key) {
			case FL_Page_Up: 
					scroll_y-=size_y; 
					if ( scroll_y<-cursor_y ) scroll_y=-cursor_y;
					redraw();
					break;
			case FL_Page_Down:
					scroll_y+=size_y; 
					if ( scroll_y>0 ) scroll_y = 0;
					redraw();
					break;
			case FL_Up:    write(bAppCursor?"\033OA":"\033[A"); break;
			case FL_Down:  write(bAppCursor?"\033OB":"\033[B"); break;
			case FL_Right: write(bAppCursor?"\033OC":"\033[C"); break;
			case FL_Left:  write(bAppCursor?"\033OD":"\033[D"); break;
			case FL_BackSpace: write("\177"); break;
			default: if ( Fl::event_state(FL_ALT )==0 ) {
					write(Fl::event_text());
					scroll_y = 0;
				}
				else switch(key) {
					case FL_Enter: if ( host!=NULL ) 
										if ( host->state()==HOST_IDLE ) 
											host->start(NULL); 
									break;
					case FL_Home:scroll_y=-cursor_y; redraw(); break;
					case FL_End: scroll_y=0; redraw(); break;
					case 'a': sel_left=0; sel_right=cursor_x;
							  Fl::copy(buff, cursor_x, 1);
							  redraw();
							  break;
					case 's': srch(NULL); break;
					case 'p': srch(keyword); break;
					case 'n': srch(keyword, 1); break;
					default: break;
				}
			}
			return 1;
		}
		case FL_PUSH: if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x=Fl::event_x()/iFontWidth; 
				int y=(Fl::event_y()-8)/iFontHeight+screen_y+scroll_y; 
				sel_left = line[y]+x;
				if ( sel_left>line[y+1] ) sel_left=line[y+1] ;
				sel_right = sel_left;
			}
			return 1;
		case FL_DRAG: if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				int x=Fl::event_x()/iFontWidth; 
				int y=(Fl::event_y()-8)/iFontHeight+screen_y+scroll_y; 
				sel_right = line[y]+x;
				if ( sel_right>line[y+1] ) sel_right=line[y+1];
				if ( sel_right!=sel_left  ) redraw();
			}
			return 1;
		case FL_RELEASE:
			if ( Fl::event_button()==FL_LEFT_MOUSE ) {
				take_focus();		//left button drag to copy
				if ( sel_left>sel_right ) {
					int t=sel_left; sel_left=sel_right; sel_right=t;
				}
				if ( sel_left<sel_right ) 
					Fl::copy(buff+sel_left, sel_right-sel_left, 1);
			}
			else					//right click to paste
				Fl::paste(*this, 1);
			return 1; 
		case FL_DND_ENTER: 
		case FL_DND_DRAG:  
		case FL_DND_RELEASE: 
		case FL_DND_LEAVE:  return 1;
		case FL_PASTE: 	{
				const char *txt = Fl::event_text();
				const char *p = strchr(txt, 0x0a);
				if ( p==NULL || p[1]==0 ) {	//paste if only one line
					write(txt);
				}
				else { 			//run script when more than one line
					strncpy(script, txt, 4095 );
					pthread_t id;
					pthread_create( &id, NULL, scripter, (void *)this);
				}
			}
			return 1;
	}
	return(Fl_Widget::handle(e));
}
void Fl_Term::resize( int X, int Y, int W, int H ) 
{
	Fl_Widget::resize(X,Y,W,H);
	size_y = (h()-8)/iFontHeight;
	size_x = w()/iFontWidth;
	if ( host!=NULL ) host->send_size(size_x, size_y);
}

void Fl_Term::more_chars()
{
	Fl::lock();
	char *new_buff = (char *)realloc(buff, buff_size*2+256);
	char *new_attr = (char *)realloc(attr, buff_size*2+256);
	if ( new_buff && new_attr ) {
		buff_size *= 2;
		attr = new_attr; buff = new_buff;
	}
	else clear();
	Fl::unlock();
}
void Fl_Term::more_lines()
{
	Fl::lock();
	int *new_line = (int *)realloc(line, (line_size*2+1)*sizeof(int));
	if ( new_line!=NULL ) {
		line_size *= 2; line = new_line;
		for ( int i=cursor_y+1; i<line_size+1; i++ ) line[i]=0;
	}
	else clear();
	Fl::unlock();
}

void Fl_Term::append( const char *newtext, int len ){
	const char *p = newtext;

	if ( bLogging ) fwrite( newtext, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p ); 
	while ( p < newtext+len ) {
		char c=*p++;
		switch ( c ) {
		case 0x07: if ( bTitle ) {
						bTitle=false;
						buff[cursor_x]=0;
						cursor_x = line[cursor_y]; 
						//copy_label(buff+cursor_x);
					}
					break;
		case 0x08: --cursor_x; break;
		case 0x09: do { buff[cursor_x++]=' '; } 
					while ( (cursor_x-line[cursor_y])%8!=0 );
					break;
		case 0x1b: 	p = vt100_Escape( p ); break;
		case 0x0d: 	cursor_x = line[cursor_y]; break; 
		case 0x0a: 	cursor_x = line[cursor_y+1];
					if ( bAlterScreen ) { 
						if ( ++cursor_y>roll_bot+screen_y ) {
							cursor_x = line[--cursor_y];
							vt100_Escape("D");// scroll down one line
						}
						break;
					}
		default:	if ( bGraphic ) switch ( c ) {
						case 'q': c='_'; break;	//c=0xc4; break;
						case 'x':				//c=0xb3; break;
						case 't':				//c=0xc3; break;
						case 'u':				//c=0xb4; break;
						case 'm':				//c=0xc0; break;
						case 'j': c='|'; break;	//c=0xd9; break;
						case 'l':				//c=0xda; break;
						case 'k':				//c=0xbf; break;
						default: c = ' ';
					}
					if ( bInsert ) vt100_Escape("[1@");	//insert one space
					attr[cursor_x] = c_attr;
					buff[cursor_x++] = c;
					if ( line[cursor_y+1]<cursor_x ) 
						line[cursor_y+1] = cursor_x;
					if ( cursor_x>=buff_size ) more_chars(); 
					if ( c==0x0a || cursor_x-line[cursor_y]==size_x ) {
						line[++cursor_y] = cursor_x;
						if ( scroll_y<0 ) scroll_y--;
						if ( cursor_y==line_size ) more_lines();
						if ( line[cursor_y+1]<cursor_x ) 
							line[cursor_y+1] = cursor_x;
						if ( screen_y<cursor_y-size_y ) 
							screen_y = cursor_y-size_y;
					}
		}
	}
	if ( strncmp(sPrompt, buff+cursor_x-iPrompt, iPrompt)==0 ) bPrompt=true;
	Fl::awake( this );	//redraw() is slower in responding to key strokes;
}

void Fl_Term::srch( const char *word, int dirn ) {
	if ( word==NULL ) {
		word=fl_input("search for:", keyword);
		if ( word!=NULL ) strncpy(keyword, word, 255);
		else return;
	}
	
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
		append("\n***Log file closed***\n",23);
	}
	else {
		fpLogFile = fopen( fn, "wb+" );
		if ( fpLogFile != NULL ) {
			bLogging = true;
			append("\n***",4);
			append(fn, strlen(fn));
			append(" opened for logging***\n",23);
		}
	}
}
void Fl_Term::save(const char *fn)
{
	FILE *fp = fopen( fn, "wb+" );
	if ( fp != NULL ) {
		fwrite(buff, 1, cursor_x, fp);
		fclose( fp );
		append("\n***buffer saved***\n", 20);
	}
}
const char *Fl_Term::vt100_Escape( const char *sz ) 
{
	bEscape=true; 
	while ( *sz && bEscape ){
		ESC_code[ESC_idx++] = *sz++;
		switch( ESC_code[0] ) {
		case '[':	if ( isalpha(ESC_code[ESC_idx-1]) || 
								 ESC_code[ESC_idx-1]=='@' ) {
			bEscape = false;
			int n0=1, n1=0, n2=0;
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
			case 'A': //cursor up n0 lines
				x = cursor_x-line[cursor_y]; 
				cursor_y-=n0; 
				cursor_x = line[cursor_y]+x; 
				break;
			case 'B': //cursor down n0 lines
				x = cursor_x-line[cursor_y]; 
				cursor_y+=n0; 
				cursor_x = line[cursor_y]+x; 
				break;
			case 'C': //cursor right n0 positions
				cursor_x+=n0; break; 
			case 'D': //cursor left n0 positions
				cursor_x-=n0; break;
			case 'G': //cursor to n0th position from left
				cursor_x = line[cursor_y]+n0-1; break;
			case 'H': //cursor to line n1, postion n2
				if ( n1>size_y ) n1 = size_y;
				if ( n2>size_x ) n2 = size_x;
				cursor_y = screen_y+n1; 
				cursor_x = line[cursor_y]+n2-1;
				break;
			case 'J': {//[J kill till end, 1J begining, 2J entire screen
				int i=0, j=size_y;
				if ( ESC_code[ESC_idx-2]=='[' ) {
					i=cursor_y-screen_y;
					memset( buff+cursor_x, ' ', line[cursor_y+1]-cursor_x );
					memset( attr+cursor_x,   0, line[cursor_y+1]-cursor_x );
				}
				else {
					cursor_y = screen_y+1;
					cursor_x = line[cursor_y];
					if ( n0==1 ) j = cursor_y-screen_y-1;
				}
				char empty[256];
				memset(empty, ' ', 256);
				for ( ; i<j; i++ ) append(empty, size_x);
				}
				break;
			case 'K': {//[K kill till end, 1K begining, 2K entire line
				int i=line[cursor_y], j=line[cursor_y+1];
				if ( ESC_code[ESC_idx-2]=='[' ) i = cursor_x;
				else if ( n0==1 ) j = cursor_x;
				memset(buff+i, ' ', j-i);
				memset(attr+i,   0, j-i);
				}
				break;
			case 'L': //insert lines
				for ( int i=roll_bot; i>cursor_y-screen_y; i-- ) {
					memcpy(buff+line[screen_y+i], buff+line[screen_y+i-n0], size_x);
					memcpy(attr+line[screen_y+i], attr+line[screen_y+i-n0], size_x);
				}
				for ( int i=0; i<n0; i++ ) {
					memset(buff+line[cursor_y+i], ' ', size_x);
					memset(attr+line[cursor_y+i],   0, size_x);
				}
				break; 
			case 'M': //delete lines
				for ( int i=cursor_y-screen_y; i<=roll_bot-n0; i++ ) {
					memcpy(buff+line[screen_y+i], buff+line[screen_y+i+n0], size_x);
					memcpy(attr+line[screen_y+i], attr+line[screen_y+i+n0], size_x);
				}
				for ( int i=0; i<n0; i++ ) {
					memset(buff+line[screen_y+roll_bot+1-i], ' ', size_x);
					memset(attr+line[screen_y+roll_bot+1-i],   0, size_x);
				}
				break;
			case 'P': //remove n0 letters
				for ( int i=cursor_x; i<line[cursor_y+1]-n0; i++ )
					{ buff[i]=buff[i+n0]; attr[i]=attr[i+n0]; }
				line[cursor_y+1]-=n0;
				break;
			case '@': //insert n0 spaces
				for ( int i=line[cursor_y+1]; i>=cursor_x; i-- ) 
					{ buff[i+n0]=buff[i]; attr[i+n0]=attr[i]; }
				for ( int i=0; i<n0; i++ ) 
					{ buff[cursor_x+i]=' '; attr[cursor_x+i]=0; }
				line[cursor_y+1]+=n0;
				break;
			case 'h': 
			case 'l': 
				if ( ESC_code[1]=='4' ) {
					if ( ESC_code[2]=='h' ) bInsert=true;
					if ( ESC_code[2]=='l' ) bInsert=false;
				}
				if ( ESC_code[1]=='?' ) {
					n0 = atoi(ESC_code+2);
					if ( n0==1 ) // ?1h app cursor key, ?1l exit app cursor key
						bAlterScreen = bAppCursor = (ESC_code[ESC_idx-1]=='h');
					if ( n0==1049 ) { 	//?1049h alternate screen, 
						if ( ESC_code[ESC_idx-1]=='h' ) {
							save_y = cursor_y;
							char empty[256];
							memset(empty, ' ', 256);
							for ( int i=0; i<size_y; i++ ) 
								append(empty, size_x);
							bAlterScreen = true;
						}
						else {			//?1049l exit alternate screen
							cursor_y = save_y; 
							cursor_x = line[cursor_y];
							for ( int i=0; i<=size_y; i++ ) 
								line[screen_y+i+1] = 0;
							screen_y -= size_y;
							if ( screen_y<0 ) screen_y=0;
							bAlterScreen = false; 
						}
					}
				}
				break;
			case 'm': //text style, color attributes 
				if ( n0<10 ) c_attr = 0;
				if ( int(n0/10)==3 ) c_attr = n0%10;
				if ( n1==1 && n2/10==3 ) c_attr = n2%10; 
				break;	
			case 'r': roll_top=n1; roll_bot=n2; break;
				}
			}
			break;
		case 'D': // scroll down one line
			for ( int i=roll_top; i<roll_bot; i++ ) {
				memcpy(buff+line[screen_y+i], buff+line[screen_y+i+1], size_x);
				memcpy(attr+line[screen_y+i], attr+line[screen_y+i+1], size_x);
			}
			memset(buff+line[screen_y+roll_bot], ' ', size_x);
			memset(attr+line[screen_y+roll_bot],   0, size_x);
			bEscape = false;
			break;
		case 'M': // scroll up one line
			for ( int i=roll_bot; i>roll_top; i-- ) {
				memcpy(buff+line[screen_y+i], buff+line[screen_y+i-1], size_x);
				memcpy(attr+line[screen_y+i], attr+line[screen_y+i-1], size_x);
			}
			memset(buff+line[screen_y+roll_top], ' ', size_x);
			memset(attr+line[screen_y+roll_top],   0, size_x);
			bEscape = false;
			break; 
		case ']':if ( ESC_code[ESC_idx-1]==';' ) {
					if ( ESC_code[1]=='0' ) bTitle = true;
					bEscape = false;
				 }
				 break;
		case '(':if ( (ESC_code[1]=='B'||ESC_code[1]=='0') ) {
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
void Fl_Term::exec(const char *cmd)
{
	if ( strncmp(cmd, "Timeout", 7)==0 )  iTimeOut=atoi(cmd+8); 
	else if ( strncmp(cmd,"Wait",4)==0 ) sleep(atoi(cmd+5));
	else if ( strncmp(cmd,"Log ",4)==0 ) logg( cmd+4 );
	else if ( strncmp(cmd,"Save",4)==0 ) save( cmd+5 );
	else if ( strncmp(cmd,"Clear",5)==0 ) clear();
	else if ( strncmp(cmd,"Title",5)==0 ) copy_label( cmd+6 );
	else if ( strncmp(cmd,"Prompt",6)==0 ) {
		strncpy(sPrompt, cmd+7, 31); 
		for ( char *p=sPrompt; *p!=0; p++ ) {
			if ( *p=='%' && isdigit(p[1]) ) {
				int a;
				sscanf( p+1, "%02x", &a);
				*p = a;
				strcpy(p+1, p+3);
			}
		}
		iPrompt=strlen(sPrompt);
	} 
}
int Fl_Term::command( const char *cmd, char **response )
{
	if ( *cmd=='#' ) { exec(cmd+1); return 0; }
	if ( host==NULL ) return 0;
	if ( host->state()==HOST_CONNECTED ) {
		bPrompt = false; 
		write( cmd ); write("\015");
		recv0 = cursor_x;
		int oldlen = cursor_x;
		for ( int i=0; i<iTimeOut && !bPrompt; i++ ) {
			sleep(1);
			if ( cursor_x>oldlen ) { i=0; oldlen=cursor_x; }
		}
		if ( response!=NULL ) *response = buff+recv0;	
		return cursor_x-recv0;
	}
	return 0;
}
void *Fl_Term::scripter( void *pv )
{
	Fl_Term *pTerm = (Fl_Term *)pv;
	char *p0=pTerm->get_script(), *p1, *p2;
	do {
		p2=p1=strchr(p0, 0x0a);
		if ( p1==NULL ) p1 = p0+strlen(p0);
		*p1 = 0;

		if (p1-p0>1) pTerm->command( p0, NULL );
		if ( p0!=p1 ) { p0 = p1+1; *p1 = 0x0a; }
	} 
	while ( p2!=NULL );
	return NULL;
}
