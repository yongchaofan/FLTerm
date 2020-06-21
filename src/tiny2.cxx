//
// "$Id: tiny2.cxx 26109 2020-06-18 20:05:10 $"
//
// tinyTerm2 -- FLTK based terminal emulator
//
//    example application using the Fl_Term widget.
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

const char ABOUT_TERM[]="\r\n\
\ttinyTerm2 is a simple, small and scriptable terminal emulator,\r\n\n\
\ta serial/telnet/ssh/sftp/netconf client with unique features:\r\n\n\n\
\t    * cross platform, Windows, macOS and Linux\r\n\n\
\t    * small executable runs on minimum resource\r\n\n\
\t    * command history and autocompletion\r\n\n\
\t    * text based batch command automation\r\n\n\
\t    * drag and drop to transfer files via scp\r\n\n\
\t    * scripting interface at xmlhttp://127.0.0.1:%d\r\n\n\n\
\thomepage: https://yongchaofan.github.io/tinyTerm2\r\n\n\
\tdownload: https://www.microsoft.com/store/apps/9PBX72DJMZT5\r\n\n\
\tVerision 1.2.0, ©2018-2020 Yongchao Fan, All rights reserved\r\n";

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <thread>
#include "host.h"
#include "ssh2.h"
#include "Fl_Term.h"
#include "Fl_Browser_Input.h"
#ifndef WIN32
	#include <pwd.h>
	#include <fnmatch.h>
#endif

#define TABHEIGHT	24
#ifdef __APPLE__
  #define MENUHEIGHT 0
#else
  #define MENUHEIGHT 24
#endif
#include <FL/platform.H>// needed for fl_display
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>

int httport;
void httpd_init();
void httpd_exit();

void tab_cb(Fl_Widget *w);
void localedit_cb(Fl_Widget *w, void *data);
void menu_cb(Fl_Widget *w, void *data);
void conn_menu_cb(Fl_Widget *w, void *data);

Fl_Term *pTerm;
Fl_Window *pWindow;
Fl_Tabs *pTabs = NULL;
Fl_Browser_Input *pCmd;
Fl_Sys_Menu_Bar *pMenuBar;
Fl_Menu_Item *pMenuDisconn, *pMenuEcho, *pMenuLogg;
Fl_Font fontnum = FL_COURIER;
char fontname[256] = "Courier New";
int fontsize = 16;
int termcols = 80;
int termrows = 25;
int wnd_w = 800;
int wnd_h = 600;
int buffsize = 4096;
int localedit = false;
int sendtoall = false;
int keepalive = 0;

#define CHOOSE_FILE 		Fl_Native_File_Chooser::BROWSE_FILE
#define CHOOSE_SAVE_FILE	Fl_Native_File_Chooser::BROWSE_SAVE_FILE
static Fl_Native_File_Chooser fnfc;
const char *file_chooser(const char *title, const char *filter, int type)
{
	fnfc.title(title);
	fnfc.filter(filter);
	fnfc.directory(".");
	fnfc.type(type);
	switch ( fnfc.show() ) {			// Show native chooser
		case -1:  			 			// ERROR
		case  1: return NULL;  			// CANCEL
		default: return fnfc.filename();// FILE CHOSEN
	}
}
void about_cb(Fl_Widget *w, void *data)
{
	char buf[4096];
	sprintf(buf, ABOUT_TERM, httport);
	pTerm->disp(buf);
}
const char *kb_gets(const char *prompt, int echo)
{
	if ( pTerm->connected() ) {
		return pTerm->gets(prompt, echo);
	}
	return NULL;
}
void resize_window(int w, int h)
{
	if ( pTabs!=NULL ) h += TABHEIGHT;
	pWindow->resize(pWindow->x(), pWindow->y(), w, h+MENUHEIGHT);
}

