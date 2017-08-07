//
// "$Id: sqliteTable 3988 2017-08-04 13:48:10 $"
//
// sqliteTable -- A sqlite3 db manipulation utility using 
//                the sqlTable widget based on FLTK
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

const char *ABOUT_TABLE="\n\n\n\
	sqliteTable is a sqlite3 database manipulation utility, data in\n\n\
	sqlite3 databases are presented using FLTK table widget, data \n\n\
	rows can be inserted, deleted, selected or updated by \n\n\
	    1. select rows or cells then use context menu\n\n\
	    2. type sql command in command input at bottom of window\n\n\n\
		by yongchaofan@gmail.com	07-31-2017";
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vector>
#include <sqlite3.h>
#include "sqlTable.h"
#include "Fl_Term.h"

#define TABHEIGHT  24
#define CMDHEIGHT  20
#include <FL/x.H>               // needed for fl_display
#include <FL/Fl.H>
#include <FL/Fl_Ask.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Menu.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
static Fl_Native_File_Chooser fnfc;
const char *file_chooser(const char *title, const char *filter, 
					int type=Fl_Native_File_Chooser::BROWSE_SAVE_FILE)
{
	fnfc.type(type);
	fnfc.title(title);
	fnfc.filter(filter);
	fnfc.directory(".");				// default directory to use
	switch ( fnfc.show() ) {			// Show native chooser
		case -1:  			 			// ERROR
		case  1: return NULL;  			// CANCEL
		default: return fnfc.filename();// FILE CHOSEN
	}
}

Fl_Tabs *pTableTabs;
Fl_Window *pTableWin;

sqlite3 *db=NULL;
const Fl_Color tbl_colors[8]={ 
	fl_rgb_color(192,192,192), fl_rgb_color(192,160,160), 
	fl_rgb_color(160,192,160), fl_rgb_color(160,160,192),
	fl_rgb_color(160,192,192), fl_rgb_color(192,160,192), 
	fl_rgb_color(192,192,160), fl_rgb_color(160,160,160) 
};
sqlTable *table_new()
{
	const char *name = fl_input("table name:", "newtable");
	if ( name==NULL ) return NULL;
	sqlTable *pTable= new sqlTable(0, TABHEIGHT, pTableWin->w(), 
						pTableWin->h()-TABHEIGHT-CMDHEIGHT, name);
	pTable->color(tbl_colors[pTableTabs->children()%8]);
	pTable->copy_label(name);
	pTable->end();
	pTableTabs->insert(*pTable, pTableTabs->children()-1);
	pTableTabs->value(pTable);
	pTableTabs->resizable(pTable);
	pTable->take_focus();
	Fl::awake((void *)pTableTabs);
	return pTable;
}

