//
// "$Id: flTerm.cxx 19951 2019-01-31 21:05:10 $"
//
// tinyTerm2 -- FLTK based terminal emulator
//
//    example application using the Fl_Term widget.
//
// Copyright 2017-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/flTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/flTerm/issues/new
//

const char ABOUT_TERM[]="\n\
\ttinyTerm2 is a simple, small and scriptable terminal emulator,\n\n\
\ta serial/telnet/ssh/sftp/netconf client with unique features:\n\n\n\
\t    * cross platform, Windows, macOS and Linux\n\n\
\t    * win64 portable exe smaller than 640KB\n\n\
\t    * command history and autocompletion\n\n\
\t    * text based batch command automation\n\n\
\t    * drag and drop to transfer files via scp\n\n\
\t    * scripting interface at xmlhttp://127.0.0.1:%s\n\n\n\
\tmacOS:  https://www.microsoft.com/store/apps/9NXGN9LJTL05\n\n\
\tWindows: https://www.microsoft.com/store/apps/9NXGN9LJTL05\n\n\
\thomepage: https://yongchaofan.github.io/tinyTerm2/\n\n\n\
\tVerision 1.1, �2018-2019 Yongchao Fan, All rights reserved\r\n";

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
#else
const char SCP_TO_HOME[]="#!/bin/sh\n\
FILENAME=`curl \"http://127.0.0.1:8080/%3F%21Selection\"`\n\
curl \"http://127.0.0.1:8080/%3F%21scp%20%3A$FILENAME%20%2E%2E\"";
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef WIN32
	#include <direct.h>
	#include <process.h>
#else
	#include <pwd.h>
#endif
#include <thread>
#include "host.h"
#include "ssh2.h"
#include "Fl_Term.h"
#include "Fl_Browser_Input.h"

#define TABHEIGHT 	24
#ifdef __APPLE__
  #define MENUHEIGHT 0
#else
  #define MENUHEIGHT 24
#endif
#include <FL/platform.H>               // needed for fl_display
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

char http_port[16];
void httpd_init();
void httpd_exit();

void tab_cb(Fl_Widget *w);
void editor_cb(Fl_Widget *w, void *data);
void menu_cb(Fl_Widget *w, void *data);
void menu_host_cb(Fl_Widget *w, void *data);

Fl_Term *pTerm;
Fl_Window *pWindow;
Fl_Tabs *pTabs = NULL;
Fl_Browser_Input *pCmd;
Fl_Sys_Menu_Bar *pMenuBar;
Fl_Menu_Item *pMenuDisconn, *pMenuEcho, *pMenuLogg;
int textsize = 14;
int buffsize = 4096;
int keepalive = 0;
int localedit = false;

Fl_Window *pDialog;
Fl_Choice *pProtocol;
Fl_Input_Choice *pPort;
Fl_Input_Choice *pHostname, *pSettings;
Fl_Button *pConnect;
Fl_Button *pCancel;