void term_cb(Fl_Widget *w, void *data )	//called when term connection changes
{
	Fl_Term *term = (Fl_Term *)w;
	if ( term==pTerm ) {
		pTerm->connected()  ? pMenuDisconn->activate()
							: pMenuDisconn->deactivate();
		pTerm->echo() ? pMenuEcho->set() : pMenuEcho->clear();
		pTerm->logg() ? pMenuLogg->set() : pMenuLogg->clear();
	}
	if ( data==NULL ) {//disconnected
		if ( localedit )
			term->disp("\r\n\033[32mtinyTerm2> \033[37m");
	}
	if ( pTabs!=NULL ) Fl::awake( pTabs );
}
/*******************************************************************************
*  tab management functions                                                    *
*******************************************************************************/
void tab_init()
{
	pTabs = new Fl_Tabs(0, MENUHEIGHT, pWindow->w(), pWindow->h()-MENUHEIGHT);
	pTabs->when(FL_WHEN_RELEASE|FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED);
	pTabs->callback(tab_cb);
	pTabs->selection_color(FL_CYAN);
	pTabs->end();
	pWindow->remove(pTerm);
	pWindow->insert(*pTabs, pWindow->children()-1);
	pWindow->resizable(pTabs);
	pTerm->resize(0, MENUHEIGHT+TABHEIGHT, pTabs->w(), pTabs->h()-TABHEIGHT);
	pTerm->labelsize(16);
	pTabs->insert(*pTerm, pTabs->children());
	pTabs->resizable(pTerm);
	pTabs->redraw();
}
void tab_act(Fl_Term *pt)
{
	char label[32];
	if ( pTerm!=NULL ) {		//remove "  x" from previous active tab
		strncpy(label, pTerm->label(), 31);
		char *p = strchr(label, ' ');
		if ( p!=NULL ) *p=0;
		pTerm->copy_label(label);
	}

	pTabs->value(pt);
	pTerm = pt;
	pTerm->take_focus();		//add "  x" to current active tab
	pTerm->textfont(fontnum);
	pTerm->textsize(fontsize);
	strncpy(label, pTerm->label(), 24);
	strcat(label, "  x");
	pTerm->copy_label(label);
	pTabs->redraw();
	pTerm->connected() ? pMenuDisconn->activate() : pMenuDisconn->deactivate();
	pTerm->echo() ? pMenuEcho->set() : pMenuEcho->clear();
	pTerm->logg() ? pMenuLogg->set() : pMenuLogg->clear();
}
void tab_new()
{
	if ( pTabs==NULL ) tab_init();
	Fl_Term *pt = new Fl_Term(0, pTabs->y()+TABHEIGHT,
						pTabs->w(), pTabs->h()-TABHEIGHT, "term");
	pt->labelsize(16);
	pt->textsize(fontsize);
	pt->buffsize(buffsize);
	pt->callback( term_cb );
	pTabs->insert(*pt, pTabs->children());
	tab_act(pt);
}
void tab_del()
{
	if ( pTerm->connected() ) pTerm->disconn();
	if ( pTabs->children()>1 ) {
		pTabs->remove(pTerm);
		//Fl::delete_widget(pTerm);
		pTerm = NULL;
		tab_act((Fl_Term *)pTabs->child(0));
	}
	else {
		pTerm->clear();
		pTerm->label("term");
	}
	pTabs->redraw();
}
void tab_cb(Fl_Widget *w)
{
	Fl_Term *pt = (Fl_Term *)pTabs->value();

	if ( pt==pTerm ) {	//clicking on active tab, delete it
		int confirm = 0;
		if ( pTerm->connected() ) confirm =
			fl_choice("Disconnect from %s?", "Yes", "No", 0, pTerm->label());
		if ( confirm==0 ) tab_del();
	}
	else
		tab_act(pt);	//clicking on inactive tab, activate it
}
void term_connect(const char *hostname)
{
	if ( pTerm->connected() ) tab_new();
	pTerm->connect( hostname );
}

int term_act(const char *host)
{
	int rc = 0;
	if ( pTabs==NULL ) return rc;
	for ( int i=0; i<pTabs->children(); i++ )
		if ( strncmp(host, pTabs->child(i)->label(), strlen(host))==0 ) {
			tab_act((Fl_Term *)pTabs->child(i));
			rc = 1;
			break;
		}
	return rc;
}

/*******************************************************************************
*  connection dialog functions                                                 *
*******************************************************************************/
Fl_Window *pConnectDlg;
Fl_Choice *pProtocol;
Fl_Input_Choice *pPort;
Fl_Input_Choice *pHostname, *pSettings;
Fl_Button *pConnect;
Fl_Button *pCancel;

