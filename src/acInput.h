#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <string>
#include <vector>

#ifndef _AC_INPUT_H_
#define _AC_INPUT_H_

class acInput: public Fl_Input {
private:
	std::vector<char*> cmds;
	char keys[256];
	int id;
public:
	acInput(int X,int Y,int W,int H,const char* L=0):Fl_Input(X,Y,W,H,L)
	{
		cmds.clear();
		cmds.push_back(strdup("192.168.1.1"));
		keys[0] = 0;
		id = 0;
	}
	void add( const char *cmd );
	int handle( int e );
};
#endif  //acInput
