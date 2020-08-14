## Introduction

tinyTerm2 is a simple small and scriptable terminal emulator, it is a rewrite of [tinyTerm](https://yongchaofan.github.io/tinyTerm) using C++, [FLTK](http://fltk.org), and [libssh2](http://libssh2.org). Which is now cross platform with the same unique features and still very small, at release 1.2.6, the win64 executable is 824KB. 

Stable release: |[1.2.6](https://github.com/yongchaofan/tinyterm2) | License: | [GPL 3.0](https://github.com/yongchaofan/tinyTerm2/LICENSE)
--- |:---:|:---:|:---:
**PC install:** | [Microsoft Store](https://www.microsoft.com/store/apps/9NXGN9LJTL05) | [32bit EXE](https://github.com/yongchaofan/tinyTerm2/releases/download/1.2.6/tinyTerm2.exe) | [64bit EXE](https://github.com/yongchaofan/tinyTerm2/releases/download/1.2.6/tinyTerm2_x64.exe)
**Mac install:** | Apple store | [tinyTerm2.pkg](https://github.com/yongchaofan/tinyTerm2/releases/download/1.2.6/tinyTerm2.pkg)

<video width="960" height="540" controls>
<source src="tinyTerm2.mp4" type="video/mp4">
Your browser does not support the video tag.
</video>

---

## Documentation

> ### Making connections
> A connection dialog will popup at the start of tinyTerm2, simply choose the protocol, type in port and ip address/hostname then press enter to make a connection. Each time a connection is make using dialog, an entry will be added to the Term menu, simply select the menu entry to make the same connection again. Six types of connections are supported: shell, serial, telnet, ssh, sftp and netconf, except that sandboxed Apple store app will block shell execution.
>
> Once a connection is made, any key press will be transmitted to remote host, with response from host displayed in the terminal, most vt100/xterm escape sequences are supported, enough to work normally for top, vi, vttest etc. 
> 
> Press left mouse button and drag to select text, double click to select the whole word under mouse pointer, selected text will be copied to clipboard when mouse is moved out of the terminal window. When text is selected, right click will paste selected text into the same terminal, when no text is selected, right click will paste from clipboard.
>
> Scroll back buffer holds 64K lines of text, scrollbar is hidden by default, which will appear when scrolled back, use page up/page down key or mouse wheel to scroll. The buffer can be saved to a text file at any time, or turn logging function to write all terminal output to a text file. 
>
> ### Command history and autocompletion
> 
> Local Edit Mode was a feature on physical terminals used to save time on slow connections. On tinyTerm2 local edit mode is used to implement command history and command auto-completion, which is useful to help users remember long commands and save time on typing.
>
> When local edit mode is enabled, key presses are not sent to remote host until "Enter", "Tab" or "?" key is pressed, and the input is auto completed using command history, user can also press "up" or "down" key to bring up the command history list, then select command from the list to send to host. Every command typed in local edit mode is added to command history, which saved to tinyTerm.hist at exit, then loaded into memory at the next start of tinyTerm. 
>  
> Additionally when disconnected with local edit mode enabled, user is presented with a "tinyTerm >" prompt, simply type commands like "telnet 192.168.1.1" or "ssh admin@172.16.1.1" to make conection
> 
> ### Transfer files to remote host
>
> Drag and drop files to the terminal window will get the files transfered to remote host, placed in the current working directory
>
	on ssh connection files are copied via scp
	on sftp connection files are copied via sftp put
	on netconf connection the files content will be sent as xml
	on serial connection first file will be sent using xmodem protocol
    
>only send function of the original xmodem protocol is supported, CRC optional. This is added to support bootstraping of MCUs on embeded system like Ardiuno
> 
> ### Task Automation with batch commands
>
> To automate tasks, simply drag and drop a list of commands to the terminal, tinyTerm send one command at a time, wait for prompt string before sending the next command, to avoid overflowing the receive buffer of the remote host or network device. 
> 
> Most command line interface system uses a prompt string to tell user it’s ready for the next command, for example "> " or "$ " used by Cisco routers. tinyTerm will auto detect the prompt string used by remote host when a batch is dropped. Additionally, prompt string can be set in the script using command "!Prompt {str}", refer to appendix A for details and other special commands supported for scripting.
>
> A few local command like "Wait", "Clear", "Log" etc to enance the automation, the example below automate the task of connecting to a host, list some files, run top and log everthing to a file
>
	!Prompt $%20
	!Log top.log
	!ssh pi@192.168.12.8
	ls -l
	!Wait 3
	top -b -n 3
	!Log
	exit	

## Scripting interface
> More complex automation is facilited through the xmlhttp interface, a built in HTTPd listens at 127.0.0.1:8080, and will accept GET request from local machine, which means any program running on the same machine, be it a browser or a javascript or any program that supports xmlhttp interface, can connect to tinyTerm and request either a file or the result of a command, 

for example:

	http://127.0.0.1:8080/tinyTerm.html	return tinyTerm.html from current working folder
	http://127.0.0.1:8080/?ls -al		return the result of "ls -al" from remote host
	http://127.0.0.1:8080/?!Selection 	return current selected text from scroll back buffer
	
Notice the "!" just before "Selection" in the last example, when a command is started with "!", it's being executed by tinyTerm instead of sent to remote host, There are about 30 tinyTerm commands supported for the purpose of making connections, setting options, sending commands, scp files, turning up ssh2 tunnels, see appendix for the list.

The snippet below shows how to call the xmlhttp interfaces from javascript. An example in github/tinyTerm2/scripts, xmlhttp_get.html, demostrates a simple webpage, which takes a command from input field, send it through tinyTerm2, and present the result in browser

```js
var xhr = new XMLHttpRequest();
function xmlhttp(cmd) {
	xhr.open('GET', "/?"+cmd, false);
	xhr.send(null);
	return xhr.responseText;
}
```
> 
The following commands can be used programatically for scripting

    !/bin/bash          start local shell, on Windows try "ping 192.168.1.1"
    !com3:9600,n,8,1    connect to serial port com3 with settings 9600,n,8,1
    !telnet 192.168.1.1 telnet to 192.168.1.1
    !ssh pi@piZero:2222 ssh to host piZero port 2222 with username pi
    !sftp -P 2222 jun01 sftp to host jun01 port 2222
    !netconf rtr1       netconf to port 830(default) of host rtr1
    !disconn            disconnect from current connection
    !Tab                open a new tab on tinyTerm2

    !Clear              set clear scroll back buffer
    !Prompt $%20        set command prompt to “$ “, for CLI script
    !Timeout 30	        set time out to 30 seconds for CLI script
    !Wait 10            wait 10 seconds during execution of CLI script
    !Waitfor 100%       wait for “100%” from host during execution of CLI script
    !Log test.log       start/stop logging with log file test.log

    !Disp test case #1  display “test case #1” in terminal window
    !Send exit          send “exit” to host
    !Recv               get all text received since last Send/Recv
    !Selection          get current selected text


## Top secrets

> SSH know_hosts file is stored at %USERPROFILE%\.ssh on Windows, $HOME/.ssh on MacOS/Linux. Password, keyboard interactive and public key are the three ways of authentication supported, when public key is used, key pairs should be copied to the same .ssh directory. id_rsa is supported by the Microsoft store version, which was compiled with winCNG crypto backend, id_rsa, id_ecdsa and id_ed25512 are supported on the apple app store version, which was compiled with openssl crypto.

> scp and tunnling funciton is integrated to the terminal, when a ssh connection is active, enable local edit mode and try the following commands:

    !scp tt.txt :t1.txt secure copy local file tt.txt to remote host as t1.txt
    !scp :t1.txt d:/     secure copy remote files *.txt to local d:/
    !tun 127.0.0.1:2222 127.0.0.1:22 
                        start ssh2 tunnel from localhost port 2222 to remote host port 22
    !tun                list all ssh2 tunnels 
    !tun 3256           close ssh2 tunnel number 3256

> Command history file is saved as %USERPROFILE%\.tinyTerm\tinyTerm.hist on Windows, $HOME/.tinyTerm/tinyTerm.hist on MacOS/Linux by default, copy tinyTerm.hist to the same folder as tinyTerm2 executable for portable use. Since the command history file is just a plain text file, user can edit the file outside of tinyTerm to put additional commands in the list for command auto-completion. For example put all TL1 commands in the history list to use as a dictionary. In addition to command history, the following lines in the .hist file are used to save settings between sessions

    ~TermSize 100x40    set terminal size to 100 cols x 40 rows
    ~Transparency 192   set window transparency level to 192/255(only tinyTerm, not tinyTerm2)
    ~FontFace Consolas  set font face to “Consolas”
    ~FontSize 18        set font size to 18
    ~LocalEdit		Enable local edit

> One more option about local edit is "Send to all", when enabled locally edited command will be sent to all open tabs when "Enter" is pressed, this is useful when multiple host is contorlled at the same time.
>