#ifdef WIN32
const char *ports[]={"COM1", "23", "22", "22", "830"};
#else
const char *ports[]={"/dev/tty.usbserial", "23", "22", "22", "830"};
#endif
void protocol_cb(Fl_Widget *w)
{
	static int proto = 2;
	if ( proto==0 ) pSettings->menubutton()->clear();
	proto = pProtocol->value();
	pPort->value(ports[proto]);
	if ( proto==0 ) {
		pHostname->menubutton()->clear();
		pSettings->label("Settings:");
		pSettings->add("9600,n,8,1");
		pSettings->add("19200,n,8,1");
		pSettings->add("38400,n,8,1");
		pSettings->add("57600,n,8,1");
		pSettings->add("115200,n,8,1");
		pSettings->add("230400,n,8,1");
		pSettings->value("9600,n,8,1");
	}
	else {
		pHostname->label("Host:");
		pHostname->value("192.168.1.1");
	}
}
void connect_cb(Fl_Widget *w)
{
	char buf[256]="!";
	int proto = pProtocol->value();
	strcat(buf, pProtocol->mvalue()->label());
	strcat(buf, " ");
	if ( proto>0 ) {
		pHostname->add(pHostname->value());
		strcat(buf, pHostname->value());
		if ( strcmp(ports[proto],pPort->value())!=0 ) {
			strcat(buf, ":");
			strcat(buf, pPort->value());
		}
	}
	else {
		strcat(buf, pPort->value());
		strcat(buf, ":");
		strcat(buf, pSettings->value());
	}
	pConnectDlg->hide();
	if ( pCmd->add(buf)!=0 )
		pMenuBar->insert(pMenuBar->find_index("Script")-1, buf+1, 0, conn_menu_cb);
	term_connect(buf+1);
	pMenuDisconn->activate();
}
void cancel_cb(Fl_Widget *w)
{
	w->parent()->hide();
}
void connect_dialog_build()
{
	pConnectDlg = new Fl_Window(360, 200, "Connect");
	{
		pProtocol = new Fl_Choice(100,20,192,24, "Protocol:");
		pPort = new Fl_Input_Choice(100,60,192,24, "Port:");
		pHostname = new Fl_Input_Choice(100,100,192,24,"Host:");
		pSettings = pHostname;
		pConnect = new Fl_Button(200,160,80,24, "Connect");
		pCancel = new Fl_Button(80,160,80,24, "Cancel");
		pProtocol->textsize(16); pProtocol->labelsize(16);
		pHostname->textsize(16); pHostname->labelsize(16);
		pPort->textsize(16); pPort->labelsize(16);
		pConnect->labelsize(16);
		pConnect->shortcut(FL_Enter);
		pCancel->labelsize(16);
		pProtocol->add("serial|telnet|ssh|sftp|netconf");
		pProtocol->value(2);
		pPort->value("22");
		pHostname->add("192.168.1.1");
		pHostname->value("192.168.1.1");
		pProtocol->callback(protocol_cb);
		pConnect->callback(connect_cb);
		pCancel->callback(cancel_cb);
	}
	pConnectDlg->end();
	pConnectDlg->set_modal();
}
void conn_dialog(Fl_Widget *w, void *data)
{
	pConnectDlg->resize(pWindow->x()+100, pWindow->y()+150, 360, 200);
	pConnectDlg->show();
	pHostname->take_focus();
}

/*******************************************************************************
* font dialog functions                                                        *
*******************************************************************************/
Fl_Window *pFontDlg;
Fl_Hold_Browser *fontobj, *sizeobj;
Fl_Button *donebtn;
int **sizes;
int *numsizes;

