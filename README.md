# RunHiddenConsole
Hide console window for windows programs

What it does.
-------------
RunHiddenConsole is a lite program for hiding console window,it running on windows,like a linux command line end with '&',
Let the program run behind without bloking console.

# RunHiddenConsole solves the following problems:

* Running Windows console programs in the background (similar to Linux daemon processes)
* Solving the issue of starting multiple console services simultaneously from a single .bat file
* Enabling graceful exit of console services

# Usages.
-------------
	
RunHiddenConsole Usage:
RunHiddenConsole.exe [/l] [/w] [/r] [/n name] [/k name] [/o output-file] [/p pidfile] commandline
For example:
RunHiddenConsole.exe /l /r e:\WNMP\PHP\php-cgi.exe -b 127.0.0.1:9000 -c e:\WNMP\php\php.ini
RunHiddenConsole.exe /l /r E:/WNMP/nginx/nginx.exe -p E:/WNMP/nginx
The /l is optional, printing the result of process startup
The /w is optional, waiting for termination of the process
The /o is optional, redirecting the output of the program to a file
The /p is optional, saving the process id to a file
The /r is optional, supervise the child process, if the child process exits, restart the child process
The /n is optional, naming control signals
The /k is optional, kill the daemon according to the specified control signal

# A sample batch file for start service
```bash
RunHiddenConsole.exe /l /r /n phpfpm-8902 E:/WebWorkroom/php-7.4.12-nts/php-cgi.exe -b 127.0.0.1:8902 -c E:/WebWorkroom/php-7.4.12-nts/php.ini
RunHiddenConsole.exe /l /r /n phpfpm-8904 E:/WebWorkroom/php-7.4.12-nts/php-cgi.exe -b 127.0.0.1:8904 -c E:/WebWorkroom/php-7.4.12-nts/php.ini
RunHiddenConsole.exe /l /r /n nginx-blog E:/WebWorkroom/nginx/nginx.exe -p E:/WebWorkroom/nginx
```
# A sample batch file for stop service
```bash
RunHiddenConsole.exe /k /n phpfpm-8902
RunHiddenConsole.exe /k /n phpfpm-8904
RunHiddenConsole.exe /k /n nginx-blog
```
