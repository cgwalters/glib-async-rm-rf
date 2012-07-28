rm-rf-async: rm-rf-async.c Makefile
	gcc `pkg-config --cflags --libs gio-2.0` -Wall -Werror=format-security -o rm-rf-async rm-rf-async.c