#define CHOOSE_FILE 		Fl_Native_File_Chooser::BROWSE_FILE
#define CHOOSE_DIR			Fl_Native_File_Chooser::BROWSE_DIRECTORY 	
#define CHOOSE_SAVE_FILE 	Fl_Native_File_Chooser::BROWSE_SAVE_FILE
#define CHOOSE_SAVE_DIR 	Fl_Native_File_Chooser::BROWSE_SAVE_DIRECTORY
static Fl_Native_File_Chooser fnfc;
const char *file_chooser(const char *title, const char *filter, int type,
						 const char *fn="")
{
	fnfc.title(title);
	fnfc.filter(filter);
	fnfc.directory(".");
	fnfc.preset_file(fn);
	fnfc.type(type);
	if ( fnfc.show()==0 ) {	// Show native chooser
		if ( fnfc.count()==1 ) 
			return fnfc.filename();	
	}
	return NULL;  			// cancel or error
}
void about_cb(Fl_Widget *w, void *data)
{
	char buf[4096];
	sprintf(buf, ABOUT_TERM, http_port);
	pTerm->disp(buf);
}
const char *kb_gets(const char *prompt, int echo)
{
	if ( pTerm->live() ) {
		return pTerm->ssh_gets(prompt, echo);
	}
	return NULL;
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
	pTerm->resize(0, MENUHEIGHT+TABHEIGHT, pTabs->w(), 
								pTabs->h()-TABHEIGHT);
	pTerm->labelsize(16);
	pTabs->insert(*pTerm, pTabs->children());
	pTabs->resizable(pTerm);
	pTabs->redraw();
	pWindow->label(pTerm->title());
	pWindow->redraw();
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
	strncpy(label, pTerm->label(), 24);
	strcat(label, "  x");
	pTerm->copy_label(label);
	pTabs->redraw();

	if ( pTerm->live() ) 		//update "Term" menu items
		pMenuDisconn->activate();
	else
		pMenuDisconn->deactivate();
	if ( pTerm->echo() ) 
		pMenuEcho->set();
	else
		pMenuEcho->clear();
	if ( pTerm->logg() )
		pMenuLogg->set();
	else
		pMenuLogg->clear();

	pWindow->label(pTerm->title());
	pWindow->redraw();
}
void tab_new()
{
	if ( pTabs==NULL ) tab_init();
	Fl_Term *pt = new Fl_Term(0, pTabs->y()+TABHEIGHT, 
						pTabs->w(), pTabs->h()-TABHEIGHT, "term");
	pt->labelsize(16);
	pt->textsize(textsize);
	pt->buffsize(buffsize);
	pTabs->insert(*pt, pTabs->children());
	tab_act(pt);
}
void tab_del()
{
	if ( pTerm->live() ) pTerm->connect("disconn");
	if ( pTabs->children()>1 ) {
		pTabs->remove(pTerm);
	//	Fl::delete_widget(pTerm); 
		pTerm = NULL;
		tab_act((Fl_Term *)pTabs->child(0));
	}
	else {
		pTerm->clear();
		pTerm->label("term");
		pWindow->label("tinyTerm2");
	}
	pTabs->redraw();
}
void tab_cb(Fl_Widget *w) 
{
	Fl_Term *pt = (Fl_Term *)pTabs->value();

	if ( pt==pTerm ) { 	//clicking on active tab, delete it
		int confirm = 0;
		if ( pTerm->live() ) confirm = 
			fl_choice("Disconnect from %s?", "Yes", "No", 0, pTerm->label());
		if ( confirm==0 ) tab_del();
	}
	else
		tab_act(pt);	//clicking on inactive tab, activate it
}

