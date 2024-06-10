# Create/update webgui_html.c and webgui_js.c in the parent directory.
# If necessary, first compile txt2c, which is used to build them.
CC=gcc

all: txt2c ../webgui_html.c ../webgui_js.c

txt2c: txt2c.c
	$(CC) -o txt2c txt2c.c

../webgui_html.c: txt2c webgui.html
	./txt2c webgui.html ../webgui_html.c webgui_html

../webgui_js.c: txt2c webgui.js
	./txt2c webgui.js ../webgui_js.c webgui_js
