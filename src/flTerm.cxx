//
// "$Id: flTerm.cxx 7357 2017-08-04 13:48:10 $"
//
// flTerm -- A minimalist ssh terminal simulator
//
//    an example application using the Fl_Term widget.
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


const char ABOUT_TERM[]="\n\n\n\
	flTerm is a terminal simulator for network engineers,\n\n\
	a simple ssh and telnet client that features:\n\n\
	    * minimalist user interface\n\n\
	    * unlimited screen buffer size\n\n\
	    * Select to copy, right click to paste\n\n\
	    * Drag and Drop to run list of commands\n\n\
	    * Scripting interface at xmlhttp://127.0.0.1:%d\n\n\
		by yongchaofan@gmail.com	07-31-2017";
#include <ctype.h>
#include <unistd.h>
#include "ssh_Host.h"

#define TABHEIGHT 20
#include <FL/x.H>               // needed for fl_display
#include <FL/Fl.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Menu.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
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

Fl_Tabs *pTermTabs;
Fl_Window *pTermWin;

Fl_Term *act_term=NULL;
Fl_Term *term_new(const char *host)
{			
	Fl_Host *pHost;
	Fl_Term *pTerm=new Fl_Term(0, TABHEIGHT, pTermTabs->w(), pTermTabs->h()-TABHEIGHT, "");
	pTerm->color(FL_BLACK, FL_GRAY);
	if ( strncmp(host, "telnet ", 7)==0 ) {
		pTerm->copy_label(host+7);
		pHost = new tcpHost();
		pHost->set_term(pTerm);
		pHost->start(host+7);
	}
	else {
		pTerm->copy_label(host);
		pHost = new sshHost();
		pHost->set_term(pTerm);
		pHost->start(host);
	}
	pTermTabs->insert(*pTerm, pTermTabs->children()-1);
	pTermTabs->value(pTerm);
	pTerm->take_focus();
	Fl::awake((void *)pTermTabs);
	return act_term = pTerm;
}
void term_delete()
{
	if ( act_term==NULL ) return;
	act_term->get_host()->set_term(NULL);
	pTermTabs->remove(act_term);
	Fl_Term *pTerm = act_term;
	
	int n = pTermTabs->children()-1;
	act_term = n>0?(Fl_Term *)pTermTabs->child(0):NULL;
	pTermTabs->value( pTermTabs->child(0) );
	if ( act_term!=NULL ) act_term->take_focus();
		
	Fl::delete_widget(pTerm);	
	Fl::awake(pTermWin);
}
void term_tab(const char *host)
{
	for ( int i=0; i<pTermTabs->children(); i++ )
		if ( strcmp(host, pTermTabs->child(i)->label())==0 ) {
			pTermTabs->value(pTermTabs->child(i));
			act_term = (Fl_Term *)pTermTabs->value();
		}
}
int term_cli( const char *cmd, char **preply )
{
	return act_term!=NULL ? act_term->command(cmd, preply ) : 0;
}
void tab_callback(Fl_Widget *w) 
{
	Fl_Term *pTerm = (Fl_Term *)pTermTabs->value();
	if ( strcmp(pTerm->label(), " + ")==0 ) {
		const char *host = "192.168.1.1";
		if ( act_term!=NULL ) {
			pTermTabs->value(act_term);
			host = act_term->label();
		}
		const char *cmd = fl_input("[telnet] ip[:port]", host);
		if ( cmd!=NULL ) term_new(cmd);
		return;
	}
	act_term = pTerm;
	act_term->take_focus();
	if ( Fl::event_button() == FL_RIGHT_MOUSE ) {
		Fl_Menu_Item rclick_menu[] = { 
			{"Clear"}, {"Log"}, {"Save",}, {"Delete"}, {0} };
		const Fl_Menu_Item *m = rclick_menu->popup(Fl::event_x(),Fl::event_y(),0,0,0);
		if ( m ) {
			const char *sel = m->label();
			switch ( *sel ) {
			case 'C': act_term->clear(); break;
			case 'D': term_delete(); break;
			case 'L': act_term->logg(file_chooser("Log to file:", "Log\t*.log")); break;
			case 'S': act_term->save(file_chooser("Save to file:", "Text\t*.txt")); break;
			}
		}
	}
	
}
int main() {
	char buf[4096];
	int port = host_init();
	sprintf(buf, ABOUT_TERM, port);
	pTermWin = new Fl_Double_Window(1024, 640, "Terms");
	{
		pTermTabs = new Fl_Tabs(0, 0, pTermWin->w(),pTermWin->h());
		pTermTabs->selection_color(10);
		pTermTabs->when(FL_WHEN_RELEASE|FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED);
        pTermTabs->callback(tab_callback);
		Fl_Term *pPlus = new Fl_Term(0, TABHEIGHT, pTermTabs->w(), 
									pTermTabs->h()-TABHEIGHT, " + ");
		pPlus->color(FL_DARK3, FL_GRAY);
		pPlus->set_fontsize(20);
		pPlus->append(buf, strlen(buf));
		pTermTabs->end();
	}
	pTermWin->resizable(*pTermTabs);
	pTermWin->end();
	Fl::lock();
#ifdef WIN32
    pTermWin->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	pTermWin->show();

	while ( Fl::wait() ) {
		Fl_Widget *pt = (Fl_Widget *)Fl::thread_message();
		if ( pt!=NULL )pt->redraw();
	}
		
	host_exit();
	return 0;
}
int http_callback( char *buf, char **preply)
{
//fprintf(stderr, "\"%s\"\n", buf);
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
	if ( strncmp(buf, "SSH=", 4)==0 ) 	   term_new( buf+4 );
	else if ( strncmp(buf, "Tab=", 4)==0 ) term_tab( buf+4 ); 
	else if ( strncmp(buf, "Cli=", 4)==0 ) rc = term_cli( buf+4, preply );
	return rc;
}
/**********************************HTTPd**************************************/
const char HEADER[]="HTTP/1.1 %s\
					\nServer: flTable-httpd\
					\nAccess-Control-Allow-Origin: *\
					\nContent-Type: text/plain\
					\nContent-length: %d\
					\nConnection: Keep-Alive\
					\nCache-Control: no-cache\n\n";
void *httpd( void *pv )
{
	struct sockaddr_in cltaddr;
	socklen_t addrsize=sizeof(cltaddr);
	char buf[4096], *cmd, *reply=buf;
	int cmdlen, replen;

	int http_s0 = *(int *)pv;
	int http_s1 = accept( http_s0, (struct sockaddr*)&cltaddr, &addrsize );
	while ( http_s1!= -1 ) {
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
		http_s1 = accept( http_s0, (struct sockaddr*)&cltaddr, &addrsize );
	}
	closesocket(http_s1);
	return 0;
}
/****************************************************************************/
static int http_s0 = -1;
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
	short port = 8079;
	int rc = -1;
	while ( rc==-1 && port<9080 ) {
		svraddr.sin_port=htons(++port);
		rc = bind(http_s0, (struct sockaddr*)&svraddr, addrsize);
	}
	if ( rc!=-1 ) {
		if ( listen(http_s0, 1)!=-1){
			pthread_t pthReaderId;
			pthread_create( &pthReaderId, NULL, httpd, &http_s0 );
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
