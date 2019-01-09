//
// "$Id: flTerm.cxx 24849 2018-11-11 21:05:10 $"
//
// flTerm -- FLTK based terminal emulator
//
//    example application using the Fl_Term widget.
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

const char ABOUT_TERM[]="\r\n\r\n\
    flTerm is a terminal emulator designed for network engineers,\n\n\
    with focus on simplicity and scriptibility for CLI users:\r\n\r\n\r\n\
        * serial/telnet/ssh/sftp/netconf client\r\n\r\n\
        * single executable smaller than 1MB\r\n\r\n\
        * Windows, macOS and Linux compatible\r\n\r\n\
        * simple automation of command batches\r\n\r\n\
        * scriptable through \033[34mxmlhttp://127.0.0.1:%d\033[37m\r\n\r\n\r\n\
    by yongchaofan@gmail.com		11-11-2018\r\n\r\n\
    https://github.com/zoudaokou/flTerm\r\n\r\n\r\n";

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include <thread>
#include "Hosts.h"
#include "ssh2.h"
#include "Fl_Term.h"
#include "Fl_Browser_Input.h"

#define CMDHEIGHT	20
#define TABHEIGHT 	24
#ifdef __APPLE__
  #define MENUHEIGHT 0
#else
  #define MENUHEIGHT 24
#endif
#include <FL/x.H>               // needed for fl_display
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>

int httpd_init();
void httpd_exit();
int term_scp(Fl_Term *term, char *cmd, char **preply);
int term_tun(Fl_Term *term, char *cmd, char **preply);
void term_drop(Fl_Term *term, const char *buf);

void tab_cb(Fl_Widget *w);
void editor_cb(Fl_Widget *w, void *data);
void menu_cb(Fl_Widget *w, void *data);
void menu_host_cb(Fl_Widget *w, void *data);

Fl_Term *acTerm;
Fl_Window *pTermWin;
Fl_Sys_Menu_Bar *pMenuBar;
Fl_Browser_Input *pCmd;
Fl_Tabs *pTermTabs = NULL;
int fontsize = 14;
int buffsize = 8192;
Fl_Window *pDialog;
Fl_Choice *pProtocol;
Fl_Input_Choice *pPort;
Fl_Input_Choice *pHostname, *pSettings;
Fl_Button *pConnect;
Fl_Button *pCancel;

