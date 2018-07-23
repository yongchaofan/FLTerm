//
// "$Id: flTerm.cxx 20615 2018-06-30 23:55:10 $"
//
// flTerm -- A minimalist ssh terminal simulator
//
//    an example application using the Fl_Term widget.
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

const char ABOUT_TERM[]="\n\n\n\
    flTerm is a terminal simulator for network engineers,\n\n\
    a simple telnet/ssh/scp/sftp client that features:\n\n\n\
        * minimalist user interface\n\n\
        * unlimited screen buffer size\n\n\
        * Select to copy, right click to paste\n\n\
        * Drag and Drop to run list of commands\n\n\
        * Scripting interface \033[34mxmlhttp://127.0.0.1:%d\033[30m\n\n\n\
    by yongchaofan@gmail.com		06-30-2018\n\n\
    https://github.com/zoudaokou/flTerm\n";

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

#include <thread>
#include "acInput.h"
#include "Fl_Term.h"
#include "Hosts.h"
#include "sftp.h"
#include "ftpd.h"

#define MARGIN 20
#include <FL/x.H>               // needed for fl_display
#include <FL/Fl.H>
#include <FL/Fl_Ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Native_File_Chooser.H>
int httpd_init();
void httpd_exit();
void scp(char *cmd);
void dnd_cb(void *w, const char *txt);

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
Fl_Button *pConnect;
Fl_Button *pCancel;

