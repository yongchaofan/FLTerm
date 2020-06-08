//
// "$Id: tiny2.cxx 24559 2020-06-08 10:05:10 $"
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
\t    * download as small portable signle exe\r\n\n\
\t    * command history and autocompletion\r\n\n\
\t    * text based batch command automation\r\n\n\
\t    * drag and drop to transfer files via scp\r\n\n\
\t    * scripting interface at xmlhttp://127.0.0.1:%d\r\n\n\n\
\thomepage: https://yongchaofan.github.io/tinyTerm2\r\n\n\
\tdownload: https://www.microsoft.com/store/apps/9PBX72DJMZT5\r\n\n\
\tVerision 1.2.0, ©2018-2020 Yongchao Fan, All rights reserved\r\n";

#ifdef WIN32
const char SCP_TO_FOLDER[]="\
var xml = new ActiveXObject(\"Microsoft.XMLHTTP\");\n\
var port = \"8080/?\";\n\
if ( WScript.Arguments.length>0 ) port = WScript.Arguments(0)+\"/?\";\n\
var filename = term(\"!Selection\");\n\
var objShell = new ActiveXObject(\"Shell.Application\");\n\
var objFolder = objShell.BrowseForFolder(0,\"Destination folder\",0x11,\"\");\n\
if ( objFolder ) term(\"!scp :\"+filename+\" \"+objFolder.Self.path);\n\
function term( cmd ) {\n\
	xml.Open (\"GET\", \"http://127.0.0.1:\"+port+cmd, false);\n\
	xml.Send();\n\
	return xml.responseText;\n\
}";
#endif

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
#include <FL/platform.H>				// needed for fl_display
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>

int httport;
void httpd_init();
void httpd_exit();

void tab_cb(Fl_Widget *w);
void localedit_cb(Fl_Widget *w, void *data);
void menu_cb(Fl_Widget *w, void *data);
void term_menu_cb(Fl_Widget *w, void *data);

Fl_Term *pTerm;
Fl_Window *pWindow;
Fl_Tabs *pTabs = NULL;
Fl_Browser_Input *pCmd;
Fl_Sys_Menu_Bar *pMenuBar;
Fl_Menu_Item *pMenuDisconn, *pMenuEcho, *pMenuLogg;
Fl_Font fontface = FL_FREE_FONT;
int fontsize = 16;
int termcols = 80;
int termrows = 25;
int buffsize = 4096;
int keepalive = 0;
int localedit = false;
int sendtoall = false;