#define CHOOSE_FILE 		Fl_Native_File_Chooser::BROWSE_FILE
#define CHOOSE_SAVE_FILE 	Fl_Native_File_Chooser::BROWSE_SAVE_FILE
#define CHOOSE_SAVE_DIR 	Fl_Native_File_Chooser::BROWSE_SAVE_DIRECTORY
static Fl_Native_File_Chooser fnfc;
const char *file_chooser(const char *title, const char *filter, int type,
						 const char *fn="")
{
	static char files[4096];
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

int http_port;
void about_cb(Fl_Widget *w, void *data)
{
	char buf[4096];
	sprintf(buf, ABOUT_TERM, http_port);
	acTerm->disp(buf);
}
const char *kb_gets(const char *prompt, int echo)
{
	const char *p = NULL;
	if ( acTerm->live() ) {
		sshHost *host = (sshHost *)acTerm->user_data();
		p = host->ssh_gets(prompt, echo);
	}
	return p;
}
void term_cb(void *data, const char *buf, int len)
{
	Fan_Host *host = (Fan_Host *)data;
	if ( len>0 ) 
		host->write(buf, len);
	else {
		Fl_Term *term = (Fl_Term *)host->host_data();
		if ( len==0 ) 
			host->send_size(term->size_x(), term->size_y());
		else //len<0
			term_drop(term, buf);
	}
}
void host_cb(void *data, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)data;
	if ( len>0 )
		term->puts(buf, len);
	else {
		if ( len==0 ) {
			term->live(true);
			Fan_Host *host = (Fan_Host *)term->user_data();
			host->send_size(term->size_x(), term->size_y());
		}
		else {	//len<0
			term->live(false);
			term->disp("\r\n\033[31m");
			term->disp(buf);
			term->disp(*buf!='D'?" failure\033[37m\r\n\r\n":
				 ", press Enter to restart\033[37m\r\n\r\n" );
		}
	}
}
void conf_host_cb(void *data, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)data;
	if ( len>0 )
		term->putxml(buf, len);
	else {
		if ( len==0 )
			term->live(true);
		else {	//len<0
			term->live(false);
			term->disp(buf);
			term->disp(*buf=='D'?" press Enter to restart\r\n":" failure\r\n");
		}
	}
}
void tab_init()
{
	pTermTabs = new Fl_Tabs(0, MENUHEIGHT, pTermWin->w(), 
					pTermWin->h()-MENUHEIGHT-(Fl::focus()==pCmd?CMDHEIGHT:0));
	pTermTabs->when(FL_WHEN_RELEASE|FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED);
	pTermTabs->box(FL_FLAT_BOX);
	pTermTabs->callback(tab_cb);
	pTermTabs->selection_color(FL_CYAN);
	pTermTabs->end();
	pTermWin->remove(acTerm);
	pTermWin->insert(*pTermTabs, pTermWin->children()-1);
	pTermWin->resizable(pTermTabs);
	acTerm->resize(0, MENUHEIGHT+TABHEIGHT, pTermTabs->w(), 
								pTermTabs->h()-TABHEIGHT);
	acTerm->labelsize(18);
	pTermTabs->insert(*acTerm, pTermTabs->children());
	pTermTabs->resizable(acTerm);
	pTermTabs->redraw();
	pTermWin->label(acTerm->title());
	pTermWin->redraw();
}
void tab_act(Fl_Term *pTerm)
{
	char label[32];
	if ( acTerm!=NULL ) {		//remove "  x" from previous active tab
		strncpy(label, acTerm->label(), 31);
		char *p = strchr(label, ' ');
		if ( p!=NULL ) *p=0;
		acTerm->copy_label(label);
	}

	pTermTabs->value(pTerm);
	acTerm = pTerm;
	acTerm->take_focus();		//add "  x" to current active tab
	strncpy(label, acTerm->label(), 24);
	strcat(label, "  x");
	acTerm->copy_label(label);
	pTermTabs->redraw();

	pTermWin->label(acTerm->title());
	pTermWin->redraw();
}
void tab_new()
{
	if ( pTermTabs==NULL ) tab_init();
	Fl_Term *pTerm = new Fl_Term(0, pTermTabs->y()+TABHEIGHT, 
						pTermTabs->w(), pTermTabs->h()-TABHEIGHT, "term");
	pTerm->labelsize(18);
	pTerm->textsize(fontsize);
	pTerm->buffsize(buffsize);
	pTermTabs->insert(*pTerm, pTermTabs->children());
	tab_act(pTerm);
}
void tab_del()
{
	if ( acTerm->live() ) {
		Fan_Host *host = (Fan_Host *)acTerm->user_data();
		host->disconn();
		delete host;
		acTerm->callback(NULL, NULL);
	}
	if ( pTermTabs->children()>1 ) {
		pTermTabs->remove(acTerm);
	//	Fl::delete_widget(acTerm); 
		acTerm = NULL;
		tab_act((Fl_Term *)pTermTabs->child(0));
	}
	else {
		acTerm->clear();
		acTerm->label("term");
	}
	pTermTabs->redraw();
}
void tab_cb(Fl_Widget *w) 
{
	Fl_Term *pTerm = (Fl_Term *)pTermTabs->value();

	if ( pTerm==acTerm ) { 	//clicking on active tab, delete it
		int confirm = 0;
		if ( acTerm->live() ) confirm = 
			fl_choice("Disconnect from %s?", "Yes", "No", 0, acTerm->label());
		if ( confirm==0 ) tab_del();
	}
	else
		tab_act(pTerm);	//clicking on inactive tab, activate it
}

