//
// "$Id: flTerm.cxx 16563 2018-05-25 23:55:10 $"
//
// flTerm -- A minimalist ssh terminal simulator
//
//    an example application using the Fl_Term widget.
//
// Copyright 2017-2018 by Yongchao Fan.
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

const char ABOUT_TERM[]="\n\n\n\
    flTerm is a terminal simulator for network engineers,\n\n\
    a simple telnet/ssh/scp/sftp client that features:\n\n\n\
        * minimalist user interface\n\n\
        * unlimited screen buffer size\n\n\
        * Select to copy, right click to paste\n\n\
        * Drag and Drop to run list of commands\n\n\
        * Scripting interface \033[34mxmlhttp://127.0.0.1:%d\n\n\n\
    \033[30mby yongchaofan@gmail.com		05-25-2018\n\n\
    https://github.com/zoudaokou/flTerm\n";

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <thread>
#include "acInput.h"
#include "Fl_Term.h"
#include "Fl_Host.h"
#include "ssh2.h"

#define MARGIN 20
#include <FL/x.H>               // needed for fl_display
#include <FL/Fl.H>
#include <FL/Fl_Ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
//#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Native_File_Chooser.H>
int host_init();
void host_exit();
void cmd_disp(const char *buf);

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

Fl_Window *pTermWin;
Fl_Sys_Menu_Bar *pMenu;
Fl_Tabs *pTermTabs;
Fl_Term *pAbout;
Fl_Term *acTerm;

Fl_Window *pCmdWin;
acInput *pCmd;