void font_cb(Fl_Widget *, long) {
	int sel = fontobj->value();
	if (!sel) return;
	fontnum = long(fontobj->data(sel));
	pTerm->textfont(fontnum);
	pCmd->textfont(fontnum);
	resize_window(pTerm->sizeX(), pTerm->sizeY());
	sizeobj->clear();
	int n = numsizes[fontnum];
	int *s = sizes[fontnum];
	if (!n) {// no sizes
	} 
	else if (s[0] == 0) {// many sizes;
		int j = 1;
		for (int i=6; i<=32 || i<s[n-1]; i++) {
			char buf[20];
			sprintf(buf,"%d",i);
			sizeobj->add(buf);
			if ( i==fontsize ) sizeobj->value(sizeobj->size());
		}
	} 
	else {// some sizes
		int w = 0;
		for (int i = 0; i < n; i++) {
			char buf[20];
			sprintf(buf,"@b%d",s[i]);
			sizeobj->add(buf);
			if ( s[i]==fontsize ) sizeobj->value(sizeobj->size());
		}
	}
}
void size_cb(Fl_Widget *, long) {
	int i = sizeobj->value();
	if (!i) return;
	const char *c = sizeobj->text(i);
	while (*c < '0' || *c > '9') c++;
	fontsize = atoi(c);
	pTerm->textsize(fontsize);
	pCmd->textsize(fontsize);
	resize_window(pTerm->sizeX(), pTerm->sizeY());
}
void font_dialog_build()
{
	pFontDlg = new Fl_Double_Window(400, 240, "Font Dialog");
	{
		fontobj = new Fl_Hold_Browser(10, 20, 290, 210, "Face:");
		fontobj->align(FL_ALIGN_TOP|FL_ALIGN_LEFT);
		fontobj->box(FL_FRAME_BOX);
		fontobj->color(53,3);
		fontobj->callback(font_cb);
		sizeobj = new Fl_Hold_Browser(310, 20, 80, 180, "Size:");
		sizeobj->align(FL_ALIGN_TOP|FL_ALIGN_LEFT);
		sizeobj->box(FL_FRAME_BOX);
		sizeobj->color(53,3);
		sizeobj->callback(size_cb);
		donebtn = new Fl_Button(310, 206, 80, 24, "Done");
		donebtn->callback(cancel_cb);
	}
	pFontDlg->end();
	pFontDlg->set_modal();
}
void font_dialog_init()
{
	int k = Fl::set_fonts(NULL);
	sizes = new int*[k];
	numsizes = new int[k];
	for (long i = 0; i < k; i++) {
		int t; 
		const char *name = Fl::get_font_name((Fl_Font)i,&t);
		if (t==0 ) {
			fontobj->add(name, (void *)i);
			if ( strcmp(fontname, name)==0 ) { 
				fontobj->select(fontobj->size());
				fontnum = i;
			}
			int *s;
			int n = Fl::get_font_sizes((Fl_Font)i, s);
			numsizes[i] = n;
			if (n) {
				sizes[i] = new int[n];
				for (int j=0; j<n; j++) sizes[i][j] = s[j];
			}
		}
	}
}
void font_dialog(Fl_Widget *w, void *data)
{
	pFontDlg->resize(pWindow->x()+200, pWindow->y()+100, 400, 240);
	pFontDlg->show();
	fontobj->take_focus();
}

/*******************************************************************************
* scripting functions                                                          *
*******************************************************************************/
void script_open( const char *fn )
{
	const char *ext = fl_filename_ext(fn);
	if ( ext!=NULL ) {
		if ( strcmp(ext, ".html")==0 ) {
			char url[1024], msg[1024];
			sprintf(url, "http://127.0.0.1:%d/%s", httport, fn);
 			if ( !fl_open_uri(url, msg, 1024) ) fl_alert("Error:%s",msg);
			return;
		}
	}
	pTerm->learn_prompt();
#ifdef WIN32
	char http_port[16];
	sprintf(http_port, "%d", httport);
	ShellExecuteA(NULL, "open", fn, http_port, NULL, SW_SHOW);
#else
	system(fn);
#endif
}
void script_cb(Fl_Widget *w, void *data)
{
	const char *script = pMenuBar->text();
	script_open( script );
}