void term_act(const char *host)
{
	for ( int i=0; i<pTermTabs->children(); i++ )
		if ( strncmp(host, pTermTabs->child(i)->label(), strlen(host))==0 ) {
			tab_act((Fl_Term *)pTermTabs->child(i));
			break;
		}
}
#ifdef WIN32
const char *ports[]={"COM1","23", "22", "22", "830"};
#else
const char *ports[]={"/dev/tty.usbserial","23", "22", "22", "830"};
#endif
void term_connect(const char *host)
{
	Fan_Host *pHost = NULL;
	if ( strncmp(host, "disconn", 7)==0 ) {
		pHost = (Fan_Host *)acTerm->user_data();
		if ( pHost!=NULL ) pHost->disconn();
		return;
	}
	else if ( strncmp(host, "serial ", 7)==0 ) {
		pHost = new comHost(host+7);
	}
	else if ( strncmp(host, "telnet ", 7)==0 ) {
		pHost = new tcpHost(host+7);
	}
	else if ( strncmp(host, "ssh " , 4)==0 ) {
		pHost = new sshHost(host+4);
	}
	else if ( strncmp(host, "sftp ", 5)==0 ) {
		pHost = new sftpHost(host+5);
	}
	else if ( strncmp(host, "netconf ", 8)==0 ) {
		pHost = new confHost(host+8);
	}
	if ( pHost!=NULL ) {
		if ( acTerm->live() ) tab_new();
		acTerm->callback(term_cb, pHost);
		char label[32];
		strncpy(label, pHost->name(), 28);
		label[28]=0;
		strcat(label, "  x");
		acTerm->copy_label(label);
		acTerm->live(true);
		
		pHost->callback(host_cb, acTerm);
		pHost->connect();
	}
}
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
	char buf[256];
	int proto = pProtocol->value();
	buf[0] = '!';
	strcpy(buf+1, pProtocol->mvalue()->label());
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
	pCmd->add(buf);
	term_connect(buf+1); 
}
void cancel_cb(Fl_Widget *w)
{
	w->parent()->hide();
}
void conn_dialog()
{
	pDialog->resize(pTermWin->x()+100, pTermWin->y()+150, 360, 200);
	pDialog->show();
	pHostname->take_focus();
}