Fl_Window *pDialog;
Fl_Choice *pProtocol;
Fl_Input_Choice *pPort;
Fl_Input_Choice *pHostname, *pSettings;
Fl_Button *pConnect;
Fl_Button *pCancel;

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
	if ( data==NULL ) {					//disconnected
		if ( localedit ) 
			term->disp("\r\n\033[32mtinyTerm2 > \033[37m");
		else
			term->disp("\r\n\033[33mPress Enter to restart\033[37m\r\n");
	}
	if ( pTabs!=NULL ) Fl::awake( pTabs );
}
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
	pTerm->textfont(fontface);
	pTerm->textsize(fontsize);
	resize_window(pTerm->sizeX(), pTerm->sizeY());
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
	pDialog->hide();
	if ( pCmd->add(buf)!=0 )
		pMenuBar->insert(pMenuBar->find_index("Script")-1, buf+1, 0, term_menu_cb);
	term_connect(buf+1);
	pMenuDisconn->activate();
}
void cancel_cb(Fl_Widget *w)
{
	w->parent()->hide();
}
void conn_dialog()
{
	pDialog->resize(pWindow->x()+100, pWindow->y()+150, 360, 200);
	pDialog->show();
	pHostname->take_focus();
}

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
			t->disp(cmd);
			t->disp("\n");
			term_connect(cmd);
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
void script_cb(Fl_Widget *w, void *data)
{
	const char *script = pMenuBar->text();
	script_open( script );
}
void term_menu_cb(Fl_Widget *w, void *data)
{
	const char *host = pMenuBar->text();
	term_connect(host);
//	term_connect( data!=NULL? (const char *)data: pMenuBar->text() );
}
void menu_cb(Fl_Widget *w, void *data)
{
	const char *menutext = pMenuBar->text();
	if ( strcmp(menutext, "&Connect...")==0 ) {
		conn_dialog();
	}
	else if ( strcmp(menutext, "&Disconnect")==0 ) {
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
	else if ( strcmp(menutext, "Monaco")==0 ||
			  strcmp(menutext, "Consolas")==0  ) {
		fontface = FL_FREE_FONT;
		pTerm->textfont(fontface);
		pCmd->textfont(fontface);
		resize_window(pTerm->sizeX(), pTerm->sizeY());
	}
	else if ( strcmp(menutext, "Courier New")==0 ) {
		fontface = FL_FREE_FONT+1;
		pTerm->textfont(fontface);
		pCmd->textfont(fontface);
		resize_window(pTerm->sizeX(), pTerm->sizeY());
	}
	else if ( strcmp(menutext, "Menlo")==0 ||
			  strcmp(menutext, "Lucida Console")==0 ) {
		fontface = FL_FREE_FONT+2;
		pTerm->textfont(fontface);
		pCmd->textfont(fontface);
		resize_window(pTerm->sizeX(), pTerm->sizeY());
	}
	else if ( strcmp(menutext, "12")==0 ||
			  strcmp(menutext, "14")==0 ||
			  strcmp(menutext, "16")==0 ||
			  strcmp(menutext, "18")==0 ||
			  strcmp(menutext, "20")==0 ){ 
		fontsize = atoi(menutext);
		pTerm->textsize(fontsize);
		pCmd->textsize(fontsize);
		resize_window(pTerm->sizeX(), pTerm->sizeY());
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
{"&Connect...", 0,	menu_cb},
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
{"Options", 	FL_CMD+'o',0,		0,	FL_SUBMENU},
{"Local &Edit",	FL_CMD+'e',	localedit_cb,0,	FL_MENU_TOGGLE},
{"Send to all",	0,			sendall_cb,0,	FL_MENU_TOGGLE},
{"Font &Face",	0,	0,		0,	FL_SUBMENU},
#ifdef __APPLE__
{"Menlo",		0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"Courier New", 0,	menu_cb,0,	FL_MENU_RADIO},
{"Monaco",		0,	menu_cb,0,	FL_MENU_RADIO},
#else
{"Consolas",	0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"Courier New",	0,	menu_cb,0,	FL_MENU_RADIO},
{"Lucida Console",0,menu_cb,0,	FL_MENU_RADIO},
#endif
{0},
{"Font &Size",	0,	0,		0,	FL_SUBMENU},
{"12",			0,	menu_cb,0,	FL_MENU_RADIO},
{"14",			0,	menu_cb,0,	FL_MENU_RADIO},
{"16",			0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"18",			0,	menu_cb,0,	FL_MENU_RADIO},
{"20",			0,	menu_cb,0,	FL_MENU_RADIO},
{0},
{"&Buffer Size", 0,	0,		0,	FL_SUBMENU},
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
void load_dict(const char *fn)			// set working directory and load scripts to menu
{
#ifdef WIN32
 	if ( GetFileAttributes(fn)==INVALID_FILE_ATTRIBUTES ) 
 	{						// if current directory doesn't have .hist
		_chdir(getenv("USERPROFILE"));
		_mkdir("Documents\\tinyTerm");
		_chdir("Documents\\tinyTerm");
	}
	if ( GetFileAttributes("scp_to_folder.js")==INVALID_FILE_ATTRIBUTES ) {
		FILE *fp = fopen("scp_to_folder.js", "w");
		if ( fp!=NULL ) {
			fprintf(fp, "%s", SCP_TO_FOLDER); 
			fclose(fp);
		}
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
													line+1, 0, term_menu_cb);
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
					Fl::set_font(FL_FREE_FONT, line+10);
					strcpy(name+8, "Font Face/");
					strcat(name, line+10);
				}
				else if ( strncmp(line+1, "FontSize", 8)==0 ) {
					fontsize = atoi(line+10);
					strcpy(name+8, "Font Size/");
					strcat(name, line+10);
				}
				else if ( strncmp(line+1, "WndSize ", 8)==0 ) {
					int w, h;
					sscanf(line+9, "%dx%d", &w, &h);
					pWindow->size(w, h);
				}
				else if ( strncmp(line+1, "BuffSize", 8)==0 ) {
					buffsize = atoi(line+10);
					strcpy(name+8, "Buffer Size/");
					strcat(name, line+10);
				}
				else if ( strcmp(line+1, "LocalEdit")==0 ) {
					localedit_cb(NULL, NULL);
					strcpy(name+8, "Local Edit");
					Fl_Menu_Item *menu=(Fl_Menu_Item *)pMenuBar->find_item(name);
					if ( menu!=NULL ) menu->set();
					continue;
				}
				Fl_Menu_Item *menu=(Fl_Menu_Item *)pMenuBar->find_item(name);
				if ( menu!=NULL ) menu->setonly();
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
		if ( localedit )
			fprintf(fp, "~LocalEdit\n");
		if ( fontsize!=16 ) 
			fprintf(fp, "~FontSize %d\n", fontsize);
		if ( buffsize!=4096 ) 
			fprintf(fp, "~BuffSize %d\n", buffsize);
		fprintf(fp, "~WndSize %dx%d\n", pWindow->w(), pWindow->h());
			
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
#ifdef __APPLE__
	Fl::set_font(FL_FREE_FONT, "Menlo");
	Fl::set_font(FL_FREE_FONT+1, "Courier New");
	Fl::set_font(FL_FREE_FONT+2, "Monaco");
#else
	Fl::set_font(FL_FREE_FONT, "Consolas");
	Fl::set_font(FL_FREE_FONT+1, "Courier New");
	Fl::set_font(FL_FREE_FONT+2, "Lucida Console");
#endif


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

	pDialog = new Fl_Window(360, 200, "Connect");
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
	pDialog->end();
	pDialog->set_modal();

	Fl::lock();
#ifdef WIN32
	pWindow->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	load_dict("tinyTerm.hist");
	pTerm->buffsize(buffsize);
	pTerm->textfont(fontface);
	pTerm->textsize(fontsize);
	pCmd->textfont(fontface);
	pCmd->textsize(fontsize);
	pWindow->show();

	if ( localedit ) 
		pTerm->disp("\n\033[32mtinyTerm2 > \033[37m");
	else
		conn_dialog();

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