/*******************************************************************************
* command editor functions                                                     *
*******************************************************************************/
void cmd_send(Fl_Term *t, const char *cmd)
{
	if ( *cmd=='!' ) {
		if ( strncmp(cmd, "!scp ", 5)==0 ) {
			std::thread scp_thread(&Fl_Term::scp, t,
									strdup(cmd+5), (char **)NULL);
			scp_thread.detach();
		}
		else if ( strncmp(cmd, "!tun", 4)==0 ) {
			std::thread scp_thread(&Fl_Term::tun, t,
									strdup(cmd+4), (char **)NULL);
			scp_thread.detach();
		}
		else if ( strncmp(cmd, "!script ",8)==0 )
			script_open(cmd+8);
		else
			t->command(cmd, NULL);
	}
	else {
		if ( t->connected() ) {
			t->send(cmd);
			t->send("\r");
		}
		else {
			 if ( *cmd ) {
				t->disp(cmd);
				t->disp("\n");
				term_connect(cmd);
			}
			else
				pTerm->send("\r");
		}
	}
}
void cmd_cb(Fl_Widget *o, void *p)
{
	if ( p!=NULL ) { 						//from do_callback
		pTerm->send((const char *)p);
		return;
	}

	const char *cmd = pCmd->value();		//normal callback
	pCmd->add( cmd );
	if ( !sendtoall || pTabs==NULL ) {
		cmd_send(pTerm, cmd);
	}
	else {
		for ( int i=0; i<pTabs->children(); i++ )
			cmd_send((Fl_Term *)pTabs->child(i), cmd);
	}
	pCmd->value("");
}
void localedit_cb(Fl_Widget *w, void *data)
{
	localedit = !localedit;
	if ( !localedit ) {
		pCmd->resize(0, 0, 1, 1);
		pTerm->take_focus();
	}
	pTerm->redraw();
}
void sendall_cb(Fl_Widget *w, void *data)
{
	sendtoall = !sendtoall;
	pCmd->color(sendtoall?0xC0000000:FL_BLACK);
	pCmd->redraw();
}
int move_editor(int x, int y, int w, int h)
{
	if ( localedit ) {
		pCmd->resize(x, y, w, h);
		pCmd->take_focus();
	}
	return localedit;
}

/*******************************************************************************
* menu call back functions                                                     *
*******************************************************************************/
void conn_menu_cb(Fl_Widget *w, void *data)
{
	const char *host = pMenuBar->text();
	term_connect(host);
}
void menu_cb(Fl_Widget *w, void *data)
{
	const char *menutext = pMenuBar->text();
	if ( strcmp(menutext, "&Disconnect")==0 ) {
		pTerm->disconn();
		pMenuDisconn->deactivate();
	}
	else if ( strcmp(menutext, "&Logging...")==0 ) {
		if ( pTerm->logg() ) {
			pTerm->logg( NULL );
		}
		else {
			const char *fname = file_chooser("logfile:", "Log\t*.log",
														CHOOSE_SAVE_FILE);
			if ( fname!=NULL ) pTerm->logg(fname);
		}
		if ( pTerm->logg() )
			pMenuLogg->set();
		else
			pMenuLogg->clear();
	}
	else if ( strcmp(menutext, "local &Echo")==0 ) {
		pTerm->echo(!pTerm->echo());
	}
	else if ( strcmp(menutext, "&Run...")==0 ) {
		const char *fname = file_chooser("script:", "All\t*.*", CHOOSE_FILE);
		if ( fname!=NULL ) {
			char relative_name[FL_PATH_MAX+8]="!script ";
			fl_filename_relative(relative_name+8, FL_PATH_MAX, fname);
			pCmd->add(relative_name);
			script_open(relative_name+8);
		}
	}
	else if ( strcmp(menutext, "&Pause")==0 ) {
		pTerm->pause_script( );
	}
	else if ( strcmp(menutext, "&Quit")==0 ) {
		pTerm->quit_script();
	}
	else if ( strcmp(menutext, "2048")==0 ||
			  strcmp(menutext, "4096")==0 ||
			  strcmp(menutext, "8192")==0 ||
			  strcmp(menutext, "16384")==0||
			  strcmp(menutext, "32768")==0 ){
		buffsize = atoi(menutext);
		pTerm->buffsize(buffsize);
	}
}
void close_cb(Fl_Widget *w, void *data)
{
	if ( pTabs==NULL ) {	//not multi-tab
		if ( pTerm->connected() ) {
			if ( fl_choice("Disconnect and exit?", "Yes", "No", 0)==1 ) return;
			pTerm->disconn();
		}
	}
	else {						//multi-tabbed
		int active = pTerm->connected();
		if ( !active ) for ( int i=0; i<pTabs->children(); i++ ) {
			Fl_Term *pt = (Fl_Term *)pTabs->child(i);
			if ( pt->connected() ) active = true;
		}
		if ( active ) {
			if ( fl_choice("Disconnect all and exit?", "Yes", "No", 0)==1 )
				return;
		}
		while ( pTabs->children()>1 ) tab_del();
	}
	pCmd->close();
	pWindow->hide();
}
#ifdef __APPLE__
#define FL_CMD FL_META
#else
#define FL_CMD FL_ALT
#endif
Fl_Menu_Item menubar[] = {
{"Term", 		FL_CMD+'t',0,	0,	FL_SUBMENU},
{"&Connect...", 0,	conn_dialog},
{"&Disconnect", 0,	menu_cb},
{"local &Echo",	0,	menu_cb,0,	FL_MENU_TOGGLE},
{"&Logging...",	0,	menu_cb,0,	FL_MENU_TOGGLE|FL_MENU_DIVIDER},
//{"ftpd ",		0, 	daemon_host_cb},
//{"tftpd ",		0,	daemon_host_cb},
{0},
{"Script",		FL_CMD+'s',0,	0,	FL_SUBMENU},
{"&Run...",		0,	menu_cb},
{"&Pause",		0,	menu_cb},
{"&Quit",		0,	menu_cb,0,	FL_MENU_DIVIDER},
{0},
{"Options", 	FL_CMD+'o',	0,			0,	FL_SUBMENU},
{"&Font...",	FL_CMD+'f',	font_dialog,0},
{"Local &Edit",	FL_CMD+'e',	localedit_cb,0,	FL_MENU_TOGGLE},
{"Send to all",	0,			sendall_cb,	0,	FL_MENU_TOGGLE},
{"&Buffer Size",0,			0,			0,	FL_SUBMENU},
{"2048",		0,	menu_cb,0,	FL_MENU_RADIO},
{"4096",		0,	menu_cb,0,	FL_MENU_RADIO},
{"8192",		0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"16384",		0,	menu_cb,0,	FL_MENU_RADIO},
{"32768",		0,	menu_cb,0,	FL_MENU_RADIO},
{0},
{"&About tinyTerm2", 0, about_cb},
{0},
{0}
};