Fl_Window *pDialog;
Fl_Choice *pProtocol;
Fl_Input *pHostname;
Fl_Input *pPort;
//Fl_Input *pUser;
//Fl_Secret_Input *pPass;
Fl_Button *pConnect;
Fl_Button *pCancel;

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
	Fl::awake(pTermWin);
}
const char *term_get(const char *prompt, int echo)
{
	return acTerm==pAbout?NULL:acTerm->gets(prompt, echo);
}
void term_new(const char *host)
{
	Fl_Host *pHost = NULL;
	Fl_Term *pTerm = NULL;
	if ( strncmp(host, "telnet ", 7)==0 ) {
		pHost = new tcpHost(host+7);
	}
	else if ( strncmp(host, "ssh ", 4)==0 ) {
		pHost = new sshHost(host+4);
	}
	else if ( strncmp(host, "sftp ", 5)==0 ) {
		pHost = new sftpHost(host+5);
	}
	else if ( strncmp(host, "netconf ", 8)==0 ) {
		pHost = new confHost(host+8);	
	}
#ifdef WIN32
	else if ( strncmp(host, "serial ", 7)==0 ) {
		pHost = new comHost(host+7);
	}
#endif
	else if ( strncmp(host, "ftpd ", 5)==0 ) { 
		char *root = fl_dir_chooser("choose root directory", ".", 0);
		if ( root!=NULL ) pHost = new ftpDaemon(root);
	}
	else if ( strncmp(host, "tftpd ", 6)==0 ) {
		char *root = fl_dir_chooser("choose root directory", ".", 0);
		if ( root!=NULL ) pHost = new tftpDaemon(root);
	}
	if ( !pHost ) return;
	if ( (acTerm==pAbout) | (acTerm->active()) ) { 
		pTerm=new Fl_Term(0, pTermTabs->y()+MARGIN, pTermTabs->w(), 
										pTermTabs->h()-MARGIN, "term");
		pTerm->labelsize(16);
		pTermTabs->insert(*pTerm, pTermTabs->children()-1);
		pTerm->set_host(pHost);
		pTerm->start_reader();
		term_act(pTerm);
	}
	else {
		acTerm->set_host(pHost);
		acTerm->start_reader();		
	}
}
void term_tab(const char *host)
{
	for ( int i=0; i<pTermTabs->children(); i++ )
		if ( strncmp(host, pTermTabs->child(i)->label(), strlen(host))==0 )
			term_act((Fl_Term *)pTermTabs->child(i));
}
int term_del()
{
	if ( acTerm->active() ) {
		int confirm = fl_choice("Disconnect from %s?", "Yes", "No", 0, 
														acTerm->label() );
		if ( confirm==1 ) return false;
		acTerm->stop_reader();
		acTerm->set_host(NULL);
	}
	if ( acTerm!=pAbout ) {
		pTermTabs->remove(acTerm);
//		Fl::delete_widget(acTerm);  //crash if delete tab with active connection
		acTerm = NULL;
		term_act((Fl_Term *)pTermTabs->child(0));
	}
	return true;
}
void tab_callback(Fl_Widget *w) 
{
	Fl_Term *pTerm = (Fl_Term *)pTermTabs->value();

	if ( pTerm!=pAbout ) {
		if ( pTerm==acTerm ) 	//clicking on active tab, delete it
			term_del();
		else
			term_act(pTerm);	//clicking on inactive tab, activate it
	}
	else {						//clicking on "+" tab
		if ( acTerm!=pAbout ) {	//return to former active tab
			pTermTabs->value(acTerm);	
			acTerm->take_focus();
		}
		pDialog->show();		//then open connect dialog
		pHostname->take_focus();
	}
}
int term_cmd(const char *cmd, char** preply)
{
	int rc = 0;

	cmd_disp(cmd);
	if ( acTerm->active() ) 
		rc = acTerm->command( cmd, preply );
	else
		term_new(cmd);

	return rc;
}
void menu_callback(Fl_Widget *w, void *data)
{
	const char *session = pMenu->text();
	if ( session ) term_new((const char *)data);
}
void menu_add(char *line)
{
	if ( strncmp(line, "ssh ",4)==0 ||
		 strncmp(line, "telnet ",7)==0 ||
		 strncmp(line, "serial ",7)==0 ||
		 strncmp(line, "netconf ",8)==0 ) {
		char *menuline = strdup(line);
		char *p = strrchr(line, ' ');
		if ( p[1]!= 0 ) {
#ifdef WIN32
			char *p1 = strchr(p+1, '@');
			if ( p1!=NULL ) {
				for (int i=0; i<p1-p; i++ ) 
					p[i] = p[i+1];
			}
			else
				p++;
#endif			
			char item[40] = "Sessions/";
			strncat(item, p, 31);
			pMenu->add(item, 0, menu_callback, (void *)menuline );
		}
	}	
}
void cmd_disp(const char *buf)
{
	pCmd->value(buf); 
	pCmd->position(0, strlen(buf));
	Fl::awake(pCmd);
}
void cmd_callback(Fl_Widget *o) 
{
	char cmd[256];
	strncpy(cmd, pCmd->value(), 255);
	pCmd->position(0, strlen(cmd));
	pCmd->add( cmd );
	switch( *cmd ) {
		case '/':  acTerm->srch(cmd+1); break;
		case '\\': acTerm->srch(cmd+1, 1); break;
		case '#':  acTerm->command(cmd, NULL); break;
		default: if ( acTerm->active() ) {
					acTerm->write(cmd); 
					acTerm->write("\r");
				}
				else {
					term_new(cmd);
					menu_add(cmd);
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
		int x = acTerm->cursorx();
		pCmdWin->resize( pTermWin->x()+x, 
						 pTermWin->y()+pTermWin->h()-MARGIN,
						 pTermWin->w()-x, MARGIN );
		pCmdWin->show();
		pCmd->take_focus();
	}
}
int shortcut_handler(int e)
{
	if ( e==FL_SHORTCUT && Fl::event_alt() )
		switch ( Fl::event_key() ) {
		case 'e':	editor_callback(NULL, NULL);
					return 1;
		}
	return 0;
}

void font_callback(Fl_Widget *w, void *data)
{
	acTerm->textsize(0);
}
void about_callback(Fl_Widget *w, void *data)
{
	pTermTabs->value(pAbout);
	Fl::awake(pTermWin);
}
void clear_callback(Fl_Widget *w, void *data)
{
	acTerm->clear();
}
void save_callback(Fl_Widget *w, void *data)
{
	acTerm->save(file_chooser("Save to file:", "Text\t*.txt"));
}
void log_callback(Fl_Widget *w, void *data)
{
	acTerm->logg( acTerm->logging() ? 
					NULL:file_chooser("Log to file:", "Log\t*.log"));	
}
void close_callback(Fl_Widget *w, void *data)
{
	while ( acTerm!=pAbout )
		if ( !term_del() ) return;
	pCmdWin->hide();
	pTermWin->hide();
}

const char *protocols[]={"telnet ","ssh ","sftp ","netconf ","serial ","ftpd ", "tftpd "};
const char *ports[]={"23", "22", "22", "830","9600,n,8,1","21", "61"};
void new_callback(Fl_Widget *, void *data)
{
	pDialog->show();
	pHostname->take_focus();
}
void connect_callback(Fl_Widget *w)
{
	char buf[256];
	int proto = pProtocol->value();
	strcpy(buf, protocols[proto]);
	const char *p;

/*	p = pUser->value();
	if ( *p ) {
		strcat(buf, "-l ");
		strcat(buf, p );
		strcat(buf, " ");
	}
	p = pPass->value();	
	if ( *p ) {
		strcat(buf, "-pw ");
		strcat(buf, p );
		strcat(buf, " ");
	}
*/
	strcat(buf, pHostname->value());
	p = pPort->value();
	if ( *p && strcmp(p, ports[proto])!=0 ) {
		strcat(buf, ":");
		strcat(buf, p );
	}
	pDialog->hide();
	term_new(buf); 
	menu_add(buf);
}
void cancel_callback(Fl_Widget *)
{
	pDialog->hide();
}
void protocol_callback(Fl_Widget *w)
{
	int proto = pProtocol->value();
	pPort->value(ports[proto]);
	if ( proto==5 || proto==6 ) 
		pHostname->value("");
	else if ( proto==4 ) {
		pHostname->value("COM1");
		pHostname->label("Port:");
		pPort->label("Settings:");
	}
	else {
		pHostname->label("Host:");
		pHostname->value("192.168.1.1");
		pPort->label("       Port:");
	}
}
int main() {
	int http_port = host_init();
	pTermWin = new Fl_Double_Window(800, 640, "Fl_Term");
	{
		pMenu=new Fl_Sys_Menu_Bar(0,0,pTermWin->w(),MARGIN);
		pMenu->add("Terminal/Log...",	"#l", 	log_callback, 	NULL);
		pMenu->add("Terminal/Save...",	"#s", 	save_callback, 	NULL);
		pMenu->add("Terminal/Clear", 	"#c", 	clear_callback, NULL);
		pMenu->add("Sessions/New...",	"#n",	new_callback, 	NULL);
		pMenu->add("Options/Cmd Editor","#e", 	editor_callback,NULL);
		pMenu->add("Options/Font size", "#f", 	font_callback, 	NULL);
		pMenu->textsize(16);
		pMenu->box(FL_FLAT_BOX);
#ifdef __APPLE__
		pTermTabs = new Fl_Tabs(0, 0, pTermWin->w(),pTermWin->h());
#else
		pTermTabs = new Fl_Tabs(0, MARGIN, pTermWin->w(),pTermWin->h()-MARGIN);
#endif //__APPLE__
		pTermTabs->when(FL_WHEN_RELEASE|FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED);
        pTermTabs->callback(tab_callback);
			pAbout = new Fl_Term(0, pTermTabs->y()+MARGIN, pTermTabs->w(),
														pTermTabs->h(), "+");
			pAbout->textsize(20);
			pAbout->print(ABOUT_TERM, http_port);
			acTerm = pAbout;
		pTermTabs->resizable(pAbout);
		pTermTabs->selection_color(FL_CYAN);
		pTermTabs->end();
	}
	pTermWin->resizable(*pTermTabs);
	pTermWin->callback(close_callback);
	pTermWin->end();

	pCmdWin = new Fl_Double_Window(800, MARGIN, "Command Center");
	pCmdWin->border(0);
	  	pCmd = new acInput(0, 0, pCmdWin->w()-0, MARGIN, "");
	  	pCmd->color(FL_GREEN);
		pCmd->box(FL_FLAT_BOX);
		pCmd->textsize(16);
	  	pCmd->when(FL_WHEN_ENTER_KEY_ALWAYS);
	  	pCmd->callback(cmd_callback);
	pCmdWin->focus(pCmd);
	pCmdWin->end();
	pCmdWin->resize( pTermWin->x(), pTermWin->y()+pTermWin->h()-MARGIN,
												pTermWin->w(), MARGIN );
	FILE *fp = fopen("flTerm.dic", "r");
	if ( fp!=NULL ) {
		char line[256];
		while ( fgets(line, 255, fp)!=NULL ) {
			int l = strlen(line)-1;
			while ( line[l]=='\015' || line[l]=='\012' ) line[l--]=0; 
			pCmd->add(line);
			menu_add(line);
		}
	}
	
	pDialog = new Fl_Window(300, 200, "Connect");
		pProtocol = new Fl_Choice(100,20,128,24, "Protocol:");
		pPort = new Fl_Input(100,60,128,24, "Port:");
		pHostname = new Fl_Input(100,100,128,24,"Host:");
		pConnect = new Fl_Button(160,160,80,24, "Connect");
		pCancel = new Fl_Button(60,160,80,24, "Cancel");
		pProtocol->textsize(16); pProtocol->labelsize(16);
		pHostname->textsize(16); pHostname->labelsize(16);
		pPort->textsize(16); pPort->labelsize(16);
//		pUser = new Fl_Input(300,60,128,24, "Username:");
//		pPass = new Fl_Secret_Input(300,100,128,24, "Password:");
//		pUser->textsize(16); pUser->labelsize(16);
//		pPass->textsize(16); pPass->labelsize(16);
		pConnect->labelsize(16);
		pConnect->shortcut(FL_Enter);
		pProtocol->add("telnet|ssh|sftp|netconf");
#ifdef WIN32
		pProtocol->add("serial|ftpd|tftpd");
#endif
		pProtocol->value(1);
		pHostname->value("192.168.1.1");
		pPort->value("22");
		pProtocol->callback(protocol_callback);
		pConnect->callback(connect_callback);
		pCancel->callback(cancel_callback);
	pDialog->end();
	pDialog->set_modal();
	Fl::lock();
#ifdef WIN32
    pTermWin->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	pTermWin->show();
	Fl::add_handler(shortcut_handler);

	while ( Fl::wait() ) {
		Fl_Widget *pt = (Fl_Widget *)Fl::thread_message();
		if ( pt!=NULL )pt->redraw();
	}
		
	host_exit();
	return 0;
}
int http_callback( char *buf, char **preply)
{
	int rc=0;
	if ( *buf=='?' ) buf++;
	for ( char *p=buf; *p!=0; p++ ) {
		if ( *p=='+' ) *p=' ';
		if ( *p=='%' && isdigit(p[1]) ){
			int a;
			sscanf( p+1, "%02x", &a);
			*p = a;
			strcpy(p+1, p+3);
		}
	}
	if ( strncmp(buf, "Cmd=", 4)==0 ) rc = term_cmd( buf+4, preply );
	else if ( strncmp(buf, "Tab=", 4)==0 ) term_tab( buf+4 ); 
	else if ( strncmp(buf, "New=", 4)==0 ) term_new( buf+4 );
	return rc;
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
	char buf[4096], *cmd, *reply=buf;
	int cmdlen, replen, http_s1;

	while ( (http_s1=accept(s0,(struct sockaddr*)&cltaddr,&addrsize ))!=-1 ) {
		while ( (cmdlen=recv(http_s1,buf,4095,0))>0 ) {
			buf[cmdlen] = 0;
			if ( strncmp(buf, "GET", 3)==0 ) {
				cmd = buf+5;
				char *p = strchr(cmd, ' ');
				if ( p!=NULL ) *p = 0;
				if ( *cmd=='?' ) {
					replen = http_callback(cmd, &reply);
					int len = sprintf( buf, HEADER, "200 OK", replen );
					send( http_s1, buf, len, 0 );
					if ( replen>0 ) send( http_s1, reply, replen, 0 );
				}
			}
		}
	}
	closesocket(http_s1);
}
static int http_s0 = 01;
int host_init()
{
#ifdef WIN32
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2,0), &wsadata);
#endif
	libssh2_init(0);
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
void host_exit()
{
	libssh2_exit();
	closesocket(http_s0);
}
/*******************************************************************************
#ifdef WIN32
#define ID_CLEAR	70
#define ID_SAVE		71
#define ID_LOG		72
#define ID_NEW		73
int sys_menu_handler( void *event, void *data )
{
	MSG *msg = (MSG *)event;
	if ( msg->message == WM_SYSCOMMAND ) {
		switch ( msg->wParam ) {
		case ID_CLEAR:	clear_callback(NULL, NULL); return 1;
		case ID_SAVE:	save_callback(NULL, NULL); return 1;
		case ID_LOG:	log_callback(NULL, NULL); return 1;
		case ID_NEW:	new_callback(NULL, NULL); return 1;
		default: return 0;
		}
	}
	return 0;
}	
void sys_menu_insert()
{
	HMENU hSysMenu = GetSystemMenu(GetActiveWindow(), FALSE);
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_CLEAR,"&Clear  \tAlt+C");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_SAVE, "&Save...\tAlt+S");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_LOG, 	"&Log... \tAlt+L");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_NEW, 	"&New... \tAlt+N");
}
#endif //WIN32
#ifdef WIN32
	sys_menu_insert();
	Fl::add_system_handler(sys_menu_handler, NULL);
#endif
#ifdef __APPLE__
		fl_mac_set_about(about_callback, NULL);
#endif //__APPLE__

int shortcut_handler(int e)
{
	if ( e==FL_SHORTCUT && Fl::event_alt() )
		switch ( Fl::event_key() ) {
		case 's':	save_callback(NULL, NULL);	return 1;
		case 'l':	log_callback(NULL, NULL);	return 1;
		case 'n':	new_callback(NULL, NULL);	return 1;
		case 'c':	clear_callback(NULL, NULL);	return 1;
		case 't':	if ( Fl::focus()==pCmd ) {
						pCmdWin->hide();
						if ( acTerm!=pAbout ) acTerm->take_focus();
					}
					else {
						int x = acTerm->cursorx();
						pCmdWin->resize( pTermWin->x()+x, 
										pTermWin->y()+pTermWin->h()-MARGIN,
										pTermWin->w()-x, MARGIN );
						pCmdWin->show();
						pCmd->take_focus();
					}
					return 1;
		}
	return 0;
}
	Fl::add_handler(shortcut_handler);

*******************************************************************************/