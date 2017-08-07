//
// "$Id: sqlTable.cxx 17937 2017-08-04 13:48:10 $"
//
// sqlTable -- A sql table widget based on FLTK Fl_Table
//             which allows direct manipulation of data
//             rows from database tables
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

#include <FL/Fl.H>
#include <FL/Fl_Ask.H>
#include <FL/Fl_Draw.H>
#include <FL/Fl_Menu.H>
#include "sqlTable.h"

int sql_exec( const char *sql, int t);
int table_select(sqlTable *pTable, const char *sql);
//void connect_node( const char *nodename, const char *address );
//void disconn_node( const char *nodename );
//void sync_node( const char *nodename );
//void getpm_node( const char *nodename, const char *resource );
//void login_node( const char *nodename, const char *user, const char *pass );
//void term_new( const char *address, const char *nodename );
//void console_new( const char *nodename );

void tkInput::add( const char *cmd ) {
	std::vector<char *>::iterator it;
	int i=cmds.size()-1;
	if ( strcmp(cmd, cmds[i])>0 ) 
		cmds.push_back(strdup(cmd));
	else
	for ( it=cmds.begin(),i=0; it<cmds.end(); it++, i++ ) {
		if ( strcmp(cmd, cmds[i])==0 ) break;
		if ( strcmp(cmd, cmds[i]) < 0 ) {
			cmds.insert(it, strdup(cmd));
			break;
		}
	}
}
int tkInput::handle( int e ) {
	int rc = Fl_Input::handle(e);
	int key = Fl::event_key();
	int len = position();
	if ( len>255 ) len=255;
	switch (e) {
		case FL_KEYDOWN: {
			switch (key) {
			case FL_Up: while( --id >=0 ) {
					if ( strncmp(keys, cmds[id], len)==0 || keys[0]==0 ) {
						value(cmds[id]);
						position(len, strlen(cmds[id]));
						break;
					}
				}
				if ( id<0 ) id++;
				return 1;
			case FL_Down: while( ++id < (int)cmds.size() ) {
					if ( strncmp(keys, cmds[id], len)==0 || keys[0]==0 ) {
						value(cmds[id]);
						position(len, strlen(cmds[id]));
						break;
					}
				}
				if ( id==(int)cmds.size() ) id--;
				return 1;
			}
		}
		case FL_KEYUP: if ( len==0  ) keys[0]=0;				
			else switch (key) {
			case FL_BackSpace:
			case FL_Delete: keys[len] = 0;
			case FL_Left:  
			case FL_Right: 
			case FL_Up: 
			case FL_Down:
			case FL_Enter:
			case FL_Shift_L:
			case FL_Shift_R:
			case FL_Control_L:
			case FL_Control_R: break;
			default: if ( len<size() ) break;
				{
					strncpy(keys, value(), 255);
					for ( id=0; id<(int)cmds.size(); id++ ) {
						if ( strncmp(keys, cmds[id], len)==0 ){
							value(cmds[id]);
							position(len, strlen(cmds[id]));
							break;
						}
					}
				}
			}
		case FL_DND_ENTER: 
		case FL_DND_DRAG:  
		case FL_DND_RELEASE:
		case FL_DND_LEAVE: return 1;
		case FL_PASTE:	value(Fl::event_text()); return 1;	
	}
	return rc;
}
sqlTable::sqlTable(int x,int y,int w,int h,const char *l):Fl_Table(x,y,w,h,l) 
{
	clear();
    refresh = true;
	select_sql = "select * from ";
	select_sql = select_sql + l + " limit 3000";
	for ( int i=0; i<MAXCOLS; i++ ) sort[i]=0;
	col_header(1);
	col_resize(1);
	col_header_height(24);
	row_header(1);
	row_resize(1);
	row_height_all(16);			// height of all rows
	row_resize_min(16);
    labelfont(HEADER_FONTFACE);
    labelsize(HEADER_FONTSIZE);
    
    edit_input = NULL;
	edit_row = edit_col = -1;
}
void sqlTable::draw_sort_arrow(int X,int Y,int W,int H, int C) 
{
    int xlft = X+(W-6)-8;
    int xctr = X+(W-6)-4;
    int xrit = X+(W-6)-0;
    int ytop = Y+(H/2)-4;
    int ybot = Y+(H/2)+4;
    if ( sort[C]>0 ) {              // Engraved down arrow
        fl_color(FL_WHITE);
        fl_line(xrit, ytop, xctr, ybot);
        fl_color(41);                   // dark gray
        fl_line(xlft, ytop, xrit, ytop);
        fl_line(xlft, ytop, xctr, ybot);
    } 
    else {                              // Engraved up arrow
        fl_color(FL_WHITE);
        fl_line(xrit, ybot, xctr, ytop);
        fl_line(xrit, ybot, xlft, ybot);
        fl_color(41);                   // dark gray
        fl_line(xlft, ybot, xctr, ytop);
    }
}