int term_cmd(const char *cmd, char** preply)
{
	
	int rc=0;
	if ( *cmd=='!' ) {
		pCmd->value(cmd++);
		if ( strncmp(cmd,"Tab ", 4)==0 ) term_act(cmd+4);
		else if ( strncmp(cmd,"scp",3)==0 ) 
			rc = term_scp(acTerm,strdup(cmd+4),preply);
		else if ( strncmp(cmd,"tun",3)==0 ) 
			rc = term_tun(acTerm,strdup(cmd+3),preply);
		else {
			rc = acTerm->cmd(cmd-1, preply);
			if ( rc==-1 ) term_connect(cmd);
		}
	}
	else 
		rc = acTerm->cmd(cmd, preply);
	return rc;
}
void scper(char *cmd)	//used by cmd_cb for #scp thread
{
	term_scp(acTerm, cmd, NULL);
}
void tuner(char *cmd)	//used by cmd_cb for #tun thread
{
	term_tun(acTerm, cmd, NULL);
}
void cmd_cb(Fl_Widget *o) 
{
	const char *cmd = pCmd->value();
	pCmd->add( cmd );
	pCmd->position(0, strlen(cmd));
	switch( *cmd ) {
		case '/':  acTerm->srch(cmd+1); break;
		case '!':	if ( strncmp(cmd, "!scp ", 5)==0 ) {
						std::thread scp_thread(scper,strdup(cmd+5));
						scp_thread.detach();
					}
					else if ( strncmp(cmd, "!tun", 4)==0 ) {
						std::thread tun_thread(tuner,strdup(cmd+4));
						tun_thread.detach();
					}
					else
						term_cmd(cmd, NULL);
					break;
		default: 	if ( acTerm->live() ) {
						acTerm->send(cmd);
						acTerm->send("\r");
					}
	}
}
void editor_cb(Fl_Widget *w, void *data)
{
	if ( pCmd->w()==1 ) {
		pTermWin->resize( pTermWin->x(), pTermWin->y(), 
							pTermWin->w(), pTermWin->h()+CMDHEIGHT);
		pCmd->resize(0, pTermWin->h()-CMDHEIGHT, pTermWin->w(), CMDHEIGHT);
		if ( pTermTabs!=NULL ) 
			pTermTabs->resize( 0, MENUHEIGHT, pTermWin->w(), 
							pTermWin->h()-MENUHEIGHT-CMDHEIGHT);
		else
			acTerm->resize( 0, MENUHEIGHT, pTermWin->w(),
							pTermWin->h()-MENUHEIGHT-CMDHEIGHT);
		pCmd->take_focus();
		pTermWin->redraw();
	}
	else {
		pTermWin->resize( pTermWin->x(), pTermWin->y(), 
							pTermWin->w(), pTermWin->h()-CMDHEIGHT);
		pCmd->resize(0, pTermWin->h(), 1, 1);
		if ( pTermTabs!=NULL ) 
			pTermTabs->resize( 0, MENUHEIGHT, pTermWin->w(), 
							pTermWin->h()-MENUHEIGHT);
		else
			acTerm->resize( 0, MENUHEIGHT, pTermWin->w(),
							pTermWin->h()-MENUHEIGHT);
		acTerm->take_focus();
		pTermWin->redraw();
	}
}
void menu_cb(Fl_Widget *w, void *data)
{
	const char *menutext = pMenuBar->text();
	if ( strcmp(menutext, "&Connect...")==0 ) {
		conn_dialog();
	}
	else if ( strcmp(menutext, "&Disconnect")==0 ) {
		Fan_Host *host = (Fan_Host *)acTerm->user_data();
		if ( host!=NULL ) host->disconn();
	}
	else if ( strcmp(menutext, "Logging...")==0 ) {
		if ( acTerm->logg() ) 
			acTerm->logg( NULL );
		else {
			const char *fname = file_chooser("logfile:", "Log\t*.log", 
														CHOOSE_SAVE_FILE);	
			if ( fname!=NULL ) acTerm->logg(fname);
		}
	}
	else if ( strcmp(menutext, "local Echo")==0 ) {
		if ( !acTerm->live() ) return;
		acTerm->echo(!acTerm->echo());
	}
	else if ( strcmp(menutext, "Run...")==0 ) {
		const char *fname = file_chooser("script file:","Text\t*.txt", 
														CHOOSE_FILE);
		if ( fname!=NULL ) {
			struct stat sb;
			if ( stat(fname, &sb)!=-1 ) {
				char *script = (char *)malloc(sb.st_size+1);
				if ( script!=NULL ) {
					FILE *fp = fopen(fname, "r");
					if ( fp!=NULL ) {
						fread(script, 1, sb.st_size, fp);
						fclose(fp);
						script[sb.st_size]=0;
						acTerm->run_script(script);
					}
				}
			}
		}
	}
	else if ( strcmp(menutext, "Pause")==0 ) {
		acTerm->pause_script();
	}
	else if ( strcmp(menutext, "Stop")==0 ) {
		acTerm->stop_script();
	}
	else if ( strcmp(menutext, "Courier New")==0 ||
			  strcmp(menutext, "Monaco")==0 ||
			  strcmp(menutext, "Menlo")==0 ||
			  strcmp(menutext, "Consolas")==0 ||
			  strcmp(menutext, "Lucida Console")==0 ) {
		Fl::set_font(FL_COURIER, menutext);
		acTerm->textsize();
	}
	else if ( strcmp(menutext, "12")==0 ||
			  strcmp(menutext, "14")==0 ||
			  strcmp(menutext, "16")==0 ||
			  strcmp(menutext, "18")==0 ||
			  strcmp(menutext, "20")==0 ){ 
		fontsize = atoi(menutext);
		acTerm->textsize(fontsize);
	}
	else if ( strcmp(menutext, "2048")==0 ||
			  strcmp(menutext, "4096")==0 ||
			  strcmp(menutext, "8192")==0 ||
			  strcmp(menutext, "16384")==0||
			  strcmp(menutext, "32768")==0 ){
		buffsize = atoi(menutext);
		acTerm->buffsize(buffsize);
	}
}
void menu_host_cb(Fl_Widget *w, void *data)
{
	term_connect((const char *)data);
}
void close_cb(Fl_Widget *w, void *data)
{
	if ( pTermTabs==NULL ) {	//not multi-tab
		if ( acTerm->live() ) {
			if ( fl_choice("Disconnect and exit?", "Yes", "No", 0)==1 ) return;
			Fan_Host *host = (Fan_Host *)acTerm->user_data();
			host->disconn();
			delete host;
		}
	}
	else {						//multi-tabbed
		int active = acTerm->live();
		if ( !active ) for ( int i=0; i<pTermTabs->children(); i++ ) {
			Fl_Term *pTerm = (Fl_Term *)pTermTabs->child(i);
			if ( pTerm->live() ) active = true;
		}
		if ( active ) {
			if ( fl_choice("Disconnect all and exit?", "Yes", "No", 0)==1 )
				return;
		}
		while ( pTermTabs->children()>1 ) tab_del();
	}
	pTermWin->hide();
}
Fl_Menu_Item menubar[] = {
{"Term", 		0,	0,		0, 	FL_SUBMENU},
{"&Connect...", FL_ALT+'c', menu_cb},
{"&Disconnect", FL_ALT+'d', menu_cb},
{"Logging...",	0,			menu_cb},
{"local Echo",	0,			menu_cb,	0,	FL_MENU_DIVIDER},
{0},
{"Script", 		0,	0,		0, 	FL_SUBMENU},
{"Run...",		FL_ALT+'r',	menu_cb},
{"Pause",		FL_ALT+'p',	menu_cb},
{"Stop",		FL_ALT+'q',	menu_cb,	0,	FL_MENU_DIVIDER},		
{0},
{"Options", 	0,	0,		0, 	FL_SUBMENU},
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
{"4096",		0,	menu_cb,0,	FL_MENU_RADIO},
{"8192",		0,	menu_cb,0,	FL_MENU_RADIO|FL_MENU_VALUE},
{"16384",		0,	menu_cb,0,	FL_MENU_RADIO},
{"32768",		0,	menu_cb,0,	FL_MENU_RADIO},
{0},
{"Editor",		FL_ALT+'e',	editor_cb,	0,	FL_MENU_TOGGLE},
#ifndef __APPLE__
{"About", 		0,			about_cb,	0,	FL_MENU_DIVIDER},
#endif
{0},
{0}
};
int main(int argc, char **argv)
{
	http_port = httpd_init();
	libssh2_init(0);

#ifdef __APPLE__ 
	Fl::set_font(FL_COURIER, "Menlo");
#else
	Fl::set_font(FL_COURIER, "Consolas");
#endif

	pTermWin = new Fl_Double_Window(800, 640, "Fl_Term");
	{
		pMenuBar=new Fl_Sys_Menu_Bar(0, 0, pTermWin->w(), MENUHEIGHT);
		pMenuBar->menu(menubar);
		pMenuBar->textsize(20);
		pMenuBar->box(FL_THIN_UP_BOX);
		acTerm = new Fl_Term(0, MENUHEIGHT, pTermWin->w(), 
								pTermWin->h()-MENUHEIGHT, "term");
		acTerm->textsize(fontsize);
	
		pCmd = new Fl_Browser_Input( 0, pTermWin->h()-1, 1, 1, "");
		pCmd->box(FL_FLAT_BOX);
	  	pCmd->color(FL_DARK_GREEN);
		pCmd->textcolor(FL_YELLOW);
		pCmd->textsize(20);
	  	pCmd->when(FL_WHEN_ENTER_KEY_ALWAYS);
	  	pCmd->callback(cmd_cb);
	}
	pTermWin->callback(close_cb);
	pTermWin->resizable(acTerm);
	pTermWin->end();

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
    pTermWin->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	pTermWin->show();

	int i;
	for ( i=1; i<argc; i++ ) {
		Fl_Menu_Item *menu;
		char itemname[256] = "Options/";
		if ( strcmp(argv[i], "--tabs")==0 ) {
			tab_init();
		}
		else if ( strcmp(argv[i], "--editor")==0 ) {
			editor_cb(NULL, NULL);
			strcpy(itemname+8, "Editor");
			menu = (Fl_Menu_Item *)pMenuBar->find_item(itemname);
			if ( menu!=NULL ) menu->set();
		}
		else if ( strcmp(argv[i], "--fontface")==0 ) {
			Fl::set_font(FL_COURIER, argv[++i]);
			strcpy(itemname+8, "Font Face/");
			strcat(itemname, argv[i]);
			menu = (Fl_Menu_Item *)pMenuBar->find_item(itemname);
			if ( menu!=NULL ) menu->setonly();
		}
		else if ( strcmp(argv[i], "--fontsize")==0 ) {
			fontsize = atoi(argv[++i]);
			acTerm->textsize(fontsize);
			strcpy(itemname+8, "Font Size/");
			strcat(itemname, argv[i]);
			menu = (Fl_Menu_Item *)pMenuBar->find_item(itemname);
			if ( menu!=NULL ) menu->setonly();
	}
		else if ( strcmp(argv[i], "--buffsize")==0 ) {
			buffsize = atoi(argv[++i]);
			acTerm->buffsize(buffsize);
			strcpy(itemname+8, "Buffer Size/");
			strcat(itemname, argv[i]);
			menu = (Fl_Menu_Item *)pMenuBar->find_item(itemname);
			if ( menu!=NULL ) menu->setonly();
		}
		else
			break;
	}
	if ( i<argc ) 
		term_connect(argv[i]);
	else 
		conn_dialog();
			
	FILE *fp;
	if ( (fp=fopen("flTerm.hist", "r"))!=NULL ) {
		char line[256];
		while ( fgets(line, 256, fp)!=NULL ) {
			line[strcspn(line, "\r\n")] = 0; 
			pCmd->add(line);
			if ( *line=='!' ) 
				pMenuBar->insert(5,line+1,0,menu_host_cb,(void *)strdup(line+1));
		}
		fclose(fp);
	}

	while ( Fl::wait() ) {
		Fl_Widget *pt = (Fl_Widget *)Fl::thread_message();
		if ( pt!=NULL )pt->redraw();
	}

	if ( (fp=fopen("flTerm.hist", "w"))!=NULL ) {
		const char *p = pCmd->first();
		while ( p!=NULL ) { 
			fprintf(fp, "%s\n", p);
			p = pCmd->next();
		}
		fclose(fp);
	}
	libssh2_exit();
	httpd_exit();
	return 0;
}
/**********************************HTTPd**************************************/
const char HEADER[]="HTTP/1.1 %s\
					\nServer: flTerm-httpd\
					\nAccess-Control-Allow-Origin: *\
					\nContent-Type: text/plain\
					\nContent-length: %d\
					\nCache-Control: no-cache\n\n";