const char *kb_gets(const char *prompt, int echo)
{
	if ( acTerm!=pAbout ) {
		acTerm->puts(prompt);
		return acTerm->gets(echo);
	}
	return NULL;
}
const char *term_gets(void *pTerm, int echo)
{
	return ((Fl_Term *)pTerm)->gets(echo);
}
void term_puts(void *pTerm, const char *buf, int len)
{
	Fl_Term *term = (Fl_Term *)pTerm;
	term->puts(buf, len);
}
void term_putxml(void *pTerm, const char *msg, int len)
{
	Fl_Term *term = (Fl_Term *)pTerm;
	term->putxml(msg);
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
}
void term_new(const char *host)
{
	Fl_Term *pTerm = acTerm;
	if ( (acTerm==pAbout) | (acTerm->active()) ) { 
		pTerm=new Fl_Term(0, pTermTabs->y()+MARGIN, pTermTabs->w(), 
										pTermTabs->h()-MARGIN, "term");
		pTerm->labelsize(16);
		pTermTabs->insert(*pTerm, pTermTabs->children()-1);
	}

	Fan_Host *pHost = NULL;
	if ( strncmp(host, "telnet ", 7)==0 ) {
		pHost = new tcpHost(host+7);
	}
	else if ( strncmp(host, "ssh ", 4)==0 ) {
		pHost = new sshHost(host+4);
	}
	else if ( strncmp(host, "sftp ", 5)==0 ) {
		pHost = new sftpHost(host+5);
		pTerm->command("#Timeout 300", NULL);
	}
	else if ( strncmp(host, "netconf ", 8)==0 ) {
		pHost = new confHost(host+8);
	}
#ifdef WIN32
	else if ( strncmp(host, "serial ", 7)==0 ) {
		pHost = new comHost(host+7);
	}
	else if ( strncmp(host, "ftpd ", 5)==0 ) { 
		char *root = fl_dir_chooser("choose root directory", ".", 0);
		if ( root!=NULL ) pHost = new ftpDaemon(root);
	}
	else if ( strncmp(host, "tftpd ", 6)==0 ) {
		char *root = fl_dir_chooser("choose root directory", ".", 0);
		if ( root!=NULL ) pHost = new tftpDaemon(root);
	}
#endif
	if ( !pHost ) return;
	pHost->gets_callback(term_gets, pTerm);
	pHost->puts_callback(pHost->type()==HOST_CONF?term_putxml:term_puts, pTerm);
	pTerm->set_host(pHost);
	pTerm->callback(dnd_cb);
	pTerm->start_reader();		
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
	if ( !acTerm->active() ) {
		acTerm->puts(cmd);
		return 0;
	}
	if ( *cmd=='#' ) {
		Fan_Host *host = acTerm->get_host();
		if ( host!=NULL ) if ( host->type()==HOST_SSH ) {
			if ( strncmp(cmd,"#scp ",5)==0 ) { 
				scp(cmd+5);
				return 0;
			}
			if ( strncmp(cmd,"#tun ",5)==0 ) {
				((sshHost*)host)->tun(cmd+5);
				return 0;
			}
		}
	}

	return acTerm->command( cmd, preply );
}
void term_del()
{
	if ( acTerm!=pAbout ) {
		if ( acTerm->active() ) {
			acTerm->stop_reader();
			acTerm->set_host(NULL);
		}
		pTermTabs->remove(acTerm);
//		Fl::delete_widget(acTerm);  //crash if delete tab with active connection
		acTerm = NULL;
		term_act((Fl_Term *)pTermTabs->child(0));
	}
}
void tab_callback(Fl_Widget *w) 
{
	Fl_Term *pTerm = (Fl_Term *)pTermTabs->value();

	if ( pTerm!=pAbout ) {
		if ( pTerm==acTerm ) { 	//clicking on active tab, delete it
			int confirm = 0;
			if ( acTerm->active() ) 
				confirm = fl_choice("Disconnect from %s?", "Yes", "No", 0, 
														acTerm->label() );
			if ( confirm==0 ) term_del();
		}
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
		pCmdWin->resize( pTermWin->x()+200, 
						 pTermWin->y()+pTermWin->h()-MARGIN,
						 pTermWin->w()-200, MARGIN );
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
	int active = false;
	for ( int i=0; i<pTermTabs->children(); i++ ) {
		Fl_Term *pTerm = (Fl_Term *)pTermTabs->child(i);
		if ( pTerm->active() ) {
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

const char *protocols[]={"telnet ", "ssh ","sftp ","netconf ",
									"serial ","ftpd ", "tftpd "};
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
	int http_port = httpd_init();
	libssh2_init(0);
	char buf[4096];
	sprintf(buf, ABOUT_TERM, http_port);

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
			pAbout->puts(buf);
			acTerm = pAbout;
		pTermTabs->resizable(pAbout);
		pTermTabs->selection_color(FL_CYAN);
		pTermTabs->end();
	}
	pTermWin->resizable(*pTermTabs);
	pTermWin->callback(close_callback);
	pTermWin->end();

	pCmdWin = new Fl_Double_Window(800, MARGIN, "Cmd");
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
					if ( strncmp(cmd, "New=", 4)==0 ) 
						term_new( cmd+4 );
					else if ( strncmp(cmd, "Tab=", 4)==0 ) 
						term_tab( cmd+4 ); 
					else if ( strncmp(cmd, "Cmd=", 4)==0 ) 
						replen = term_cmd( cmd+4, &reply );
#ifdef WIN32
					else if ( strncmp(cmd, "SHA", 3)==0 ) {
						reply = SHA(cmd+3);
						replen = strlen(reply);
					}
#endif	
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
void scp_writer(Fl_Term *pTerm, char *script)
{
	Fan_Host *host = pTerm->get_host();
	char *p, *p0, *p1, *p2;
	char lfile[1024], rfile[1024];
	int rpath_len;

	pTerm->command("pwd", &p2);
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
void sftp_copier(Fl_Term *pTerm, char *script)
{
	Fan_Host *host = pTerm->get_host();
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

void term_scripter(Fl_Term *pTerm, char *script)
{
	char *p0=script, *p1, *p2;	
	do {
		p2=p1=strchr(p0, 0x0a);
		if ( p1==NULL ) 
			p1 = p0+strlen(p0);
		else 
			*p1 = 0;
		if (p1!=p0) pTerm->command( p0, NULL );
		*p1 = 0x0a;
		p0 = p1+1; 
	}
	while ( p2!=NULL );
	delete script;
}
void dnd_cb(void *w, const char *txt)
{
	char *script = strdup(txt);	//script thread must delete this
	if ( script==NULL ) return;
	Fl_Term *pTerm = (Fl_Term *)w;
	Fan_Host *host = pTerm->get_host();

	if ( host->type()==HOST_CONF ) 
		host->write(txt, strlen(txt));
	else if ( host->type()==HOST_SFTP) {
		std::thread scripterThread(sftp_copier, pTerm, script);
		scripterThread.detach();
	}
	else if ( host->type()==HOST_SSH) {
		char *p0 = script;
		char *p1=strchr(p0, 0x0a);
		if ( p1!=NULL ) *p1=0;
		struct stat sb;
		int rc = stat(p0, &sb);		//is this a list of files?
		if ( p1!=NULL ) *p1=0x0a;
		if ( rc==-1 ) {		//first line is not a file, run as script
			std::thread scripterThread(term_scripter, pTerm, script);
			scripterThread.detach();
		}
		else {
			std::thread scripterThread(scp_writer, pTerm, script);
			scripterThread.detach();
		}
	}
}
int scp_read(Fl_Term *term, const char *rpath, const char *lpath)
{
	Fan_Host *host = term->get_host();
	if ( host->type()!=HOST_SSH ) return 0;
	sshHost *pHost = (sshHost *)host;

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
		if ( term->command(rnames, &rlist )>0 ) {
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
	host->write("\015", 1);
	return 0;
}
int scp_write(Fl_Term *term, const char *lpath, const char *rpath)
{
	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;

	Fan_Host *host = term->get_host();
	if ( host->type()!=HOST_SSH ) return 0;
	sshHost *pHost = (sshHost *)host;

	if ( stat(lpath, &statbuf)!=-1 ) {
		char rnames[1024]="ls -ld ", *rlist, rfile[1024];
		if ( *rpath!='/' ) strcat(rnames, "~/");
		strcat(rnames, rpath);
		strcpy(rfile, *rpath?rpath:".");
		if ( term->command(rnames, &rlist )>0 ) {
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
	host->write("\015", 1);
	return 0;
}
void scp(char *cmd)
{
	static char lpath[1024], rpath[1024];
	char *p = strchr(cmd, ' ');
	if ( p==NULL ) return;

	*p=0; 
	strncpy(rpath, cmd, 1023);
	strncpy(lpath, p+1, 1023);
	if ( *rpath==':' ) {
		std::thread scp_thread(scp_read, acTerm, rpath+1, lpath);
		scp_thread.detach();
	}
	else if ( *lpath==':') {
		std::thread scp_thread(scp_write, acTerm, rpath, lpath+1);
		scp_thread.detach();
	}
}