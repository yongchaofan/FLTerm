//
// "$Id: sqlTable.h 2529 2017-08-04 13:48:10 $"
//
// sqlTable -- A sql table widget based on FLTK Fl_Table widget
//             this widget allows direct manipulation of data
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
#include <FL/Fl_Input.H>
#include <FL/Fl_Table.H>
#include <vector>
#include <string>

class tkInput: public Fl_Input {
private:
	std::vector<char*> cmds;
	char keys[256];
	int id;
public:
	tkInput(int X,int Y,int W,int H,const char* L=0):Fl_Input(X,Y,W,H,L)
	{
		cmds.clear();
		cmds.push_back(strdup("192.168.1.1"));
		keys[0] = 0;
		id = 0;
	}
	void add( const char *cmd );
	int handle( int e );
};

#define MAXCOLS 		16
#define ROW_FONTSIZE	14
#define HEADER_FONTSIZE 16
#define ROW_FONTFACE	FL_HELVETICA
#define HEADER_FONTFACE FL_HELVETICA_BOLD
typedef std::vector<char*> Row;
class sqlTable : public Fl_Table {
private:
    int severityCol;
    int clearCol;
	int rowidCol;
    int refresh;			//true if select is needed at next redraw
	int sort[MAXCOLS];
	Row header;
	std::vector<Row> _rowdata;
	
	std::string copy_buf;
	std::string select_sql;
	std::string edit_sql;
	Fl_Input *edit_input;
	int edit_row, edit_col;
	
protected:
    void draw_cell(TableContext context, int R=0, int C=0, 
                   int X=0, int Y=0, int W=0, int H=0);
    void draw_sort_arrow(int X,int Y,int W,int H, int C);
    void col_dclick(int C);
	void cell_dclick(int R, int C);
	void cell_rclick(int R, int C);
	void start_edit(int R, int C);
	void done_edit();
    void insert_row(int R);
    void modify_row(int R);
    void delete_rows();
	void copy_rows();
	void paste_rows();
	
public:
    sqlTable(int x, int y, int w, int h, const char *l=0);
	~sqlTable(){ clear(); };
	int handle( int e );
	void draw();
    void clear();
    
    void insert(Row newrow);// insert a row into table and adjust column width
    void save(const char *fn);
	void add_col(const char *hdr);
    void set_refresh(int t) { refresh=t; }
    void select(const char *cmd) { select_sql = cmd; }
	const char *select() { return select_sql.c_str(); }
};
int sql_select(sqlTable *pTable, const char *sql);
