/*
 * fbkeyboard.c : framebuffer softkeyboard for touchscreen devices
 *
 * Copyright 2017 Julian Winkler <julia.winkler1@web.de>
 * Copyright 2020 Ferenc Bakonyi <bakonyi.ferenc@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <ft2build.h>
#include FT_FREETYPE_H

char *font = "/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf";
char *device = NULL;
char *special[][7] = {
	{ "Esc", "Tab", "F10", " / ", " - ", " . ", " \\ " },
	{ "Esc", "Tab", "F10", " ? ", " _ ", " > ", " | " },
};

char *layout[] = {
	"qwertyuiopasdfghjklzxcvbnm",
	"QWERTYUIOPASDFGHJKLZXCVBNM",
	"1234567890-=[];\'\\,.`/     ",
	"!@#$%^&*()_+{}:\"|<>~?     "
};

int layoutuse = 0;
__u16 keys[][26] = {
	{ KEY_ESC, KEY_TAB, KEY_F10, KEY_SLASH, KEY_MINUS, KEY_DOT,
	 KEY_BACKSLASH },
	{ KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O,
	 KEY_P,
	 KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
	 KEY_Z,
	 KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M },
	{ KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
	 KEY_0,
	 KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE,
	 KEY_SEMICOLON,
	 KEY_APOSTROPHE, KEY_BACKSLASH, KEY_COMMA, KEY_DOT, KEY_GRAVE,
	 KEY_SLASH,
	 KEY_C, KEY_V, KEY_B, KEY_N, KEY_M },
	{ KEY_LEFTSHIFT, KEY_BACKSPACE },
	{ KEY_LEFTALT, KEY_SPACE, KEY_RIGHTCTRL, KEY_ENTER },
	{ KEY_HOME, KEY_UP, KEY_PAGEUP,
	 KEY_LEFT, KEY_ENTER, KEY_RIGHT,
	 KEY_END, KEY_DOWN, KEY_PAGEDOWN,
	 KEY_RIGHTSHIFT }
};

#define TOUCHCOLOR 0x4444ee
#define BUTTONCOLOR 0x111122
#define BACKLITCOLOR 0xff0000
#define TERMCOLOR 0x000000
int gap = 2;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
char *buf;
int height;
int fduinput;
struct input_event ie;
FT_Face face;

void fill_rect(int x, int y, int w, int h, int color)
{
	int i, j;
	int32_t *line;
	for (i = 0; i < h; i++) {
		line = (int32_t *) (buf + finfo.line_length * (y + i));
		for (j = 0; j < w; j++) {
			*(line + x + j) = color;
		}
	}
}

void draw_char(int x, int y, char c)
{
	int i, j;
	int color;
	FT_Load_Char(face, c, FT_LOAD_RENDER);
	x += 2 + face->glyph->bitmap_left;
	y += (face->size->metrics.ascender >> 6) - face->glyph->bitmap_top;
	for (i = 0; i < (face->glyph->bitmap.rows); i++)
		for (j = 0; j < face->glyph->bitmap.width; j++) {
			color =
			    *(face->glyph->bitmap.buffer +
			      face->glyph->bitmap.pitch * i + j);
			if (color) {
				*(buf + finfo.line_length * (i + y) +
				  (j + x) * 4) = color;
				*(buf + finfo.line_length * (i + y) +
				  (j + x) * 4 + 1) = color;
				*(buf + finfo.line_length * (i + y) +
				  (j + x) * 4 + 2) = color;
			}
		}
}

void draw_text(int x, int y, char *text)
{
	while (*text) {
		draw_char(x, y, *text);
		text++;
		x += face->glyph->advance.x >> 6;
	}
}

void draw_key(int x, int y, int w, int h, int color)
{
	fill_rect(x + gap, y + gap, w - 2 * gap, 1, BACKLITCOLOR);
	fill_rect(x + gap, y + h - gap, w - 2 * gap, 1, BACKLITCOLOR);
	fill_rect(x + gap, y + gap, 1, h - 2 * gap, BACKLITCOLOR);
	fill_rect(x + w - gap, y + gap, 1, h - 2 * gap, BACKLITCOLOR);
	fill_rect(x + gap + 1, y + gap + 1, w - 2 * gap - 2,
		  h - 2 * gap - 2, color);
}

void draw_textbutton(int x, int y, int w, int h, int color, char *text)
{
	draw_key(x, y, w, h, color);
	draw_text(x + gap + 2, y + gap + 2, text);
}

void draw_button(int x, int y, int w, int h, int color, char chr)
{
	draw_key(x, y, w, h, color);
	draw_char(x + gap + 2, y + gap + 2, chr);
}

void send_key(__u16 code)
{
	ie.type = EV_KEY;
	ie.code = code;
	ie.value = 1;
	if (write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
		fprintf(stderr, "error: sending uinput event\n");
	ie.value = 0;
	if (write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
		fprintf(stderr, "error: sending uinput event\n");
//	ie.code = KEY_RIGHTCTRL;
//	if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
//		fprintf(stderr, "error: sending uinput event\n");
	ie.type = EV_SYN;
	ie.code = SYN_REPORT;
	if (write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
		fprintf(stderr, "error: sending uinput event\n");
}

int main(int argc, char *argv[])
{
	int fbfd, fdinput;
	struct input_absinfo abs_x, abs_y;
	FT_Library library;
	int absolute_x, absolute_y, touchdown, row, pressed =
	    -1, released, key, altlock = 0, ctrllock = 0;

	char c;
	while ((c = getopt(argc, argv, "d:f:h")) != (char) -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 'f':
			font = optarg;
			break;
		case 'h':
			printf("usage: %s [options]\npossible options are:\n -h: print this help\n -d: set path to inputdevice\n -f: set path to font\n",
			     argv[0]);
			exit(0);
			break;
		case '?':
			fprintf(stderr, "unrecognized option -%c\n",
				optopt);
			break;
		}
	}

	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd == -1) {
		perror("error: opening framebuffer device /dev/fb0");
		exit(-1);
	}
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
		perror("error: reading fixed framebuffer information");
		exit(-1);
	}
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
		perror("error: reading variable framebuffer information");
		exit(-1);
	}
	height = vinfo.yres / 3 / 5;	// height of one row

	if (FT_Init_FreeType(&library)) {
		perror("error: freetype initialization");
		exit(-1);
	}
	if (FT_New_Face(library, font, 0, &face)) {
		perror("unable to load font file");
		exit(-1);
	}
	if (FT_Set_Pixel_Sizes(face, height * 1 / 4, height * 1 / 4)) {
		perror("FT_Set_Pixel_Sizes failed");
		exit(-1);
	}

	if (device) {
		if ((fdinput = open(device, O_RDONLY)) == -1) {
			perror("failed to open input device node");
			exit(-1);
		}
	} else {
		DIR *inputdevs = opendir("/dev/input");
		struct dirent *dptr;
		fdinput = -1;
		while (dptr = readdir(inputdevs)) {
			if ((fdinput =
			     openat(dirfd(inputdevs), dptr->d_name,
				    O_RDONLY)) != -1
			    && ioctl(fdinput, EVIOCGBIT(0, sizeof(key)),
				     &key) != -1 && key >> EV_ABS & 1)
				break;
			if (fdinput != -1) {
				close(fdinput);
				fdinput = -1;
			}
		}
		if (fdinput == -1) {
			fprintf(stderr,
				"no absolute axes device found in /dev/input\n");
			exit(-1);
		}
	}
	if (ioctl(fdinput, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == -1)
		perror("error: getting touchscreen size");
	if (ioctl(fdinput, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == -1)
		perror("error: getting touchscreen size");

	fduinput = open("/dev/uinput", O_WRONLY);
	if (fduinput == -1) {
		perror("error: cannot open uinput device /dev/uinput");
		exit(-1);
	}
	if (ioctl(fduinput, UI_SET_EVBIT, EV_KEY) == -1)
		perror("error: SET_EVBIT EV_KEY");
	if (ioctl(fduinput, UI_SET_EVBIT, EV_SYN) == -1)
		perror("error: SET_EVBIT EV_SYN");
	for (row = 0; row < 6; row++)
		for (key = 0; key < sizeof(keys[row]); key++)
			ioctl(fduinput, UI_SET_KEYBIT, keys[row][key]);
	struct uinput_user_dev uidev;
	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "fbkeyboard");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 1;
	uidev.id.product = 1;
	uidev.id.version = 1;
	if (write(fduinput, &uidev, sizeof(uidev)) != sizeof(uidev))
		fprintf(stderr, "error setting up uinput\n");
	if (ioctl(fduinput, UI_DEV_CREATE) == -1)
		perror("error creating uinput dev");

	buf = malloc(finfo.line_length * (height * 5 + 1));
	if (buf == 0) {
		perror("malloc failed");
		exit(-1);
	}
	fill_rect(0, 0, vinfo.xres - 1, height * 5, TERMCOLOR);

	while (1) {
		for (key = 0; key < 7; key++) {
			draw_textbutton(key * vinfo.xres / 7 + 1, 1,
					vinfo.xres / 7 - 1, height - 1,
					(row == 0
					 && key ==
					 pressed) ? TOUCHCOLOR :
					BUTTONCOLOR,
					special[layoutuse & 1][key]);
		}
		for (key = 0; key < 10; key++) {
			draw_button(key * vinfo.xres / 10 + 1, height * 1,
				    vinfo.xres / 10 - 1, height - 1,
				    (row == 1
				     && key ==
				     pressed) ? TOUCHCOLOR : BUTTONCOLOR,
				    layout[layoutuse][key]);
		}
		for (key = 0; key < 9; key++) {
			draw_button(vinfo.xres / 20 +
				    key * vinfo.xres / 10, height * 2,
				    vinfo.xres / 10 - 1, height - 1,
				    (row == 1
				     && key + 10 ==
				     pressed) ? TOUCHCOLOR : BUTTONCOLOR,
				    layout[layoutuse][key + 10]);
		}
		draw_textbutton(1, height * 3, vinfo.xres * 3 / 20 - 1,
				height - 1,
				(layoutuse & 1) ? TOUCHCOLOR : BUTTONCOLOR,
				"Shift");
		for (key = 0; key < 7; key++) {
			draw_button(vinfo.xres * 3 / 20 +
				    key * vinfo.xres / 10, height * 3,
				    vinfo.xres / 10 - 1, height - 1,
				    (row == 1
				     && key + 19 ==
				     pressed) ? TOUCHCOLOR : BUTTONCOLOR,
				    layout[layoutuse][key + 19]);
		}
		draw_textbutton(vinfo.xres * 17 / 20, height * 3,
				vinfo.xres * 3 / 20 - 1, height - 1,
				(row == 3
				 && 1 ==
				 pressed) ? TOUCHCOLOR : BUTTONCOLOR,
				"Bcksp");
		draw_textbutton(1, height * 4, vinfo.xres * 3 / 20 - 1,
				height - 1,
				(99 == pressed) ? TOUCHCOLOR : BUTTONCOLOR,
				(layoutuse & 2) ? "abcABC" : "123!@\"");
		draw_textbutton(vinfo.xres * 3 / 20, height * 4,
				vinfo.xres / 10 - 1, height - 1,
				(altlock) ? TOUCHCOLOR : BUTTONCOLOR,
				"Alt");
		draw_button(vinfo.xres / 4, height * 4, vinfo.xres / 2 - 1,
			    height - 1, (row == 4
					&& 1 ==
					pressed) ? TOUCHCOLOR :
			    BUTTONCOLOR, ' ');
		draw_textbutton(vinfo.xres * 3 / 4, height * 4,
				vinfo.xres / 10 - 1, height - 1,
				(ctrllock) ? TOUCHCOLOR : BUTTONCOLOR,
				"Ctrl");
		draw_textbutton(vinfo.xres * 17 / 20, height * 4,
				vinfo.xres * 3 / 20 - 1, height - 1,
				(row == 4
				 && 3 ==
				 pressed) ? TOUCHCOLOR : BUTTONCOLOR,
				"Enter");

		lseek(fbfd, finfo.line_length * (vinfo.yres - height * 5),
		      SEEK_SET);
		write(fbfd, buf, finfo.line_length * height * 5);

		released = 0;
		while (read(fdinput, &ie, sizeof(struct input_event))
		       && !(ie.type == EV_SYN && ie.code == SYN_REPORT)) {
			if (ie.type == EV_ABS) {
				switch (ie.code) {
				case ABS_MT_POSITION_X:
					absolute_x = ie.value;
					touchdown = 1;
					key = 0;
					break;
				case ABS_MT_POSITION_Y:
					absolute_y = ie.value;
					touchdown = 1;
					key = 0;
					break;
				case ABS_MT_TRACKING_ID:
					if (ie.value == -1) {
						touchdown = 0;
						released = 1;
					}
					break;
				}
			}
			if (ie.type == EV_SYN && ie.code == SYN_MT_REPORT
			    && key) {
				touchdown = 0;
				released = 1;
			}
		}

		if (released && pressed != -1) {
			if (pressed == 99)	// second page
				layoutuse ^= 2;
			else if (row == 1) {	// normal keys (abc, 123, !@#)
				send_key(keys[row + (layoutuse >> 1)]
					 [pressed]);
			} else if (row == 3 && pressed == 0) {	// Left Shift
				layoutuse ^= 1;
				ie.type = EV_KEY;
				ie.code = KEY_LEFTSHIFT;
				ie.value = layoutuse & 1;
				if (write(fduinput, &ie, sizeof(ie)) !=
				    sizeof(ie))
					fprintf(stderr,
						"error sending uinput event\n");
			} else if (row == 4 && pressed == 0) {	// Left Alt
				altlock ^= 1;
				ie.type = EV_KEY;
				ie.code = KEY_LEFTALT;
				ie.value = altlock;
				if (write(fduinput, &ie, sizeof(ie)) !=
				    sizeof(ie))
					fprintf(stderr,
						"error sending uinput event\n");
			} else if (row == 4 && pressed == 2) {	// Right Ctrl
				ctrllock ^= 1;
				ie.type = EV_KEY;
				ie.code = KEY_RIGHTCTRL;
				ie.value = ctrllock;
				if (write(fduinput, &ie, sizeof(ie)) !=
				    sizeof(ie))
					fprintf(stderr,
						"error sending uinput event\n");
			} else {
				send_key(keys[row][pressed]);
			}
		}

		pressed = -1;
		if (touchdown) {
			switch (((absolute_y -
				  abs_y.maximum) * vinfo.yres +
				 abs_y.maximum * height * 5) / height /
				abs_y.maximum) {
			case 0:
				row = 0;
				pressed = absolute_x * 7 / abs_x.maximum;
				break;
			case 1:
				row = 1;
				pressed = absolute_x * 10 / abs_x.maximum;
				break;
			case 2:
				row = 1;
				if (absolute_x > abs_x.maximum / 20
				    && absolute_x <
				    abs_x.maximum * 19 / 20)
					pressed =
					    10 + (absolute_x * 10 -
						  abs_x.maximum / 2) /
					    abs_x.maximum;
				break;
			case 3:
				if (absolute_x < abs_x.maximum * 3 / 20) {
					row = 3;
					pressed = 0;	// Left Shift
				} else if (absolute_x <
					   abs_x.maximum * 17 / 20) {
					row = 1;
					pressed =
					    19 + (absolute_x * 10 -
						  abs_x.maximum * 3 / 2) /
					    abs_x.maximum;
				} else {
					row = 3;
					pressed = 1;	// Bcksp
				}
				break;
			case 4:
				row = 4;
				if (absolute_x < abs_x.maximum * 3 / 20)
					pressed = 99;	// 123!@
				else if (absolute_x <
					 abs_x.maximum * 5 / 20)
					pressed = 0;	// Left Alt
				else if (absolute_x <
					 abs_x.maximum * 15 / 20)
					pressed = 1;	// Space
				else if (absolute_x <
					 abs_x.maximum * 17 / 20)
					pressed = 2;	// Right Ctrl
				else
					pressed = 3;	// Enter
				break;
			default:
				row = 5;
				pressed =
				    3 * absolute_y * vinfo.yres /
				    (abs_y.maximum *
				     (vinfo.yres - height * 5));
				pressed *= 3;
				pressed += 3 * absolute_x / abs_x.maximum;
				break;
			}
		}
	}
}