tkInput *pCmd;
void cmd_disp(const char *buf)
{
	pCmd->value(buf); 
	pCmd->position(0, strlen(buf));
	Fl::awake(pCmd);
}
void cmd_callback(Fl_Widget *o) {
	char buf[1024];
	strncpy(buf, pCmd->value(), 1023);
	pCmd->add( buf );
	pCmd->position(0, strlen(buf));
	sqlTable *pTable = (sqlTable *)pTableTabs->value();
	if ( strncmp(buf, "select", 6)==0 ) {
	    pTable->select(buf);
		pTable->set_refresh(true);
		pTable->redraw();
	}
	else {
		char *zErrMsg = 0;
		int rc = sqlite3_exec(db, buf, NULL, NULL, &zErrMsg);
		if( rc==SQLITE_OK ) 
			pTable->set_refresh(true);
		else {
			fl_alert("%s", zErrMsg); 
			sqlite3_free(zErrMsg);
		}
	}

}
void db_open(const char *fn)
{
	if ( db!=NULL ) sqlite3_close(db); 
	if ( sqlite3_open( fn, &db)==SQLITE_OK ) {
		char label[128];
		sprintf(label, "Table      %s", fn);
		pTableWin->copy_label(label);
	}
	else {
		fl_alert("%s", sqlite3_errmsg(db));
	}
}
void db_save(const char *fn)
{
	sqlite3 *bdb; 
	sqlite3_backup *pBackup;
	if( sqlite3_open(fn, &bdb)==SQLITE_OK ) {
		pBackup = sqlite3_backup_init(bdb, "main", db, "main");
		if( pBackup ) {
			sqlite3_backup_step(pBackup, -1);
			fl_message("%d pages backed up",
					sqlite3_backup_pagecount(pBackup));
			sqlite3_backup_finish(pBackup);
		}
		else {
			fl_alert("%s", sqlite3_errmsg(bdb));
		}
	}
	sqlite3_close(bdb);	
}
void table_tab_callback(Fl_Widget *w) {
	w = (Fl_Widget *)pTableTabs->value();
	pTableTabs->selection_color( w->color() );	
	sqlTable * pTable = (sqlTable *)w;
	if ( Fl::event_button() == FL_LEFT_MOUSE ) {
		if ( strcmp(w->label(), "+")!=0 ) {
			cmd_disp( pTable->select() );
			pTable->set_refresh(true);
		}
		else {
			table_new();
		}
	}
	if ( Fl::event_button() == FL_RIGHT_MOUSE ) {
		const Fl_Menu_Item *m;
		if ( strcmp(w->label(), "+")==0 ) {
			Fl_Menu_Item rclick_menu[] = {{"Open DB"},{"Backup DB"},{0}};
			m = rclick_menu->popup(Fl::event_x(),Fl::event_y(),0,0,0);
		}
		else {
			Fl_Menu_Item rclick_menu[] = {{"Save to csv"},{"Delete table"},{0}};
			m = rclick_menu->popup(Fl::event_x(),Fl::event_y(),0,0,0);
		}
		if ( m ) {
			const char *sel = m->label();
			if ( strcmp(sel, "Open DB")==0 ) {
				const char *fn = file_chooser("Open database", "Database\t*.db",
										Fl_Native_File_Chooser::BROWSE_FILE);
				if ( fn!=NULL ) db_open(fn);
			}
			else if ( strcmp(sel, "Backup DB")==0 ) {
				const char *fn = file_chooser("backup DB to", 
												"Database\t*.db" );
				if ( fn!=NULL ) db_save(fn);
			}
			else if ( strcmp(sel, "Save to csv")==0 ) {
				const char *fn = file_chooser("Save table to", 
										"Spreadsheet\t*.csv" );
				if ( fn!=NULL )	pTable->save(fn); 
			}
			else if ( strcmp(sel, "Delete table")==0 ) {
				pTableTabs->remove(pTable);
				pTableTabs->value( pTableTabs->child(0) );
				Fl::delete_widget(pTable);
				Fl::awake(pTableWin);
			}
		}
	}
}
void table_refresh( void *pv )
{
	sqlTable *pTable = (sqlTable *)pTableTabs->value();
	pTable->redraw();
	Fl::repeat_timeout(1, table_refresh);
}
int main(int argc, char **argv) 
{
	Fl::lock();
	pTableWin = new Fl_Double_Window(1024, 640);
	{
		int win_w = pTableWin->w(), win_h = pTableWin->h();
		pTableTabs = new Fl_Tabs(0, 0, win_w, win_h-CMDHEIGHT);
		{
			Fl_Term *pPlus = new Fl_Term(0, TABHEIGHT, pTableTabs->w(), 
										pTableTabs->h()-TABHEIGHT, "+");
			pPlus->color(FL_DARK3, FL_GRAY);
			pPlus->set_fontsize(20);
			pPlus->append(ABOUT_TABLE, strlen(ABOUT_TABLE));
		}
		pTableTabs->selection_color(tbl_colors[0]);
		pTableTabs->when(FL_WHEN_RELEASE|FL_WHEN_CHANGED|FL_WHEN_NOT_CHANGED);
		pTableTabs->callback(table_tab_callback);
		pTableTabs->end();
	  	pCmd = new tkInput(0, win_h-CMDHEIGHT, win_w, CMDHEIGHT);
	  	pCmd->color(FL_GREEN);
	  	pCmd->when(FL_WHEN_ENTER_KEY_ALWAYS);
	  	pCmd->callback(cmd_callback);
	}
	pTableWin->end();
	pTableWin->resizable(*pTableTabs);
#ifdef WIN32
    pTableWin->icon((char*)LoadIcon(fl_display, MAKEINTRESOURCE(128)));
#endif
	pTableWin->show();
	pTableWin->focus(pCmd);
	db_open( argc>1?argv[1]:"flTable.db" );

	Fl::add_timeout(1, table_refresh);
	while ( Fl::wait() ) {
		Fl_Widget *w = (Fl_Widget *)Fl::thread_message();
		if ( w!=NULL ) w->redraw();
	}

	sqlite3_close(db); 
	return 0;
}
int sql_exec( const char *sql, int t)
{
	if ( sqlite3_exec(db, sql, NULL, NULL, NULL)==SQLITE_OK ) {
		return true;
	}

	fprintf(stderr, "%s\n", sql);
	return false;
}
int table_select(sqlTable *pTable, const char *sql)
{ 
	cmd_disp(sql);

	sqlite3_stmt *res;
	if( sqlite3_prepare_v2(db, sql, 1024, &res, NULL)!=SQLITE_OK ) {
		pCmd->insert(sqlite3_errmsg(db)); 
		return false;
	}

	int c = sqlite3_column_count(res);
	int rc = sqlite3_step(res);
	if ( rc==SQLITE_ROW ) {
		for ( int i=0; i<c; i++ )
			pTable->add_col(sqlite3_column_name(res, i));
		pTable->cols(c);
	}
	while ( rc==SQLITE_ROW ) {
		Row newrow;
		newrow.clear();
		for ( int i=0; i<c; i++ ) {
			const char *p = (const char *)sqlite3_column_text(res, i);
			newrow.push_back(strdup(p==NULL?"":p));
		}
		pTable->insert(newrow);
		rc = sqlite3_step(res);
	}
	sqlite3_finalize(res);

	return true;
}