void httpd( int s0 )
{
	struct sockaddr_in cltaddr;
	socklen_t addrsize=sizeof(cltaddr);
	char buf[4096], *cmd, *reply;
	int cmdlen, replen, http_s1;

	while ( (http_s1=accept(s0,(struct sockaddr*)&cltaddr,&addrsize ))!=-1 ) {
		while ( (cmdlen=recv(http_s1,buf,4095,0))>0 ) {
			buf[cmdlen] = 0;
			if ( strncmp(buf, "GET /?",6)==0 ) {
				cmd = buf+6;
				char *p = strchr(cmd, ' ');
				if ( p!=NULL ) *p = 0;
				for ( char *p=cmd; *p!=0; p++ )
					if ( *p=='+' ) *p=' ';
				fl_decode_uri(cmd);
				replen = term_cmd( cmd, &reply );
				int len = sprintf( buf, HEADER, "200 OK", replen );
				send( http_s1, buf, len, 0 );
				if ( replen>0 ) send( http_s1, reply, replen, 0 );
			}
		}
		closesocket(http_s1);
	}
}
static int http_s0 = -1;
int httpd_init()
{
#ifdef WIN32
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2,0), &wsadata);
#endif
	http_s0 = socket(AF_INET, SOCK_STREAM, 0);
	if ( http_s0 == -1 ) return -1;

	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=inet_addr("127.0.0.1");
	short port = 8079;
	int rc = -1;
	while ( rc==-1 && ++port<8100 ) {
		svraddr.sin_port=htons(port);
		rc = bind(http_s0, (struct sockaddr*)&svraddr, addrsize);
	}
	if ( rc!=-1 ) {
		if ( listen(http_s0, 1)!=-1){
			std::thread httpThread( httpd, http_s0 );
			httpThread.detach();
			return port;
		}
	}
	closesocket(http_s0);
	return -1;
}
void httpd_exit()
{
	closesocket(http_s0);
}
void term_pwd(Fl_Term *term, char *dst)
{
	char *p1, *p2;
	term->cmd("pwd", &p2);
	p1 = strchr(p2, 0x0a);
	if ( p1!=NULL ) {
		p2 = p1+1;
		p1 = strchr(p2, 0x0a);
		if ( p1!=NULL ) {
			strncpy(dst, p2, p1-p2);
			dst[p1-p2]=0;
		}
	}
}
int term_scp(Fl_Term *term, char *cmd, char **preply)
{
	Fan_Host *host= (Fan_Host *)term->user_data();
	if ( host==NULL ) {
		term->disp("not connected yet\n");
		return 0;
	}
	if ( host->type()!=HOST_SSH ) {
		term->disp("not ssh connection\n");
		return 0;
	}
	char *reply = term->mark_prompt();
	char *p = strchr(cmd, ' ');
	if ( p!=NULL ) {
		*p++ = 0;
		char *local, *remote, *rpath, rlist[1024];
		if ( *cmd==':' ) {			//scp_read
			local = p; remote = cmd+1;
			strcpy(rlist, "ls -1  ");
		}
		else {						//scp_write
			local = cmd; remote = p+1;
			strcpy(rlist, "ls -ld ");
		}
		if ( *remote=='/') 			//get remote dir
			strcpy(rlist+7, remote);
		else {
			term_pwd(term, rlist+7);
			if ( *remote ) {
				strcat(rlist, "/");
				strcat(rlist, remote);
			}
		}
		if ( term->cmd(rlist, &rpath)>0 ) {
			reply = term->mark_prompt();
			char *p = strchr(rpath, 0x0a);
			if ( p!=NULL ) {
				if ( *cmd==':' ) {	//scp_read
					remote = strdup(p+1);
					((sshHost *)host)->scp_read(local, remote);
				}
				else {				//scp_write
					if ( p[1]=='d' ) strcat(rlist, "/");
					remote = strdup(rlist+7);
					((sshHost *)host)->scp_write(local, remote);
				}
				free(remote);
			}
		}	
	}
	free(cmd);
	host->write("\r", 1);
	if ( preply!=NULL ) *preply = reply;
	return term->waitfor_prompt();
}
int term_tun(Fl_Term *term, char *cmd, char **preply)
{
	Fan_Host *host = (Fan_Host *)term->user_data();
	if ( host==NULL ) {
		term->disp("not connected yet\n");
		return 0;
	}
	if ( host->type()!=HOST_SSH ) {
		term->disp("not ssh connection\n");
		return 0;
	}

	if ( preply!=NULL ) *preply = term->mark_prompt();
	((sshHost *)host)->tun(cmd);
	free(cmd);
	return term->waitfor_prompt();
}
void file_copier(Fl_Term *term, char *files)
{
	Fan_Host *host = (Fan_Host *)term->user_data();
	if ( host==NULL ) return;
	if ( host->type()!=HOST_SSH && host->type()!=HOST_SFTP ) return;
	
	char rdir[1024];
	term_pwd(term, rdir);
	strcat(rdir, "/");

	char *p=files, *p1;
	do {
		p1 = strchr(p, 0x0a);
		if ( p1!=NULL ) *p1++ = 0;
		if ( host->type()==HOST_SSH )
			((sshHost *)host)->scp_write(p, rdir);
		else if ( host->type()==HOST_SFTP )
			((sftpHost *)host)->sftp_put(p, rdir);
	}
	while ( (p=p1)!=NULL ); 
	host->write("\r",1);
	free(files);
}
void term_drop(Fl_Term *term, const char *buf)
{
	if ( !term->live() ) {
		term->puts(buf, strlen(buf));
		return;
	}
	Fan_Host *host = (Fan_Host *)term->user_data();
	if ( host->type()==HOST_CONF ) {
		host->write(buf, strlen(buf));
		return;
	}

	char *script = strdup(buf);	//script thread must delete this
	char *p0 = script;
	char *p1=strchr(p0, 0x0a);
	if ( p1!=NULL ) *p1=0;
	struct stat sb;				//is this a list of files?
	int rc = stat(p0, &sb);
	if ( p1!=NULL ) *p1=0x0a;

	if ( rc!=-1 ) {
		if ( host->type()==HOST_SSH || host->type()==HOST_SFTP ) {
			std::thread scripterThread(file_copier, term, script);
			scripterThread.detach();
		}
	}
	else
		term->run_script(script);
}