/*******************************************************************************
*  dictionary functions, load at start, save at end of program                 *
*******************************************************************************/
// load_dict set working directory and load scripts to menu
void load_dict(const char *fn)			
{
#ifdef WIN32
 	if ( GetFileAttributes(fn)==INVALID_FILE_ATTRIBUTES ) {
	// current directory doesn't have .hist, change to home directory
		_chdir(getenv("USERPROFILE"));
		_mkdir("Documents\\tinyTerm");
		_chdir("Documents\\tinyTerm");
	}
#else
	char *homedir = getenv("HOME");
	if ( homedir==NULL ) homedir = getpwuid(getuid())->pw_dir;
	chdir(homedir);
	mkdir("tinyTerm", 0755);
	chdir("tinyTerm");
#endif

	FILE *fp = fopen(fn, "r");
	if ( fp!=NULL ) {
		char line[256];
		while ( fgets(line, 256, fp)!=NULL ) {
			line[strcspn(line, "\n")] = 0;
			pCmd->add(line);
			if ( *line=='!' ) {
				if (strncmp(line+1, "ssh ",   4)==0 ||
					strncmp(line+1, "sftp ",  5)==0 ||
					strncmp(line+1, "telnet ",7)==0 ||
					strncmp(line+1, "serial ",7)==0 ||
					strncmp(line+1, "netconf ",8)==0 ) {
					pMenuBar->insert(pMenuBar->find_index("Script")-1,
													line+1, 0, conn_menu_cb);
					pHostname->add( strchr(line+1, ' ')+1 );
				}
				if (strncmp(line+1, "script ", 7)==0 ) {
					pMenuBar->insert(pMenuBar->find_index("Options")-1,
										line+8, 0, script_cb);
				}
			}
			if ( *line=='~' ) {
				char name[256] = "Options/";
				if ( strncmp(line+1, "FontFace", 8)==0 ) {
					strncpy(fontname, line+10, 255);
				}
				else if ( strncmp(line+1, "FontSize", 8)==0 ) {
					fontsize = atoi(line+10);
				}
				else if ( strncmp(line+1, "WndSize ", 8)==0 ) {
					sscanf(line+9, "%dx%d", &wnd_w, &wnd_h);
				}
				else if ( strncmp(line+1, "BuffSize", 8)==0 ) {
					buffsize = atoi(line+10);
					strcpy(name+8, "Buffer Size/");
					strcat(name, line+10);
					Fl_Menu_Item *menu=(Fl_Menu_Item *)pMenuBar->find_item(name);
					if ( menu!=NULL ) menu->setonly();
				}
				else if ( strcmp(line+1, "LocalEdit")==0 ) {
					localedit_cb(NULL, NULL);
					strcpy(name+8, "Local &Edit");
					Fl_Menu_Item *menu=(Fl_Menu_Item *)pMenuBar->find_item(name);
					if ( menu!=NULL ) menu->set();
				}
			}
		}
		fclose(fp);
	}
	pMenuDisconn = (Fl_Menu_Item *)pMenuBar->find_item("Term/&Disconnect");
	pMenuLogg = (Fl_Menu_Item *)pMenuBar->find_item("Term/&Logging...");
	pMenuEcho = (Fl_Menu_Item *)pMenuBar->find_item("Term/local &Echo");
}
void save_dict(const char *fn)
{
	FILE *fp = fopen(fn, "w");
	if ( fp!=NULL ) {
		int t;
		fprintf(fp, "~WndSize %dx%d\n", pWindow->w(), pWindow->h());
		fprintf(fp, "~FontFace %s\n", Fl::get_font_name(fontnum, &t));
		fprintf(fp, "~FontSize %d\n", fontsize);
		if ( localedit )
			fprintf(fp, "~LocalEdit\n");
		if ( buffsize!=4096 )
			fprintf(fp, "~BuffSize %d\n", buffsize);

		const char *p = pCmd->first();
		while ( p!=NULL ) {
			if ( *p!='~' ) fprintf(fp, "%s\n", p);
			p = pCmd->next();
		}
		fclose(fp);
	}
}
int main(int argc, char **argv)
{
	httpd_init();
	libssh2_init(0);
	Fl::scheme("gtk+");

	pWindow = new Fl_Double_Window(800, 640, "tinyTerm2");
	{
		pMenuBar=new Fl_Sys_Menu_Bar(0, 0, pWindow->w(), MENUHEIGHT);
		pMenuBar->menu(menubar);
		pMenuBar->textsize(18);
		pTerm = new Fl_Term(0, MENUHEIGHT, pWindow->w(),
								pWindow->h()-MENUHEIGHT, "term");
		pTerm->callback( term_cb );
		pCmd = new Fl_Browser_Input( 0, pWindow->h()-1, 1, 1, "");
		pCmd->box(FL_FLAT_BOX);
		pCmd->color(FL_BLACK);
		pCmd->textcolor(FL_YELLOW);
		pCmd->cursor_color(FL_WHITE);
		pCmd->when(FL_WHEN_ENTER_KEY_ALWAYS);
		pCmd->callback(cmd_cb);
	}
	pWindow->callback(close_cb);
	pWindow->resizable(pTerm);
	pWindow->end();
	connect_dialog_build();
	font_dialog_build();


	Fl::lock();
#ifdef WIN32
	pWindow->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	load_dict("tinyTerm.hist");
	font_dialog_init();
	pTerm->buffsize(buffsize);
	pTerm->textfont(fontnum);
	pTerm->textsize(fontsize);
	pCmd->textfont(fontnum);
	pCmd->textsize(fontsize);
	pWindow->size(wnd_w, wnd_h);
	pWindow->show();
	
	if ( localedit ) {
		pTerm->disp("\n\033[32mtinyTerm2> \033[37m");
		struct stat sb;
		if ( fl_stat("tinyTerm.html", &sb)!=-1 ) 
			script_open("tinyTerm.html");
	}
	else
		conn_dialog(NULL, NULL);

	while ( Fl::wait() ) {
		Fl_Widget *pt = (Fl_Widget *)Fl::thread_message();
		if ( pt!=NULL )pt->redraw();
	}
	save_dict("tinyTerm.hist");
	libssh2_exit();
	httpd_exit();
	return 0;
}
/**********************************HTTPd**************************************/
const char HEADER[]="HTTP/1.1 200 OK\nServer: tinyTerm2\n\
Access-Control-Allow-Origin: *\nContent-Type: text/plain\n\
Cache-Control: no-cache\nContent-length: %d\n\n";

