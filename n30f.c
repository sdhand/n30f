/*
 *	n30f.c: display a png in a borderless and transparent non-wm-managed window
 *	Author: Samuel Hand (samuel dot hand at openmailbox dot org)
 *	License: WTFPL (See the LICENSE FILE)
 * 	This program is a dirty hack and is distributed under no warranty
 *	I take no responsibility for any damage caused by its use
*/

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

#include <xcb/xcb.h>

// simple structure to represent some information about our xcb display
// makes it easier to pass certain things around
struct xcb_display_info
{
	xcb_connection_t *c;
	xcb_screen_t *s;
	xcb_visualtype_t *v;
};

// print help information
// name: the name the program was called using
void usage (char *name)
{
	printf("usage: %s [-x xposition] [-y yposition] [-h, --help] [-d, --dock] [-c, --command] [-t, --title] [-u, --unmapped] [-p, --print] FILE\n", name);
	puts("    -h, --help       print this message");
	puts("    -s               set the image scaling");
	puts("    -x               set the x position");
	puts("    -y               set the y position");
	puts("    -i, --ignored    force the window to be ignored for non EWMH WMs");
	puts("    -d, --daemonise  run n30f daemonised");
	puts("    -b, --bottom     put n30f at the bottom of the screen");
	puts("    -c, --command    set the command to run on click");
	puts("    -t, --title      set the window title");
	puts("    -u, --unmapped   start with the window unmapped (hidden)");
	puts("    -p, --print      print the window id to stdout after starting");
}

// find a visualtype that supports true transparency
xcb_visualtype_t *get_alpha_visualtype(xcb_screen_t *s)
{
	xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(s);

	// iterate over the available visualtypes and return the first one with 32bit depth
	for(; di.rem; xcb_depth_next (&di)){
		if(di.data->depth == 32){
			return xcb_depth_visuals_iterator(di.data).data;
		}
	}
	// we didn't find any visualtypes with 32bit depth
	return NULL;
}

// setup a connection to X
struct xcb_display_info init (void){
	struct xcb_display_info display_info;

	// connect to the default display
	display_info.c = xcb_connect(NULL, NULL);
	if(xcb_connection_has_error(display_info.c))
		errx(1, "couldn't connect to X");

	//find the default screen
	display_info.s = xcb_setup_roots_iterator(xcb_get_setup(display_info.c)).data;
	if(display_info.s == NULL)
		errx(1, "couldn't find the screen");

	// get a visualtype with true transparency
	display_info.v = get_alpha_visualtype(display_info.s);
	if(display_info.v == NULL)
		errx(1, "transparency support not found");

	return display_info;
}

// create a window
xcb_window_t create_window(struct xcb_display_info display_info, int x, int y,
		int width, int height, int dock)
{
	// generate a colourmap for the window with alpha support
	xcb_window_t colormap = xcb_generate_id(display_info.c);
	xcb_create_colormap(display_info.c, XCB_COLORMAP_ALLOC_NONE, colormap,
			display_info.s->root, display_info.v->visual_id);

	// create the window
	xcb_window_t window = xcb_generate_id(display_info.c);
	const uint32_t vals[] = {0x00000000, 0x00000000, dock, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS, colormap};
	xcb_create_window(display_info.c, 32, window, display_info.s->root,
		x, y, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, display_info.v->visual_id,
		XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
			XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
		vals);

	xcb_free_colormap(display_info.c, colormap);

	return window;
}

// this will be used as an index when we iterate over the list of atoms
enum {
    NET_WM_WINDOW_TYPE,
    NET_WM_WINDOW_TYPE_DOCK,
    NET_WM_STATE,
    NET_WM_STATE_ABOVE,
    NET_WM_DESKTOP,
};

// configure the window then show it
void show_window (xcb_connection_t *c, xcb_window_t window, int x, int y, char *title, int should_map)
{
	// atom names that we want to find the atom for
	const char *atom_names[] = {
        "_NET_WM_WINDOW_TYPE",
        "_NET_WM_WINDOW_TYPE_DOCK",
        "_NET_WM_STATE",
        "_NET_WM_STATE_ABOVE",
        "_NET_WM_DESKTOP",
    };

	// get all the atoms
	const int atoms = sizeof(atom_names)/sizeof(char *);
	xcb_intern_atom_cookie_t atom_cookies[atoms];
	xcb_atom_t atom_list[atoms];
	xcb_intern_atom_reply_t *atom_reply;

	int i;
	for (i = 0; i < atoms; i++)
		atom_cookies[i] = xcb_intern_atom(c, 0, strlen(atom_names[i]), atom_names[i]);

	for (i = 0; i < atoms; i++) {
		atom_reply = xcb_intern_atom_reply(c, atom_cookies[i], NULL);
		if (!atom_reply)
			errx(1, "failed to find atoms");
		atom_list[i] = atom_reply->atom;
		free(atom_reply);
	}

	// set the atoms
	const uint32_t desktops[] = {-1};
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, atom_list[NET_WM_WINDOW_TYPE],
			XCB_ATOM_ATOM, 32, 1, &atom_list[NET_WM_WINDOW_TYPE_DOCK]);
	xcb_change_property(c, XCB_PROP_MODE_APPEND, window, atom_list[NET_WM_STATE],
			XCB_ATOM_ATOM, 32, 2, &atom_list[NET_WM_STATE_ABOVE]);
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, atom_list[NET_WM_DESKTOP],
			XCB_ATOM_CARDINAL, 32, 1, desktops);
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
			XCB_ATOM_STRING, 8, strlen(title), title);

	const uint32_t val[] = { 1 };
	xcb_change_window_attributes(c, window, XCB_CW_OVERRIDE_REDIRECT, val);

	// show the window
	if(should_map)
		xcb_map_window(c, window);

	// some WMs auto-position windows after they are mapped
    // this makes sure it gets to the right place
	const uint32_t vals[] = {x,y};
	xcb_configure_window(c, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);

	xcb_flush(c);
}

