//
// "$Id: flTerm.cxx 25034 2018-08-18 21:05:10 $"
//
// flTerm -- A minimal ssh/scp/sftp terminal
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

const char ABOUT_TERM[]="\n\n\
    flTerm is a terminal simulator for network engineers,\n\n\
    a simple telnet/ssh/sftp/netconf client that features:\n\n\n\
        * minimalist user interface\n\n\
        * unlimited scroll back buffer\n\n\
        * Windows, macOS and Linux compatible\n\n\
        * Select to copy, right click to paste\n\n\
        * Drag and Drop to run list of commands\n\n\
        * Scripting interface at \033[34mxmlhttp://127.0.0.1:%d\033[37m\n\n\n\
    by yongchaofan@gmail.com		08-18-2018\n\n\
    https://github.com/zoudaokou/flTerm\n";

#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

#include <thread>
#include "Hosts.h"
#include "serial.h"
#include "ftpd.h"
#include "acInput.h"
#include "Fl_Term.h"

#define TABHEIGHT 20
#ifdef __APPLE__
	#define MENUHEIGHT 0
#else
	#define MENUHEIGHT 20
#endif
#include <FL/x.H>               // needed for fl_display
#include <FL/Fl.H>
#include <FL/Fl_Ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Native_File_Chooser.H>
int httpd_init();
void httpd_exit();
void scp(char *cmd);
void tun(char *cmd);
void term_dnd(Fl_Term *term, Fan_Host *host, const char *buf);

static Fl_Native_File_Chooser fnfc;
const char *file_chooser(const char *title, const char *filter)
{
	fnfc.title(title);
	fnfc.filter(filter);
	fnfc.directory(".");           		// default directory to use
	fnfc.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
	switch ( fnfc.show() ) {			// Show native chooser
		case -1:  			 			// ERROR
		case  1: return NULL;  			// CANCEL
		default: return fnfc.filename();// FILE CHOSEN
	}
}

void menu_update();
Fl_Sys_Menu_Bar *pMenu;
Fl_Window *pTermWin;
Fl_Tabs *pTermTabs;
Fl_Term *pAbout;
Fl_Term *acTerm;

Fl_Window *pCmdWin;
acInput *pCmd;

Fl_Window *pDialog;
Fl_Choice *pProtocol;
Fl_Input_Choice *pPort;
Fl_Input_Choice *pHostname, *pSettings;
Fl_Button *pConnect;
Fl_Button *pCancel;