int term_act(const char *host)
{
	int rc = 0;
	for ( int i=0; i<pTabs->children(); i++ )
		if ( strncmp(host, pTabs->child(i)->label(), strlen(host))==0 ) {
			tab_act((Fl_Term *)pTabs->child(i));
			rc = 1;
			break;
		}
	return rc;
}
const char *ports[]={
#ifdef WIN32
	"COM1",
#else
	"/dev/tty.usbserial",
#endif
	"23", "22", "22", "830"
};
void protocol_cb(Fl_Widget *w)
{
	static int proto = 2;
	if ( proto==0 ) 
		pSettings->menubutton()->clear();
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
		pMenuBar->insert(7, buf+1, 0, menu_host_cb);
	if ( pTerm->live() ) tab_new();
	pTerm->connect(buf+1);
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
void cmd_cb(Fl_Widget *o) 
{
	const char *cmd = pCmd->value();
	pCmd->add( cmd );
	switch( *cmd ) {
		case '!':	if ( strncmp(cmd, "!scp ", 5)==0 ) {
						std::thread scp_thread(&Fl_Term::scp, pTerm, 
												strdup(cmd+5), (char **)NULL);
						scp_thread.detach();
					}
					else if ( strncmp(cmd, "!tun", 4)==0 ) {
						std::thread scp_thread(&Fl_Term::tun, pTerm, 
												strdup(cmd+4), (char **)NULL);
						scp_thread.detach();
					}
					else
						pTerm->cmd(cmd, NULL);
					break;
		default: 	if ( pTerm->live() ) {
						pTerm->send(cmd);
						pTerm->send("\r");
					}
	}
	pCmd->value("");
}
void editor_cb(Fl_Widget *w, void *data)
{
	localedit = !localedit;
	if ( !localedit ) {
		pCmd->resize(0, 0, 1, 1);
		pTerm->take_focus();
	}
	pTerm->redraw();
}
int move_editor(int x, int y, int w, int h)
{
	if ( localedit ) {
		pCmd->resize(x, y, w, h);
		pCmd->take_focus();
	}
	return localedit;
}
void script_open(const char *fname)
{
	struct stat sb;
	if ( stat(fname, &sb)!=-1 ) {
		char *script = (char *)malloc(sb.st_size+1);
		if ( script!=NULL ) {
			FILE *fp = fopen(fname, "r");
			if ( fp!=NULL ) {
				fread(script, 1, sb.st_size, fp);
				fclose(fp);
				script[sb.st_size]=0;
				pTerm->run_script(script);
			}
		}
	}
}
void script_cb(Fl_Widget *w, void *data)
{
	char fn[256] = "script/";
	strncat(fn, pMenuBar->text(), 248);
	fn[255] = 0;
	pCmd->value(fn);
	pTerm->learn_prompt();
#ifdef WIN32
	int len = strlen(fn);
	if ( strcmp(fn+len-3, ".js")==0 || strcmp(fn+len-4, ".vbs")==0 ) {
		_spawnlp(_P_NOWAIT, "WScript.exe","WScript.exe",fn,http_port,NULL);
	}
#else
	struct stat sb;
	if ( stat(fn, &sb)==0 ) {
		if ( sb.st_mode&S_IXUSR ) 
			if ( fork()==0 ) execlp(fn, http_port, NULL);
	}
#endif
	else
		script_open(fn);
}
void menu_cb(Fl_Widget *w, void *data)
{
	const char *menutext = pMenuBar->text();
	if ( strcmp(menutext, "&Connect...")==0 ) {
		conn_dialog();
	}
	else if ( strcmp(menutext, "&Disconnect")==0 ) {
		pTerm->connect("disconn");
		pMenuDisconn->deactivate();
	}
	else if ( strcmp(menutext, "Logging...")==0 ) {
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
	else if ( strcmp(menutext, "Local Echo")==0 ) {
		pTerm->echo(!pTerm->echo());
	}
	else if ( strcmp(menutext, "Run...")==0 ) {
		const char *fname = file_chooser("script file:","Text\t*.txt", 
														CHOOSE_FILE);
		if ( fname!=NULL ) script_open(fname);
	}
	else if ( strcmp(menutext, "Pause")==0 ) {
		pTerm->pause_script();
	}
	else if ( strcmp(menutext, "Stop")==0 ) {
		pTerm->stop_script();
	}
	else if ( strcmp(menutext, "Courier New")==0 ||
			  strcmp(menutext, "Monaco")==0 ||
			  strcmp(menutext, "Menlo")==0 ||
			  strcmp(menutext, "Consolas")==0 ||
			  strcmp(menutext, "Lucida Console")==0 ) {
		Fl::set_font(FL_COURIER, menutext);
		pTerm->textsize();
	}
	else if ( strcmp(menutext, "12")==0 ||
			  strcmp(menutext, "14")==0 ||
			  strcmp(menutext, "16")==0 ||
			  strcmp(menutext, "18")==0 ||
			  strcmp(menutext, "20")==0 ){ 
		textsize = atoi(menutext);
		pTerm->textsize(textsize);
		pCmd->textsize(textsize);
	}
	else if ( strcmp(menutext, "2048")==0 ||
			  strcmp(menutext, "4096")==0 ||
			  strcmp(menutext, "8192")==0 ||
			  strcmp(menutext, "16384")==0||
			  strcmp(menutext, "32768")==0 ){
		buffsize = atoi(menutext);
		pTerm->buffsize(buffsize);
	}
	else if ( strcmp(menutext, "0 disable")==0 ||
			  strcmp(menutext, "1 minute" )==0 ||
			  strcmp(menutext, "5 minutes")==0 ||
			  strcmp(menutext, "15minutes")==0 ){
		keepalive = atoi(menutext)*60;
		pTerm->keepalive(keepalive);
	}
}
void menu_host_cb(Fl_Widget *w, void *data)
{
	if ( pTerm->live() ) tab_new();
	pTerm->connect(pMenuBar->text());
	pMenuDisconn->activate();
}
void daemon_host_cb(Fl_Widget *w, void *data)
{
	char cmd[MAX_PATH];
	const char *fname = file_chooser("choose root directory", "*", CHOOSE_DIR);
	if ( fname!=NULL ) {
		if ( pTerm->live() ) tab_new();
		strcpy(cmd, pMenuBar->text());
		strcat(cmd, fname);
		pTerm->connect(cmd);
	}
}
void close_cb(Fl_Widget *w, void *data)
{
	if ( pTabs==NULL ) {	//not multi-tab
		if ( pTerm->live() ) {
			if ( fl_choice("Disconnect and exit?", "Yes", "No", 0)==1 ) return;
			pTerm->connect("disconn");
		}
	}
	else {						//multi-tabbed
		int active = pTerm->live();
		if ( !active ) for ( int i=0; i<pTabs->children(); i++ ) {
			Fl_Term *pt = (Fl_Term *)pTabs->child(i);
			if ( pt->live() ) active = true;
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
Fl_Menu_Item menubar[] = {
{"Term", 		0,	0,		0, 	FL_SUBMENU},
{"&Connect...", FL_ALT+'c', menu_cb},
{"&Disconnect", FL_ALT+'d', menu_cb},
{"Logging...",	0,			menu_cb, 	0, 	FL_MENU_TOGGLE},
{"Local Echo",	0,			menu_cb,	0,	FL_MENU_TOGGLE|FL_MENU_DIVIDER},
{"ftpd ", 		0, 	daemon_host_cb},
{"tftpd ", 		0,	daemon_host_cb},		
{0},
{"Script", 		0,	0,		0, 	FL_SUBMENU},
{"Run...",		FL_ALT+'r',	menu_cb},
{"Pause",		FL_ALT+'p',	menu_cb},
{"Stop",		FL_ALT+'q',	menu_cb,		0,	FL_MENU_DIVIDER},
{0},
{"Options", 	0,	0,		0, 	FL_SUBMENU},
{"Local Edit",	FL_ALT+'e',	editor_cb,	0,	FL_MENU_TOGGLE},
{"Font Face",	0, 	0,		0,	FL_SUBMENU},
#ifdef __APPLE__
{"Menlo",	 	0, 	menu_cb,0, 	FL_MENU_RADIO|FL_MENU_VALUE},
{"Monaco", 		0, 	menu_cb,0, 	FL_MENU_RADIO},
{"Courier New", 0, 	menu_cb,0, 	FL_MENU_RADIO},
#else
{"Consolas", 	0, 	menu_cb,0, 	FL_MENU_RADIO|FL_MENU_VALUE},
{"Courier New", 0, 	menu_cb,0, 	FL_MENU_RADIO},
{"Lucida Console",0,menu_cb,0, 	FL_MENU_RADIO},
#endif
{0},
{"Font Size", 	0,	0,		0,	FL_SUBMENU},
{"12",			0,	menu_cb,0,	FL_MENU_RADIO},
{"14",			0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"16",			0,	menu_cb,0,	FL_MENU_RADIO},
{"18",			0,	menu_cb,0,	FL_MENU_RADIO},
{"20",			0,	menu_cb,0,	FL_MENU_RADIO},
{0},
{"Buffer Size", 0,	0,		0,	FL_SUBMENU},
{"2048",		0,	menu_cb,0,	FL_MENU_RADIO},
{"4096",		0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"8192",		0,	menu_cb,0,	FL_MENU_RADIO},
{"16384",		0,	menu_cb,0,	FL_MENU_RADIO},
{"32768",		0,	menu_cb,0,	FL_MENU_RADIO},
{0},
{"Keep Alive",	0,	0,		0,	FL_SUBMENU},
{"none", 		0, 	menu_cb,0,  FL_MENU_RADIO|FL_MENU_VALUE},
{"1 minute",	0,	menu_cb,0,	FL_MENU_RADIO},
{"5 minutes",	0,	menu_cb,0,	FL_MENU_RADIO},
{"15 minutes",	0,	menu_cb,0,	FL_MENU_RADIO},
{0},
{"About tinyTerm2", 0, about_cb},
{0},
{0}
};
void load_script()			// set working directory and load scripts to menu
{
#ifdef WIN32
 	if ( GetFileAttributes("tinyTerm.hist")==INVALID_FILE_ATTRIBUTES ) 
 	{						// if current directory doesn't have .hist
		_chdir(getenv("USERPROFILE"));
		_mkdir("Documents\\tinyTerm");
		_chdir("Documents\\tinyTerm");
	}

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = FindFirstFile("script\\*.*", &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE) 
	{
		do {
			if ( FindFileData.cFileName[0]!='.' )
				pMenuBar->insert(12, FindFileData.cFileName, 0, script_cb);
		} while( FindNextFile(hFind, &FindFileData) );
	}
	else {
		_mkdir("script");
		FILE *fp = fopen("script\\scp_to_folder.js", "w+");
		if ( fp!=NULL ) {
			fprintf(fp, "%s", SCP_TO_FOLDER); 
			fclose(fp);
			pMenuBar->insert(12, "scp_to_folder.js", 0, script_cb);
		}
	}
#else
	char *homedir = getenv("HOME");
	if ( homedir==NULL ) homedir = getpwuid(getuid())->pw_dir;
	chdir(homedir);
	mkdir(".tinyTerm", 0755);
	chdir(".tinyTerm");
	DIR *dir;
	struct dirent *dp;
	if ( (dir=opendir("script") ) != NULL ) {
		while ( (dp=readdir(dir)) != NULL ) {
			if ( dp->d_name[0]!='.' )
				pMenuBar->insert(12, dp->d_name, 0, script_cb);
		}
		closedir(dir);
	}
	else {
		mkdir("script", 0755);
		FILE *fp = fopen("script/scp_to_home.sh", "w+");
		if ( fp!=NULL ) {
			fprintf(fp, "%s", SCP_TO_HOME); 
			fclose(fp);
			chmod("script/scp_to_home.sh", 0755);
			pMenuBar->insert(12, "scp_to_home.sh", 0, script_cb);
		}
	}		
#endif
}
void load_dict()
{
	FILE *fp = fopen("tinyTerm.hist", "r");
	if ( fp!=NULL ) {
		char line[256];
		while ( fgets(line, 256, fp)!=NULL ) {
			line[strcspn(line, "\n")] = 0; 
			pCmd->add(line);
			if ( *line=='!' ) {
				if ( strncmp(line+1, "ssh", 3)==0 ||
					strncmp(line+1, "telnet",6)==0 ||
					strncmp(line+1, "serial",6)==0 ||
					strncmp(line+1, "sftp",4)==0 || 
					strncmp(line+1, "netconf",7)==0 )
						pMenuBar->insert(7, line+1, 0, menu_host_cb);
			}
			if ( *line=='~' ) {
				char name[256] = "Options/";
				if ( strncmp(line+1, "FontFace", 8)==0 ) {
					Fl::set_font(FL_COURIER, line+10);
					pTerm->textsize();
					strcpy(name+8, "Font Face/");
					strcat(name, line+10);
				}
				else if ( strncmp(line+1, "FontSize", 8)==0 ) {
					textsize = atoi(line+10);
					pTerm->textsize(textsize);
					pCmd->textsize(textsize);
					strcpy(name+8, "Font Size/");
					strcat(name, line+10);
				}
				else if ( strncmp(line+1, "TermSize", 8)==0 ) {
					int w, h;
					sscanf(line+10, "%dx%d", &w, &h);
					fl_font(FL_COURIER, textsize);
					pWindow->size(w*fl_width('a'), h*textsize);
				}
				else if ( strncmp(line+1, "BuffSize", 8)==0 ) {
					buffsize = atoi(line+10);
					pTerm->buffsize(buffsize);
					strcpy(name+8, "Buffer Size/");
					strcat(name, line+10);
				}
				else if ( strcmp(line+1, "LocalEdit")==0 ) {
					editor_cb(NULL, NULL);
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
	pMenuLogg = (Fl_Menu_Item *)pMenuBar->find_item("Term/Logging...");
	pMenuEcho = (Fl_Menu_Item *)pMenuBar->find_item("Term/Local Echo");
}
void save_dict()
{
	FILE *fp = fopen("tinyTerm.hist", "w");
	if ( fp!=NULL ) {
		if ( pCmd->w()>10 )
			fprintf(fp, "~LocalEdit\n");
		if ( textsize!=14 ) 
			fprintf(fp, "~FontSize %d\n", textsize);
		if ( buffsize!=4096 ) 
			fprintf(fp, "~BuffSize %d\n", buffsize);
		if ( pWindow->w()!=800 || pWindow->h()!=640 ) {
			fl_font(FL_COURIER, textsize);
			fprintf(fp, "~TermSize %dx%d\n", 
						int(pWindow->w()/fl_width('a')), 
						pWindow->h()/textsize );
		}
			
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
	Fl::set_font(FL_COURIER, "Menlo");
#else
	Fl::set_font(FL_COURIER, "Consolas");
#endif
	
	pWindow = new Fl_Double_Window(800, 640, "tinyTerm2");
	{
		pMenuBar=new Fl_Sys_Menu_Bar(0, 0, pWindow->w(), MENUHEIGHT);
		pMenuBar->menu(menubar);
		pMenuBar->textsize(18);
		pTerm = new Fl_Term(0, MENUHEIGHT, pWindow->w(), 
								pWindow->h()-MENUHEIGHT, "term");
		pTerm->buffsize(buffsize);
		pTerm->textsize(textsize);
		
		pCmd = new Fl_Browser_Input( 0, pWindow->h()-1, 1, 1, "");
		pCmd->box(FL_FLAT_BOX);
	  	pCmd->color(fl_rgb_color(32,32,32));
		pCmd->cursor_color(FL_WHITE);
		pCmd->textcolor(FL_YELLOW);
		pCmd->textsize(textsize);
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
	load_script();
	load_dict();
	pWindow->show();
	while ( Fl::wait() ) {
		Fl_Widget *pt = (Fl_Widget *)Fl::thread_message();
		if ( pt!=NULL )pt->redraw();
	}
	save_dict();
	libssh2_exit();
	httpd_exit();
	return 0;
}
/**********************************HTTPd**************************************/
const char HEADER[]="HTTP/1.1 200 OK\
					\nServer: tinyTerm2-httpd\
					\nAccess-Control-Allow-Origin: *\
					\nContent-Type: text/plain\
					\nContent-length: %d\
					\nCache-Control: no-cache\n\n";
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
	char reply[32768], timebuf[128];
	int len, i, j;
	time_t now;

	now = time( NULL );
	strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );

    struct stat sb;
	if ( stat( file, &sb ) ==-1 ) {
		len=sprintf(reply, "HTTP/1.1 404 not found\nDate: %s\n", timebuf);
		len+=sprintf(reply+len, "Server: tinyTerm2\nContent-Type: text/html\n");
	    len+=sprintf(reply+len, "Content-Length: 14\n\nfile not found");
		send(s1, reply, len, 0);
		return;
	}

	FILE *fp = fopen( file, "rb" );	
	if ( fp!=NULL ) {
		len=sprintf(reply, "HTTP/1.1 200 Ok\nDate: %s\n", timebuf);
		const char *filext=strrchr(file, '.');
		if ( filext!=NULL ) {
			for ( i=0, j=0; j<8; j++ ) 
				if ( strcmp(filext, exts[j])==0 ) i=j;
		}
		len+=sprintf(reply+len,"Server: tinyTerm2\nContent-Type: %s\n",mime[i]);

		long filesize = sb.st_size;
		len+=sprintf(reply+len, "Content-Length: %ld\n", filesize );
		strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &sb.st_mtime ));
		len+=sprintf(reply+len, "Last-Modified: %s\n\n", timebuf );

		send(s1, reply, len, 0);
		while ( (len=fread(reply, 1, 32768, fp))>0 )
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
				for ( char *p=cmd; *p!=0; p++ )
					if ( *p=='+' ) *p=' ';
				fl_decode_uri(cmd);

				if ( *cmd=='?' ) {
					if ( strncmp(++cmd, "!Tab ",5)==0 ) {
						replen = term_act(cmd+5);
						if  ( replen ) reply = cmd;
					}
					else 
						replen = pTerm->cmd( cmd, &reply );
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
			sprintf(http_port, "%d", port);
			return;
		}
	}
	closesocket(http_s0);
}
void httpd_exit()
{
	closesocket(http_s0);
}