const char *RFC1123FMT="%a, %d %b %Y %H:%M:%S GMT";
const char *exts[]={".txt",
					".htm", ".html",
					".js",
					".jpg", ".jpeg",
					".png",
					".css"
					};
const char *mime[]={"text/plain",
					"text/html", "text/html",
					"text/javascript",
					"image/jpeg", "image/jpeg",
					"image/png",
					"text/css"
					};
void httpFile( int s1, char *file)
{
	char reply[4096], timebuf[128];
	int len, i, j;
	time_t now;

	now = time( NULL );
	strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );

	struct stat sb;
	if ( stat( file, &sb ) ==-1 ) {
		len=sprintf(reply, "HTTP/1.1 404 not found\nDate: %s\n", timebuf);
		len+=sprintf(reply+len, "Server: tinyTerm2\nConnection: close");
	    len+=sprintf(reply+len, "Content-Type: text/html\nContent-Length: 14");
	    len+=sprintf(reply+len, "\n\nfile not found");
		send(s1, reply, len, 0);
		return;
	}

	FILE *fp = fopen( file, "rb" );
	if ( fp!=NULL ) {
		len=sprintf(reply, "HTTP/1.1 200 Ok\nDate: %s\n", timebuf);
		len+=sprintf(reply+len, "Server: tinyTerm2\nConnection: close");

		const char *filext=strrchr(file, '.');
		if ( filext!=NULL ) {
			for ( i=0, j=0; j<8; j++ )
				if ( strcmp(filext, exts[j])==0 ) i=j;
		}
		len+=sprintf(reply+len,"Content-Type: %s\n",mime[i]);

		long filesize = sb.st_size;
		len+=sprintf(reply+len, "Content-Length: %ld\n", filesize );
		strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &sb.st_mtime ));
		len+=sprintf(reply+len, "Last-Modified: %s\n\n", timebuf );

		send(s1, reply, len, 0);
		while ( (len=fread(reply, 1, 4096, fp))>0 )
			if ( send(s1, reply, len, 0)==-1)	break;
		fclose(fp);
	}
}
void httpd( int s0 )
{
	struct sockaddr_in cltaddr;
	socklen_t addrsize=sizeof(cltaddr);
	char buf[4096], *cmd, *reply;
	int cmdlen, replen, http_s1;

	while ( (http_s1=accept(s0,(struct sockaddr*)&cltaddr,&addrsize ))!=-1 ) {
		while ( (cmdlen=recv(http_s1,buf,4095,0))>0 ) {
			buf[cmdlen] = 0;
			if ( strncmp(buf, "GET /",5)==0 ) {
				cmd = buf+5;
				char *p = strchr(cmd, ' ');
				if ( p!=NULL ) *p = 0;
				for ( char *p=cmd; *p; p++ )
					if ( *p=='+' ) *p=' ';
				fl_decode_uri(cmd);

				if ( *cmd=='?' ) {
					if ( strncmp(++cmd, "!Tab ",5)==0 ) {
						replen = term_act(cmd+5);
						if ( replen )
							reply = cmd;
						else
							tab_new();
					}
					else
						replen = pTerm->command( cmd, &reply );
					int len = sprintf( buf, HEADER, replen );
					send( http_s1, buf, len, 0 );
					if ( replen>0 ) send( http_s1, reply, replen, 0 );
				}
				else
					httpFile(http_s1, cmd);
			}
		}
		closesocket(http_s1);
	}
}
static int http_s0 = -1;
void httpd_init()
{
#ifdef WIN32
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2,0), &wsadata);
#endif
	http_s0 = socket(AF_INET, SOCK_STREAM, 0);
	if ( http_s0 == -1 ) return;

	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=inet_addr("127.0.0.1");
	short port = 8079;
	while ( ++port<8100 ) {
		svraddr.sin_port=htons(port);
		if ( bind(http_s0, (struct sockaddr*)&svraddr, addrsize)!=-1 )
			break;
	}
	if ( port<8100) {
		if ( listen(http_s0, 1)!=-1){
			std::thread httpThread( httpd, http_s0 );
			httpThread.detach();
			httport = port;
			return;
		}
	}
	closesocket(http_s0);
}
void httpd_exit()
{
	closesocket(http_s0);
}