const char *kb_gets(const char *prompt, int echo)
{
	const char *p = NULL;
	if ( acTerm->live() ) {
		sshHost *host = (sshHost *)acTerm->term_data();
		p = host->ssh_gets(prompt, echo);
	}
	return p;
}
void term_cb(void *data, const char *buf, int len)
{
	Fan_Host *host = (Fan_Host *)data;
	assert (host!=NULL);
	if ( len>0 ) 
		host->write(buf, len);
	else {
		Fl_Term *term = (Fl_Term *)host->host_data();
		assert(term!=NULL);
		if ( len==0 ) 
			host->send_size(term->size_x(), term->size_y());
		else //len<0
			if ( term->live() ) 
				term_dnd(term, host, buf);
			else {
				term->puts(buf);
			}
	}
}
void host_cb(void *data, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)data;
	assert(term!=NULL);
	if ( len>0 )
		term->puts(buf, len);
	else {
		if ( len==0 ) {
			term->live(true);
			Fan_Host *host = (Fan_Host *)term->term_data();
			host->send_size(term->size_x(), term->size_y());
		}
		else {	//len<0
			term->live(false);
			term->puts("\n\033[31m");
			term->puts(buf);
			term->puts(*buf!='D'?" failure\033[37m\n\n":
				 ", press Enter to restart\033[37m\n\n" );
		}
		menu_update();
	}
}
void conf_host_cb(void *data, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)data;
	assert(term!=NULL);
	if ( len>0 )
		term->putxml(buf, len);
	else {
		if ( len==0 )
			term->live(true);
		else {	//len<0
			term->live(false);
			term->puts(buf);
			term->puts(*buf=='D'?" press Enter to restart\n":" failure\n");
		}
		menu_update();
	}
}
void term_act(Fl_Term *pTerm)
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
	if ( acTerm!=pAbout ) {		//add "  x" to current active tab
		acTerm->take_focus();
		strncpy(label, acTerm->label(), 24);
		strcat(label, "  x");
		acTerm->copy_label(label);
	}
	pTermTabs->redraw();
	pTermWin->label(acTerm->title());
	pTermWin->redraw();
	menu_update();
}
void term_new()
{
	Fl_Term *pTerm = new Fl_Term(0, pTermTabs->y()+TABHEIGHT, 
						pTermTabs->w(), pTermTabs->h()-TABHEIGHT, "term");
	pTerm->labelsize(16);
	pTermTabs->insert(*pTerm, pTermTabs->children()-1);
	term_act(pTerm);
}
void term_tab(const char *host)
{
	for ( int i=0; i<pTermTabs->children(); i++ )
		if ( strncmp(host, pTermTabs->child(i)->label(), strlen(host))==0 )
			term_act((Fl_Term *)pTermTabs->child(i));
}
int term_cmd(char *cmd, char** preply)
{
	if ( !acTerm->live() ) {
		acTerm->puts(cmd);
		return 0;
	}
	if ( *cmd=='#' ) {
		cmd++;
		if ( strncmp(cmd,"scp ",4)==0 ) scp(cmd+4);
		else if ( strncmp(cmd,"tun ",4)==0 ) tun(cmd+4);
		else if ( strncmp(cmd,"Wait",4)==0 ) sleep(atoi(cmd+5));
		else if ( strncmp(cmd,"Log ",4)==0 ) acTerm->logging( cmd+4 );
		else if ( strncmp(cmd,"Save",4)==0 ) acTerm->save( cmd+5 );
		else if ( strncmp(cmd,"Clear",5)==0 ) acTerm->clear();
		else if ( strncmp(cmd,"Title",5)==0 ) acTerm->copy_label( cmd+6 );
		else if ( strncmp(cmd,"Waitfor",7)==0 ) acTerm->waitfor(cmd+8); 
		else if ( strncmp(cmd,"Timeout",7)==0 ) acTerm->timeout(atoi(cmd+8));
		else if ( strncmp(cmd,"Prompt",6)==0 ) {
			fl_decode_uri(cmd+7);
			acTerm->prompt(cmd+7);
		}
		return 0;
	}

	Fan_Host *host = (Fan_Host *)acTerm->term_data();
	acTerm->mark_prompt();
	host->write(cmd, strlen(cmd));
	host->write("\r",1);
	return acTerm->wait_prompt(preply);
}
void term_del()
{
	if ( acTerm!=pAbout ) {
		if ( acTerm->live() ) {
			Fan_Host *host = (Fan_Host *)acTerm->term_data();
			host->disconn();
			delete host;
		}
		pTermTabs->remove(acTerm);
		Fl::delete_widget(acTerm);  //crash if delete active connection on apple
		acTerm = NULL;
		term_act((Fl_Term *)pTermTabs->child(0));
	}
}
const char *protocols[]={"serial ","telnet ", "ssh ","sftp ","netconf "};
const char *ports[]={"/dev/tty.usbserial","23", "22", "22", "830"};
void term_connect(int proto, const char *host)
{
	assert ( acTerm!=pAbout );  
	Fan_Host *pHost=NULL;
	switch ( proto ) {
	case 0: //serial
		pHost = new comHost(host); break;
	case 1: //telnet
		pHost = new tcpHost(host); break;
	case 2: //ssh
		pHost = new sshHost(host); break;
	case 3: //sftp
		pHost = new sftpHost(host); break;
	case 4: //netconf
		pHost = new confHost(host); break;
	case 5: //ftpd
		pHost = new ftpDaemon(host); break;
	case 6: //tftpd
		pHost = new tftpDaemon(host); break;
	default: return;
	}
	if ( pHost!=NULL ) {
		pHost->callback(proto==4?conf_host_cb:host_cb, acTerm);
		acTerm->callback(term_cb, pHost);
		acTerm->copy_label(pHost->name());
		pHost->connect();
	}
	term_act(acTerm);
}
void term_connect(const char *host)
{
	for ( int i=0; i<5; i++ ) {
		int l = strlen(protocols[i]);
		if ( strncmp(host, protocols[i],l)==0 ) {
			term_connect(i, host+l);
			break;
		}
	}
}