void draw (cairo_t *cr, cairo_surface_t *image)
{
	cairo_set_source_surface(cr, image, 0, 0);
	cairo_rectangle(cr, 0, 0,
			cairo_image_surface_get_width(image), cairo_image_surface_get_height(image));
	cairo_fill (cr);
}

int main (int argc, char **argv)
{
	int x = 0;
	int y = 0;
	double s = 1; /* scale at normal by default */
	int image_height = 0;
	int image_width = 0;
	int dock = 0;
	int background = 0;
	int unmapped = 0;
	int bottom = 0;
	char *filename;
	char *title="n30f";

	int help_flag = 0;
	int error_flag = 0;
	int print_flag = 0;

	int option = 0;
	int option_index = 0;

	// define the long options and the flags to set for getopt_long
	struct option long_options [] =
	{
		{"help", no_argument, &help_flag, 1},
		{"ignored", no_argument, &dock, 1},
		{"daemonise", no_argument, &background, 1},
		{"bottom", no_argument, &bottom, 1},
		{"command", required_argument, 0, 'c'},
		{"title", required_argument, 0, 't'},
		{"unmapped", no_argument, &unmapped, 1},
		{"print", no_argument, &print_flag, 1},
		{0, 0, 0, 0}
	};
	char *command = NULL;
	// parse options using getopt_long
	while(option != -1){
		option = getopt_long(argc, argv, "hbidupt:c:x:y:s:", long_options, &option_index);
		switch(option){
			case 'h':
				option = -1;
				help_flag = 1;
				break;

			case 'd':
				background = 1;
				break;

			case 'i':
				dock = 1;
				break;

			case 'u':
				unmapped = 1;
				break;

			case 'p':
				print_flag = 1;
				break;

			case 'b':
				bottom = 1;
				break;

			case 'c':
				command = malloc(strlen(optarg)+3);
				strcpy(command, optarg);
				strcat(command, " &");
				break;

			case 's':
				s = atof(optarg);
				break;

			case 'x':
				x = atoi(optarg);
				break;

			case 'y':
				y = atoi(optarg);
				break;

			case 't':
				title = strdup(optarg);
				break;

			case '?':
				error_flag = 1;
				break;
		}
	}

	if(help_flag){
		usage(argv[0]);
		return 0;
	}

	// if there was an error in the command line and the help was not specified
	// getopt should print nescarry errors, so just quit
	if(error_flag)
		return 1;

	// if there aren't any arguments left after those parsed by getopt_long
	if(!(optind < argc)){
		errx(1, "no file specified");
	}

	// if there is more than one argument left
	if((optind+1) != argc){
		puts("warning: unexpected argument");
	}

	filename = argv[optind];

	// load the image or quit
	cairo_surface_t *image = cairo_image_surface_create_from_png(filename);
	if(cairo_surface_status(image) != CAIRO_STATUS_SUCCESS)
		errx(1, "error reading file: %s", filename);

	// connect to the xserver
	struct xcb_display_info display_info = init();

	if(bottom)
		y = display_info.s->height_in_pixels - cairo_image_surface_get_height(image) - y;

	// multiply by scaling factor
	image_height = (int)(cairo_image_surface_get_height(image) * s);
	image_width = (int)(cairo_image_surface_get_width(image) * s);

	// create the window
	xcb_window_t window = create_window(display_info, x, y, image_width, image_height, dock);

	// get a surface for the window and create a destination for it
	cairo_surface_t *window_surface = cairo_xcb_surface_create(display_info.c, window,
			display_info.v, image_width, image_height);

	cairo_t *cr = cairo_create(window_surface);
	cairo_scale(cr, s, s);

	// configure the window and then map it
	show_window(display_info.c, window, x, y, title, !unmapped);


	if(print_flag){
		printf("0x%08x\n", window);
		fflush(stdout);
	}

	if(background)
		daemon(1, 0);

	//loop for events
	xcb_generic_event_t *ev;
	while((ev = xcb_wait_for_event(display_info.c))){
		if((ev->response_type & ~0x80) == XCB_EXPOSE){
			draw(cr, image);
			xcb_flush(display_info.c);
		}
		if((ev->response_type & ~0x80) == XCB_BUTTON_PRESS){
			if(command != NULL)
				system(command);
		}
	}
	return 1;
}