// Handle drawing all cells in table
const Fl_Color cell_color[] = { FL_WHITE, FL_DARK1, fl_lighter(FL_BLUE), FL_YELLOW, FL_RED };
void sqlTable::draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) 
{
    switch ( context ) {
        case CONTEXT_COL_HEADER: {
            fl_push_clip(X,Y,W,H); 
            {
                fl_draw_box(FL_THIN_UP_BOX, X,Y,W,H, color());
                fl_font(HEADER_FONTFACE, HEADER_FONTSIZE);
                fl_color(FL_BLACK);
                fl_draw(header[C], X+4,Y,W,H, FL_ALIGN_LEFT|FL_ALIGN_BOTTOM, 0, 0);
                if ( sort[C]!=0 ) draw_sort_arrow(X,Y,W,H,C);
            }
			fl_pop_clip();
            break; 
        }
        case CONTEXT_ROW_HEADER: {
            fl_push_clip(X,Y,W,H); 
			{
                fl_draw_box(FL_THIN_UP_BOX, X,Y,W,H, color());
                fl_font(ROW_FONTFACE, ROW_FONTSIZE);
                fl_color(FL_BLACK);
				char hdr[8];
				sprintf(hdr, "% 4d",R+1);
                fl_draw(hdr, X,Y,W,H, FL_ALIGN_LEFT, 0, 0); 
            }
            fl_pop_clip();
            break; 
        }
        case CONTEXT_CELL: {
			if ( R >= (int)_rowdata.size() ) break;
			if ( C >= (int)_rowdata[R].size() ) break;
			if ( R==edit_row && C==edit_col ) break;
            int icolor=0;
    		const char *s = _rowdata[R][C]; 
            if ( C==severityCol && clearCol!=-1 ) {
                if ( *(_rowdata[R][clearCol])==0 ) {
                    if ( strcmp(s, "critical")==0 ) icolor=4;
                    else if ( strcmp(s, "major")==0 ) icolor=3;
                    else if ( strcmp(s, "minor")==0 ) icolor=2;
                    else if ( strcmp(s, "warning")==0 ) icolor=1;
                } 
			}
            fl_push_clip(X,Y,W,H); 
			{
                fl_color(is_selected(R,C) ? FL_CYAN : cell_color[icolor]);
                fl_rectf(X,Y,W,H); 
                fl_font(ROW_FONTFACE, ROW_FONTSIZE);
                fl_color(FL_BLACK); 
				fl_draw(s, X+4,Y+4,W,H, FL_ALIGN_LEFT|FL_ALIGN_TOP);
                fl_color(FL_LIGHT2); fl_rect(X,Y,W,H);
            }
            fl_pop_clip();
            break;
        }
        default: break;
    }
}
void sqlTable::draw()
{ 
	if ( refresh ) {
		refresh = false;
		int row = top_row();
		clear();
		table_select(this, select_sql.c_str());
		top_row( row );
	}
	Fl_Table::draw();
}
int sqlTable::handle( int e )
{
	switch( e ) {
		case FL_RELEASE: {
			int R = callback_row();
			int C = callback_col();					
			switch ( callback_context() ) {
			case CONTEXT_CELL :
				if ( Fl::event_clicks() ) {
					cell_dclick(R, C); return 1;
				}
				if ( Fl::event_button()==3 ) {
					cell_rclick(R, C); return 1;
				}
				break;
			case CONTEXT_COL_HEADER:
				if ( Fl::event_clicks() ) {
					col_dclick(C); return 1;
				}
			default:break;
			}
			break;
		}
		case FL_KEYBOARD: if ( edit_input==NULL ) break;
						  if ( !edit_input->visible() ) break;
			switch ( Fl::event_key() ) {
			case FL_Tab: 
				done_edit();
				if ( ++edit_col==rowidCol ) edit_col=0; 
				start_edit(edit_row, edit_col);
				redraw();
				return 1;
			case FL_Enter:
				done_edit();
				if ( edit_sql[0]=='i' ) {
					for ( int i=0; i<cols()&&i!=rowidCol; i++ )
						edit_sql = edit_sql + _rowdata[edit_row][i] + "\",\""; 
					edit_sql = edit_sql.replace(edit_sql.size()-3,3,"\")");
				}
				else {
					edit_sql.replace(edit_sql.size()-1,1, " ");
					edit_sql = edit_sql + "where rowid='" + 
									_rowdata[edit_row][rowidCol] + "'";
				}
				sql_exec(edit_sql.c_str(), 0);
			case FL_Escape :
				edit_row = -1;
				edit_col = -1;
				edit_input->hide();
				remove(*edit_input);
				refresh = true;
				redraw();
				return 1;
			}
			break;	
		case FL_PASTE: paste_rows();
		case FL_DND_RELEASE:
		case FL_DND_DRAG: 
		case FL_DND_ENTER: 
		case FL_DND_LEAVE: return 1;
	}
	return(Fl_Table::handle(e));
}
void sqlTable::start_edit(int R, int C)
{
	if ( edit_input == NULL ) {
		edit_input = new Fl_Input(0,0,0,0);
		edit_input->hide();
		edit_input->maximum_size(128);
		edit_input->color(FL_YELLOW);
		end();
	}	
	add(*edit_input);
	
	edit_row = R;
	edit_col = C;
	int X,Y,W,H;
	find_cell( CONTEXT_CELL, edit_row, edit_col, X,Y,W,H );
	edit_input->resize(X,Y,W,H);
	edit_input->value(_rowdata[edit_row][edit_col]);
	edit_input->show();
	edit_input->take_focus();
	edit_input->clear_changed();
}
void sqlTable::done_edit()
{
	if ( edit_input->changed() ) {
		free(_rowdata[edit_row][edit_col]);
		_rowdata[edit_row][edit_col]=strdup(edit_input->value());
		if ( edit_sql[0]=='u' ) {
			edit_sql = edit_sql + header[edit_col] + "=\"";
			edit_sql = edit_sql + edit_input->value() + "\",";
		}
	}
}
void sqlTable::insert_row(int r)
{
	Row newrow;
	newrow.clear();
	for ( int i=0; i<cols(); i++ ) 
		newrow.push_back(strdup(_rowdata[r][i]));
	std::vector<Row>::iterator it = _rowdata.begin();
    _rowdata.insert(it, newrow);
    rows((int)_rowdata.size());   // How many rows we loaded
	refresh = false;
	redraw();

	start_edit( 0, 0 );
	edit_sql = "insert into ";
	edit_sql = edit_sql + label() + " ('";
	for ( int i=0; i<cols()&&i!=rowidCol; i++ ) 
		edit_sql = edit_sql + header[i] + "','"; 
	edit_sql = edit_sql.replace(edit_sql.size()-3, 3, "') values (\"");
}
void sqlTable::modify_row(int r)
{
	start_edit( r, callback_col() );
	edit_sql = "update ";
	edit_sql = edit_sql + label() + " set ";
}
void sqlTable::delete_rows()
{
	int row_top, col_left, row_bot, col_right;
	get_selection(row_top, col_left, row_bot, col_right);
	for ( int R=row_top; R<=row_bot; R++ ) {
		edit_sql = "delete from ";
		edit_sql = edit_sql + label() + " where ";
		for ( int i=col_left; i<=col_right; i++ )
			edit_sql = edit_sql + header[i]+"=\""+_rowdata[R][i]+"\" and ";
		edit_sql = edit_sql.replace(edit_sql.size()-5,5,"");
		sql_exec(edit_sql.c_str(), 0);
	}
	refresh = true;
	redraw();
}
void sqlTable::copy_rows()
{
	int row_top, col_left, row_bot, col_right;
	get_selection(row_top, col_left, row_bot, col_right);
	if ( col_right==rowidCol ) col_right--;
	
	copy_buf = "";
	for ( int C=col_left; C<col_right; C++ ) 
		copy_buf = copy_buf + header[C] + "\t";
	copy_buf = copy_buf + header[col_right] + "\n";
	for ( int R=row_top; R<=row_bot; R++ ) {
		for ( int C=col_left; C<col_right; C++ )
			copy_buf = copy_buf + _rowdata[R][C] + "\t";
		copy_buf = copy_buf + _rowdata[R][col_right] + "\n";
	}
	Fl::copy(copy_buf.c_str(), copy_buf.size(), 1);
}
void sqlTable::paste_rows()
{
	char txt[1024];
	const char *p1 = strchr(Fl::event_text(), 0x0a);
	if ( p1==NULL ) return;
	const char *p0 = Fl::event_text();
	int l = p1-p0;
	strncpy( txt, p0, l); txt[l]=0;
	for ( char *p2=txt; *p2; p2++ ) if ( *p2=='\t' ) *p2=',';
	edit_sql = "insert or replace into ";
	edit_sql = edit_sql + label() + " (";
	edit_sql = edit_sql + txt + ") values (\"";
	int header_len = edit_sql.size();
	
	while ( p1!=NULL ) {
		p0 += l+1; 
		p1 = strchr(p0, 0x0a); 
		if ( p1!=NULL ) 
			l = p1-p0;
		else
			l = strlen(p0);
		strncpy(txt, p0, l); txt[l]=0; 
		edit_sql = edit_sql + txt + "\")";
		int i = edit_sql.find("\t");
		while ( i!=-1 ){
			edit_sql.replace(i,1,"\",\"");
			i = edit_sql.find("\t");
		}
		sql_exec( edit_sql.c_str(), 0 );
		edit_sql.erase( header_len );
	}	
	refresh = true;
	redraw();
}
void sqlTable::col_dclick(int COL) 	//dclick on col header to change sorting
{
	if ( sort[COL]==0 ) {			//no sort, change to ASC 
		int sort_order=1;
		for ( int i=0; i<MAXCOLS; i++ ) {
			if ( sort[i]==sort_order ) sort_order = 1+sort[i];
			if ( sort[i]==-sort_order ) sort_order = 1-sort[i];
		}
		sort[COL]=sort_order;
	}
    else if ( sort[COL]>0 ) {		//ASC sort, change to DESC
		sort[COL]=-sort[COL];
	}
	else if ( sort[COL]<0 ) {		//DESC sort, change to no sort
		int sort_order=-sort[COL];
        for ( int i=0; i<MAXCOLS; i++ ) {
			if ( sort[i]>sort_order ) sort[i]-=1;
			if ( sort[i]<-sort_order ) sort[i]+=1;
		}
		sort[COL]=0;
    }

	int i = select_sql.find(" order by");
	if ( i==-1 ) i = select_sql.find(" limit");
	if ( i!=-1 ) select_sql.erase(i);
	for ( int k=1; k<7; k++ ) {
		for ( int j=0; j<MAXCOLS; j++ ) {
			if ( sort[j]==k || sort[j]==-k ) {
				select_sql = select_sql + ((k==1)?" order by ":", ");
				select_sql = select_sql + header[j];
				if ( sort[j]<0 ) select_sql = select_sql + " DESC";
			}
		}
	}
	select_sql = select_sql + " limit 3000";
	refresh = true;
	redraw();
}
void sqlTable::cell_dclick(int ROW, int COL) 	//double click on cell to filter
{
	const char *cell = _rowdata[ROW][COL];
/*	if ( COL==0	&& strcmp(header[1], "address")==0 ) 
		console_new(cell);
	else if ( strcmp(header[COL], "address")==0 ) 	 
		term_new(cell, _rowdata[ROW][0]);
	else if ( strcmp(header[COL], "pm_data")==0 ) 
		getpm_node( _rowdata[ROW][0], _rowdata[ROW][1]);
	else if ( strcmp(header[COL], "pm_src")==0 
			||strcmp(header[COL], "pm_dst")==0 ) 
		getpm_node( _rowdata[ROW][0], _rowdata[ROW][COL-1]);
	else */ 
	{
		std::string order_by = "";
		int i = select_sql.find(" order by");
		if ( i==-1 ) i = select_sql.find(" limit");
		if ( i!=-1 ) {
			order_by = select_sql.substr(i);
			select_sql.erase(i);
		}
		
		std::string filter = " ";
		filter = filter + header[COL] + "='" + _rowdata[ROW][COL] + "'";
		int i2 = select_sql.find(filter + " and");
		int i1 = select_sql.find(" and" + filter);
		int i0 = select_sql.find(" where" + filter);
		if ( i2!=-1) select_sql.erase(i2, filter.size()+4);
		else if ( i1!=-1 ) select_sql.erase(i1, filter.size()+4);
		else if ( i0!=-1 ) select_sql.erase(i0, filter.size()+6);
		else {
			if ( select_sql.find(" where ")!=std::string::npos ) 	
				select_sql = select_sql + " and" + filter;
			else
				select_sql = select_sql + " where" + filter;
		}
		select_sql += order_by;
		refresh = true; 
		redraw();
	}
}
void sqlTable::cell_rclick(int ROW, int COL)	//cell right click menu
{
	if ( !is_selected(ROW, COL) ) set_selection(ROW, 0, ROW, cols()-1);
	const Fl_Menu_Item *m;
/*
	if ( strcmp(header[COL], "nodename")==0 	//nodename right click 
		  && strcmp(header[1],"address")==0 ) { //on nodes table 
		Fl_Menu_Item rclick_menu[] = { {"Connect"},{"Login"}, 
									   {"Resync"},{"Disconnect"}, {0} };
		m = rclick_menu->popup(Fl::event_x(),Fl::event_y(),0,0,0);
		if ( m ) {
			int row_top, col_left, row_bot, col_right;
			get_selection(row_top, col_left, row_bot, col_right);
			char username[32];
			const char *user, *pass, *sel = m->label();
			if ( *sel=='L' ) {
				user = fl_input("username:", "");
				if ( user!=NULL ) strncpy(username, user, 31);
				pass = fl_password("password:" "");
				if ( user==NULL || pass==NULL ) return;
			}
			for ( int R=row_top; R<=row_bot; R++ ) {
				const char *nodename = _rowdata[R][0];
				const char *address = _rowdata[R][1];
				switch ( *sel ) {
				case 'C': connect_node( nodename, address); break;
				case 'L': login_node(nodename, username, pass); break;
				case 'R': sync_node(nodename); break;
				case 'D': disconn_node(nodename); break;
				}
			}					  
		}
	}
	else*/ 
	{
		Fl_Menu_Item rclick_menu[]={{"Copy"},{"Paste"},{"Insert"},{"Delete"},{0}};
		if ( rowidCol!=-1 ) rclick_menu->add("Modify", 0, NULL);
		m = rclick_menu->popup(Fl::event_x(),Fl::event_y(),0,0,0);
		if ( m ) {
			const char *sel = m->label();
			switch ( *sel ) {
			case 'C': copy_rows(); break;
			case 'P': Fl::paste( *this, 1 ); break;
			case 'I': insert_row(ROW); break;
			case 'M': modify_row(ROW); break;
			case 'D': delete_rows(); break;
			}
		}
	}
}
void sqlTable::save( const char *fn )
{
	FILE *fp = fopen( fn, "w+" );
	if ( fp!=NULL  ){
		for ( int i=0; i<(int)_rowdata[0].size()-1; i++ )
			fprintf( fp, "%s, ", header[i] );
		for ( int i=0; i<(int)_rowdata.size(); i++ ) {
			fprintf( fp, "\n" );
			for ( int j=0; j<(int)_rowdata[i].size()-1; j++ ) 
				fprintf( fp, "%s, ", _rowdata[i][j] );
		}
		fclose(fp);
	}
}
void sqlTable::add_col( const char *hdr )
{
	header.push_back(strdup(hdr));
	int i = header.size()-1;
    if ( strncmp(hdr, "rowid",   5)==0 ) rowidCol = i;
	if ( strncmp(hdr, "cleared", 7)==0 ) clearCol = i;
    if ( strncmp(hdr,"severity", 8)==0 ) severityCol = i;
    {// Initialize column width to header width
		int w=0, h=0;
		fl_font(HEADER_FONTFACE, HEADER_FONTSIZE);
		fl_measure(hdr, w, h, 0);  // pixel width of header text
		col_width(i, w+24);
	}
}
void sqlTable::insert( Row newrow )// insert a row, called by select()
{
    std::vector<Row>::iterator it = _rowdata.begin();
    _rowdata.insert(it, newrow);
    rows((int)_rowdata.size());   // How many rows we loaded
// Automatically set column widths to widest data in each column
    fl_font(ROW_FONTFACE, ROW_FONTSIZE);
    for ( int c=0; c<(int)newrow.size()&&c!=rowidCol; c++ ) {
		int w=0, h=0;
    	fl_measure(newrow[c], w, h, 0); // pixel width of row text
		w += 24;
		if ( w>600 ) w=600;
        if ( w > col_width(c)) col_width(c, w);
    }
	table_resized();
}
void sqlTable::clear()
{
    severityCol = clearCol = rowidCol = -1;
	for ( int i=0; i<(int)_rowdata.size(); i++ ) {
		for ( int j=0; j<(int)_rowdata[i].size(); j++ ) 
			free( _rowdata[i][j] );
		_rowdata[i].clear();
	}
    _rowdata.clear();
	for ( int i=0; i<(int)header.size(); i++ ) 
		free( header[i] );
	header.clear(); 
    cols(0); rows(0);
}