void conn_callback(Fl_Widget *w, void *data);
void new_callback(Fl_Widget *w, void *data)
{
	term_new();
	conn_callback(NULL, NULL);
}
void del_callback(Fl_Widget *w, void *data)
{
	int confirm = 0;
	if ( acTerm->live() ) 
		confirm = fl_choice("Disconnect from %s?", "Yes", "No", 0, 
												acTerm->label() );
	if ( confirm==0 ) term_del();
}
void tab_callback(Fl_Widget *w) 
{
	Fl_Term *pTerm = (Fl_Term *)pTermTabs->value();

	if ( pTerm!=pAbout ) {
		if ( pTerm==acTerm )  	//clicking on active tab, delete it
			del_callback(NULL, NULL);
		else
			term_act(pTerm);	//clicking on inactive tab, activate it
	}
	else 						//clicking on "+" tab
		new_callback(NULL, NULL);
}
void menu_update()
{
    Fl_Menu_Item *p;
    if ( acTerm->live() ) {
		p = (Fl_Menu_Item*)pMenu->find_item("Session/&Connect...");
		if ( p ) {
			p->label("&Disconnect");
			p->shortcut(FL_ALT+'d');
		}
	}
	else {
		p = (Fl_Menu_Item*)pMenu->find_item("Session/&Disconnect");
		if ( p ) {
			p->label("&Connect...");
			p->shortcut(FL_ALT+'c');
		}
	}
}
void menu_callback(Fl_Widget *w, void *data)
{
	const char *session = pMenu->text();
	if ( session ) {
		if ( acTerm->live() || acTerm==pAbout ) term_new();
		term_connect((const char *)data);
	}
}
void protocol_callback(Fl_Widget *w)
{
	static int proto = 2;
	if ( proto==0 ) 
		pSettings->menubutton()->clear();
	proto = pProtocol->value();
	pPort->value(ports[proto]);
	if ( proto==0 ) {
		pHostname->menubutton()->clear();
#ifdef WIN32		//detect available serial ports
		pPort->value("");
		for ( int i=1; i<16; i++ ) {
			char port[32];
			sprintf( port, "\\\\.\\COM%d", i );
			HANDLE hPort = CreateFile( port, GENERIC_READ, 0, NULL,
										OPEN_EXISTING, 0, NULL);
			if ( hPort != INVALID_HANDLE_VALUE ) {
				pPort->add(port+4);
				pPort->value(port+4);
				CloseHandle( hPort );
			}
		}
#endif	
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
void connect_callback(Fl_Widget *w)
{
	char buf[256];
	int proto = pProtocol->value();
	if ( proto>0 ) {
		pHostname->add(pHostname->value());
		strcpy(buf, pHostname->value());
		if ( strcmp(ports[proto],pPort->value())!=0 ) {
			strcat(buf, ":");
			strcat(buf, pPort->value());
		}
		char *item = (char *)malloc(256);
		if ( item!=NULL ) {
			sprintf(item, "Session/%s %s", protocols[proto],buf );
			Fl_Menu_Item *p = (Fl_Menu_Item*)pMenu->find_item(item);
			if ( p==NULL ) 
				pMenu->insert(11, item, 0, menu_callback, (void *)(item+8));
		}
	}
	else {
		strcpy(buf, pPort->value());
		strcat(buf, ":");
		strcat(buf, pSettings->value());
	}
	pDialog->hide();
	term_connect(proto, buf); 
}
void cancel_callback(Fl_Widget *w)
{
	pDialog->hide();
}
void conn_callback(Fl_Widget *w, void *data)
{
	if ( acTerm==pAbout ) term_new();
	if ( acTerm->live() ) {
		Fan_Host *host = (Fan_Host *)acTerm->term_data();
		host->disconn();
	}	
	else {
		pDialog->resize(pTermWin->x()+100, pTermWin->y()+100, 360, 200);
		pDialog->show();
		pHostname->take_focus();
	}
}
void clear_callback(Fl_Widget *w, void *data)
{
	acTerm->clear();
	acTerm->redraw();
}
void log_callback(Fl_Widget *w, void *data)
{
	if ( acTerm->logging() ) 
		acTerm->logging( NULL );
	else {
		const char *fname = file_chooser("Log to file:", "Log\t*.log");	
		if ( fname!=NULL ) acTerm->logging(fname);
	}
}
void save_callback(Fl_Widget *w, void *data)
{
	const char *fname = file_chooser("Save to file:", "Text\t*.txt");
	if ( fname!=NULL ) acTerm->save(fname);
}
void ftpd_callback(Fl_Widget *w, void *data)
{
	const char *dir = fl_dir_chooser("choose FTPd root dir", ".", 0);
	if ( dir!=NULL ) { 
		term_new(); 
		term_connect(5, dir);
	}
}
void tftpd_callback(Fl_Widget *w, void *data)
{
	const char *dir = fl_dir_chooser("choose TFTPd root dir", ".", 0);
	if ( dir!=NULL ) {
		term_new();
		term_connect(6, dir);
	}
}
void echo_callback(Fl_Widget *w, void *data)
{
	if ( !acTerm->live() ) return;
	Fan_Host *host = (Fan_Host *)acTerm->term_data();
	host->echo(!host->echo());
	acTerm->puts(host->echo()?"\n\033[31mlocal echo ON\033[37m\n":
							  "\n\033[31mlocal echo OFF\033[37m\n");
}
void font_callback(Fl_Widget *w, void *data)
{
	int ts = acTerm->textsize()+2;
	if ( ts>16 ) ts = 12;
	acTerm->textsize(ts);
}
void close_callback(Fl_Widget *w, void *data)
{
	int active = false;
	for ( int i=0; i<pTermTabs->children(); i++ ) {
		Fl_Term *pTerm = (Fl_Term *)pTermTabs->child(i);
		if ( pTerm->live() ) {
			active = true;
			break;
		}
	}

	if ( active ) {
		int confirm = fl_choice("Disconnect all and exit?", "Yes", "No", 0);
		if ( confirm==1 ) return;
	}
	while ( acTerm!=pAbout ) term_del();
	pCmdWin->hide();
	pTermWin->hide();
}
void cmd_callback(Fl_Widget *o) 
{
	char cmd[256];
	strncpy(cmd, pCmd->value(), 255);
	cmd[255] = 0;
	pCmd->position(strlen(cmd), 0);
	pCmd->add( cmd );
	switch( *cmd ) {
		case '/':  acTerm->srch(cmd+1); break;
		case '\\': acTerm->srch(cmd+1, 1); break;
		case '#':  term_cmd(cmd, NULL); break;
		default: if ( acTerm->live() ) {
					Fan_Host *host = (Fan_Host *)acTerm->term_data();
					host->write(cmd, strlen(cmd));
					host->write("\r",1);
				}
				else { 
					if ( acTerm==pAbout ) term_new();
					term_connect(cmd);
				}
	}
}
void editor_callback(Fl_Widget *w, void *data)
{
	if ( Fl::focus()==pCmd ) {
		pCmdWin->hide();
		if ( acTerm!=pAbout ) acTerm->take_focus();
	}
	else {
		pCmdWin->resize( pTermWin->x()+220, pTermWin->y(),
						 pTermWin->w()-220, TABHEIGHT );
		pCmdWin->show();
		pCmd->take_focus();
	}
}
int shortcut_handler(int e)
{
	if ( e==FL_SHORTCUT && Fl::event_alt() )
		switch ( Fl::event_key() ) {
		case 'l':	editor_callback(NULL, NULL);
					return 1;
		}
	return 0;
}

int main() {
	int http_port = httpd_init();
	libssh2_init(0);
	char buf[4096];
	sprintf(buf, ABOUT_TERM, http_port);

	pTermWin = new Fl_Double_Window(800, 640, "Fl_Term");
	{
		pMenu=new Fl_Sys_Menu_Bar(0,0,pTermWin->w(),TABHEIGHT);
		pMenu->add("Terminal/New", 		   	0, 	new_callback, 	NULL);
		pMenu->add("Terminal/Log...",	   	0,	log_callback, 	NULL);
		pMenu->add("Terminal/Save...",		0, 	save_callback, 	NULL);
		pMenu->add("Terminal/Clear", 	   	0, 	clear_callback, NULL);
		pMenu->add("Terminal/Delete",	 	0,	del_callback, 	NULL);
		pMenu->add("Session/&Connect...","#c",	conn_callback,	NULL);
		pMenu->add("Session/FTP server",  	0, 	ftpd_callback,	NULL);
		pMenu->add("Session/TFTP server", 	0,	tftpd_callback,	NULL,
													FL_MENU_DIVIDER );
		pMenu->add("Option/local &Echo", "#e",	echo_callback,	NULL);
		pMenu->add("Option/&Font size",  "#f", 	font_callback, 	NULL);
		pMenu->add("Option/&Line Editor","#l",	editor_callback,NULL);
		pMenu->textsize(16);
		pMenu->box(FL_FLAT_BOX);
		pTermTabs = new Fl_Tabs(0, MENUHEIGHT, pTermWin->w(), 
												pTermWin->h()-MENUHEIGHT);
		pTermTabs->when(FL_WHEN_RELEASE|FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED);
        pTermTabs->callback(tab_callback);
			pAbout = new Fl_Term(0, MENUHEIGHT+TABHEIGHT, pTermTabs->w(),
														pTermTabs->h(), "+");
			pAbout->textsize(20);
			pAbout->puts(buf);
			acTerm = pAbout;
		pTermTabs->resizable(pAbout);
		pTermTabs->selection_color(FL_CYAN);
		pTermTabs->end();
	}
	pTermWin->resizable(*pTermTabs);
	pTermWin->callback(close_callback);
	pTermWin->end();

	pCmdWin = new Fl_Double_Window(800, TABHEIGHT, "Cmd");
	pCmdWin->border(0);
	{
	  	pCmd = new acInput(40, 0, pCmdWin->w()-0, TABHEIGHT, "CMD:");
	  	pCmd->color(FL_CYAN);
		pCmd->box(FL_FLAT_BOX);
		pCmd->textsize(16);
	  	pCmd->when(FL_WHEN_ENTER_KEY_ALWAYS);
	  	pCmd->callback(cmd_callback);
	}
	pCmdWin->focus(pCmd);
	pCmdWin->end();
	pCmdWin->resize( pTermWin->x(), pTermWin->y()+pTermWin->h()-TABHEIGHT,
												pTermWin->w(), TABHEIGHT );
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
		pHostname->value("192.168.1.1");
		pProtocol->callback(protocol_callback);
		pConnect->callback(connect_callback);
		pCancel->callback(cancel_callback);
	}
	pDialog->end();
	pDialog->set_modal();
	Fl::lock();
#ifdef WIN32
    pTermWin->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	pTermWin->show();

	FILE *fp = fopen("flTerm.dic", "r");
	if ( fp!=NULL ) {
		char line[256];
		while ( fgets(line, 255, fp)!=NULL ) {
			int l = strlen(line)-1;
			while ( line[l]=='\015' || line[l]=='\012' ) line[l--]=0; 
			pCmd->add(line);
			if ( strncmp(line, "ssh ",4)==0 ) 
				pMenu->insert(11,line+4,0,menu_callback,(void *)strdup(line));
		}
	}
	
	Fl::add_handler(shortcut_handler);
	while ( Fl::wait() ) {
		Fl_Widget *pt = (Fl_Widget *)Fl::thread_message();
		if ( pt!=NULL )pt->redraw();
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
					\nConnection: Keep-Alive\
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
			if ( strncmp(buf, "GET /", 5)==0 ) {
				cmd = buf+5;
				char *p = strchr(cmd, ' ');
				if ( p!=NULL ) *p = 0;
				if ( *cmd=='?' ) {
					for ( char *p=++cmd; *p!=0; p++ )
						if ( *p=='+' ) *p=' ';
					fl_decode_uri(cmd);
					
					replen = 0;
					if ( strncmp(cmd, "New=", 4)==0 ) {
						term_new();
						term_connect( cmd+4 );
					}
					else if ( strncmp(cmd, "Tab=", 4)==0 ) 
						term_tab( cmd+4 ); 
					else if ( strncmp(cmd, "Cmd=", 4)==0 ) 
						replen = term_cmd( cmd+4, &reply );
					int len = sprintf( buf, HEADER, "200 OK", replen );
					send( http_s1, buf, len, 0 );
					if ( replen>0 ) send( http_s1, reply, replen, 0 );
				}
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
	short port = 8089;
	int rc = -1;
	while ( rc==-1 && port<8100 ) {
		svraddr.sin_port=htons(++port);
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

/** taken out of Fl_Term.cxx so that NETables don't have to do this    **/
void scp_writer(Fl_Term *pTerm, Fan_Host *host, char *script)
{
	char *p, *p0, *p1, *p2, pwd[4]="pwd";
	char lfile[1024], rfile[1024];
	int rpath_len;

	term_cmd(pwd, &p2);
	p1 = strchr(p2, 0x0a);
	if ( p1==NULL ) goto done;
	p2 = p1+1;
	p1 = strchr(p2, 0x0a);
	if ( p1==NULL ) goto done;
	rpath_len = p1-p2;
	strncpy(rfile, p2, rpath_len);
	rfile[rpath_len++]='/';

	p0 = script;
	do {
		p2=p1=strchr(p0, 0x0a);
		if ( p1==NULL ) 
			p1 = p0+strlen(p0);
		else 
			*p1 = 0; 
		strncpy(lfile, p0, 1023);
		for ( p=lfile; *p; p++ ) 
			if ( *p=='\\' ) *p='/';

		p = strrchr(lfile, '/');
		if (p!=NULL) p++; else p=lfile;
		strcpy(rfile+rpath_len, p);

		((sshHost *)host)->scp_write(lfile, rfile);
		p0 = p1+1;
	}
	while ( p2!=NULL ); 
	host->write("\015", 1);
done:
	delete script;
}
void sftp_copier(Fl_Term *pTerm, Fan_Host *host, char *script)
{
	char *p0, *p1, *p2, fn[1024];

	p0 = script;
	do {
		p2=p1=strchr(p0, 0x0a);
		if ( p1==NULL ) 
			p1 = p0+strlen(p0);
		else 
			*p1 = 0; 
		strcpy(fn, "put ");
		strncat(fn, p0, 1020);
		for ( unsigned int i=0; i<strlen(fn); i++ ) 
			if ( fn[i]=='\\' ) fn[i]='/';
		((sftpHost *)host)->sftp(fn);
		p0 = p1+1;
	}
	while ( p2!=NULL ); 
	pTerm->puts("sftp> ");
	delete script;
}

void term_scripter(Fl_Term *pTerm, Fan_Host *host, char *script)
{
	char *p0=script, *p1;	
	while ( (p1=strchr(p0, 0x0a))!=NULL ) {
		if ( *p0=='#' ) {
			*p1 = 0;
			term_cmd(p0, NULL);
		}
		else {
			*p1 = '\015';
			pTerm->mark_prompt();
			host->write(p0, p1-p0+1);
			pTerm->wait_prompt(NULL);
		}
		p0 = p1+1;
	}
	if ( *p0 ) host->write(p0, strlen(p0));
	delete script;
}
void term_dnd(Fl_Term *term, Fan_Host *host, const char *buf)
{
	char *script = strdup(buf);	//script thread must delete this

	switch ( host->type() ) {
	case HOST_CONF: host->write(buf, strlen(buf)); 
					break;
	case HOST_SFTP: {
				std::thread scripterThread(sftp_copier, term, host, script);
				scripterThread.detach();
			}
			break;
	case HOST_SSH: {
				char *p0 = script;
				char *p1=strchr(p0, 0x0a);
				if ( p1!=NULL ) *p1=0;
				struct stat sb;
				int rc = stat(p0, &sb);		//is this a list of files?
				if ( p1!=NULL ) *p1=0x0a;
				if ( rc!=-1 ) {
					std::thread scripterThread(scp_writer, term, host, script);
					scripterThread.detach();
					break;
				}
			}
	default: {
				std::thread scripterThread(term_scripter, term, host, script);
				scripterThread.detach();
			}
			break;
	}
}
int scp_read(sshHost *pHost, const char *rpath, 
											const char *lpath)
{
	if ( strchr(rpath,'*')==NULL && strchr(rpath, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, lpath);
		struct stat statbuf;
		if ( stat(lpath, &statbuf)!=-1 ) {
			if ( S_ISDIR(statbuf.st_mode) ) {
				strcat(lfile, "/");
				const char *p = strrchr(rpath, '/');
				if ( p!=NULL ) p++; else p=rpath;
				strcat(lfile, p);
			}
		}	
		pHost->scp_read(rpath, lfile);
	}
	else {
		char rnames[4096]="ls -1 ", *rlist;
		if ( *rpath!='/' ) strcat(rnames, "~/");
		strcat(rnames, rpath);
		if ( term_cmd(rnames, &rlist )>0 ) {
			char rdir[1024], rfile[1024], lfile[1024];
			char *p1, *p2, *p = strrchr(rnames, '/');
			if ( p!=NULL ) *p=0;
			strncpy(rdir, rnames+6, 1023);
			strncpy(rnames, rlist, 4095);
			p = strchr(rnames, '\012');
			if ( p==NULL ) return 0;
			p++;
			while ( (p1=strchr(p, '\012'))!=NULL ) {
				*p1++ = 0; 
				strcpy(rfile, p);
				p2 = strrchr(p, '/');
				if ( p2==NULL ) p2=p; else p2++;
				strcpy(lfile, lpath);
				strcat(lfile, "/");
				strcat(lfile, p2);
				pHost->scp_read(rfile, lfile);
				p = p1;
			}
		}
	}
	pHost->write("\015", 1);
	return 0;
}
int scp_write(sshHost *pHost, const char *lpath, 
											const char *rpath)
{
	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;

	if ( stat(lpath, &statbuf)!=-1 ) {
		char rnames[1024]="ls -ld ", *rlist, rfile[1024];
		if ( *rpath!='/' ) strcat(rnames, "~/");
		strcat(rnames, rpath);
		strcpy(rfile, *rpath?rpath:".");
		if ( term_cmd(rnames, &rlist )>0 ) {
			const char *p = strchr(rlist, '\012');
			if ( p!=NULL ) if ( p[1]=='d' ) {
				p = strrchr(lpath, '/');
				if ( p!=NULL ) p++; else p=lpath;
				strcat(rfile, "/");
				strcat(rfile, p);
			}
		}
		pHost->scp_write(lpath, rfile);
	}
	else {
		const char *lname=lpath;
		char ldir[1024]=".";
		char *p = (char *)strrchr(lpath, '/');
		if ( p!=NULL ) {
			*p++ = 0; 
			lname = p;
			strcpy(ldir, lpath);
		}

		if ( (dir=opendir(ldir) ) == NULL ){
			pHost->print("\n\033[31mSCP: couldn't open local directory\033[32m%s\033[30m\n", ldir);
			return 0;
		}
		while ( (dp=readdir(dir)) != NULL ) {
			if ( fnmatch(lname, dp->d_name, 0)==0 ) {
				char lfile[1024], rfile[1024];
				strcpy(lfile, ldir);
				strcat(lfile, "/");
				strcat(lfile, dp->d_name);
				strcpy(rfile, rpath);
				strcat(rfile, "/");
				strcat(rfile, dp->d_name);
				pHost->scp_write(lfile, rfile);
			}
		}
	}
	pHost->write("\015", 1);
	return 0;
}
void scp(char *cmd)
{
	static char lpath[1024], rpath[1024];
	char *p = strchr(cmd, ' ');
	if ( p==NULL ) return;
	Fan_Host *host = (Fan_Host *)(acTerm->term_data());
	if ( host->type()!=HOST_SSH ) return;

	*p=0; 
	strncpy(rpath, cmd, 1023);
	strncpy(lpath, p+1, 1023);
	if ( *rpath==':' ) {
		std::thread scp_thread(scp_read, (sshHost *)host, rpath+1, lpath);
		scp_thread.detach();
	}
	else if ( *lpath==':') {
		std::thread scp_thread(scp_write, (sshHost *)host, rpath, lpath+1);
		scp_thread.detach();
	}
}
void tun(char *cmd)
{
	static char path[256];
	while ( *cmd==' ' ) cmd++;
	strncpy(path, cmd, 255);
	path[255] = 0;
	char *p = strchr(path, ' ');
	if ( p==NULL ) { 	//close tunnel
		return;
	}
	*p=0; p++; 

	Fan_Host *host = (Fan_Host *)(acTerm->term_data());
	if ( host->type()!=HOST_SSH ) return;
	if ( *path=='R' ) { //start remote tunnel
		std::thread tun_thread(&sshHost::tun_remote,(sshHost *)host,path+1,p);
		tun_thread.detach();
	}
	else {				//start local tunnel
		std::thread tun_thread(&sshHost::tun_local, (sshHost *)host, path, p);
		tun_thread.detach();
	}
	sleep(1);
}
