#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "eggtrayicon.h"

#include "gettext.h"
#include "../IMdkit/IMdkit.h"
#include "../IMdkit/Xi18n.h"
#include "ic.h"
#include "server.h"
#include "nabi.h"

#include "default-icons.h"

#define KEYBOARD_MAP_SIZE   94

static void remove_event_filter();


static GdkPixbuf *none_pixbuf = NULL;
static GdkPixbuf *hangul_pixbuf = NULL;
static GdkPixbuf *english_pixbuf = NULL;
static GtkWidget *none_image = NULL;
static GtkWidget *hangul_image = NULL;
static GtkWidget *english_image = NULL;

static
guint32 string_to_hex(char* p)
{
    guint32 ret = 0;
    guint32 remain = 0;

    while (*p != '\0') {
	if (*p >= '0' && *p <= '9')
	    remain = *p - '0';
	else if (*p >= 'a' && *p <= 'f')
	    remain = *p - 'a' + 10;
	else if (*p >= 'A' && *p <= 'F')
	    remain = *p - 'A' + 10;
	else
	    return 0;

	ret = ret * 16 + remain;
	p++;
    }
    return ret;
}

gboolean
load_keyboardmap_from_file(char *filename, NabiKeyboardMap *keyboardmap)
{
    int i;
    char *line, *p, *saved_position;
    char buf[256];
    FILE* file;
    wchar_t key, value;
    wchar_t *map;

    /* init */
    keyboardmap->type = NABI_KEYBOARD_3SET;
    keyboardmap->name = NULL;
    keyboardmap->map = NULL;

    file = fopen(filename, "r");
    if (file == NULL) {
	fprintf(stderr, _("Nabi: Can't read keyboard map file\n"));
	return FALSE;
    }

    map = (wchar_t *)g_malloc(KEYBOARD_MAP_SIZE * sizeof(wchar_t));
    for (i = 0; i < KEYBOARD_MAP_SIZE; i++) {
	map[i] = XK_exclam + i;
    }

    for (line = fgets(buf, sizeof(buf), file);
	 line != NULL;
	 line = fgets(buf, sizeof(buf), file)) {
	p = strtok_r(line, " \t\n", &saved_position);
	/* comment */
	if (p == NULL || p[0] == '#')
	    continue;

	if (strcmp(p, "Name:") == 0) {
	    p = strtok_r(NULL, "\n", &saved_position);
	    if (p == NULL)
		continue;
	    keyboardmap->name = g_strdup(p);
	    continue;
	} else if (strcmp(p, "Type2") == 0) {
	    keyboardmap->type = NABI_KEYBOARD_2SET;
	} else {
	    key = string_to_hex(p);
	    if (key == 0)
		continue;

	    p = strtok_r(NULL, " \t", &saved_position);
	    if (p == NULL)
		continue;
	    value = string_to_hex(p);
	    if (value == 0)
		continue;

	    if (key < XK_exclam || key > XK_asciitilde)
		continue;

	    map[key - XK_exclam] = value;
	}
    }
    fclose(file);

    if (keyboardmap->name == NULL) {
	free(map);
	return FALSE;
    }

    keyboardmap->map = map;

    return TRUE;
}

static gint
compose_item_compare(gconstpointer a, gconstpointer b)
{
    return ((NabiComposeItem*)a)->key - ((NabiComposeItem*)b)->key;
}

gboolean
load_composemap_from_file(char *filename, NabiComposeMap *composemap)
{
    int i;
    char *line, *p, *saved_position;
    char buf[256];
    FILE* file;
    guint32 key1, key2;
    wchar_t value;
    NabiComposeItem *citem;
    guint map_size;
    GSList *list = NULL;

    /* init */
    composemap->name = NULL;
    composemap->map = NULL;
    composemap->size = 0;

    file = fopen(filename, "r");
    if (file == NULL) {
	fprintf(stderr, _("Nabi: Can't read compose map file\n"));
	return FALSE;
    }

    for (line = fgets(buf, sizeof(buf), file);
	 line != NULL;
	 line = fgets(buf, sizeof(buf), file)) {
	p = strtok_r(line, " \t\n", &saved_position);
	/* comment */
	if (p == NULL || p[0] == '#')
	    continue;

	if (strcmp(p, "Name:") == 0) {
	    p = strtok_r(NULL, "\n", &saved_position);
	    if (p == NULL)
		continue;
	    composemap->name = g_strdup(p);
	    continue;
	} else {
	    key1 = string_to_hex(p);
	    if (key1 == 0)
		continue;

	    p = strtok_r(NULL, " \t", &saved_position);
	    if (p == NULL)
		continue;
	    key2 = string_to_hex(p);
	    if (key2 == 0)
		continue;

	    p = strtok_r(NULL, " \t", &saved_position);
	    if (p == NULL)
		continue;
	    value = string_to_hex(p);
	    if (value == 0)
		continue;

	    citem = (NabiComposeItem*)
		g_malloc(sizeof(NabiComposeItem));
	    citem->key = key1 << 16 | key2;
	    citem->code = value;

	    list = g_slist_prepend(list, citem);
	}
    }
    fclose(file);

    if (composemap->name == NULL) {
	/* on error free the list */
	while (list != NULL) {
	    g_free(list->data);
	    list = list->next;
	}
	g_slist_free(list);

	return FALSE;
    }

    /* sort compose map */
    list = g_slist_reverse(list);
    list = g_slist_sort(list, compose_item_compare);

    /* move data to map */
    map_size = g_slist_length(list);
    composemap->map = (NabiComposeItem**)
		g_malloc(map_size * sizeof(NabiComposeItem*));
    for (i = 0; i < map_size; i++, list = list->next) {
	composemap->map[i] = list->data;
	list->data = NULL;
    }
    composemap->size = map_size;

    /* free the list */
    g_slist_free(list);

    return TRUE;
}

enum {
    CONF_TYPE_BOOL,
    CONF_TYPE_INT,
    CONF_TYPE_STR
};

struct config_item {
    gchar* key;
    gint   type;
    guint  offset;
};

#define BASE           ((void*)((NabiApplication*)NULL))
#define MEMBER(member) ((void*)(&(((NabiApplication*)NULL)->member)))
#define OFFSET(member) ((guint)(MEMBER(member) - BASE))

#define BOOL_BY_OFFSET(offset) (gboolean*)((void*)(nabi) + offset)
#define INT_BY_OFFSET(offset)      (gint*)((void*)(nabi) + offset)
#define CHAR_BY_OFFSET(offset)   (gchar**)((void*)(nabi) + offset)

const static struct config_item config_items[] = {
    { "x",                  CONF_TYPE_INT, OFFSET(x)                    },
    { "y",                  CONF_TYPE_INT, OFFSET(y)                    },
    { "theme",              CONF_TYPE_STR, OFFSET(theme)                },
    { "keyboardmap",        CONF_TYPE_STR, OFFSET(keyboardmap_filename) },
    { "composemap",         CONF_TYPE_STR, OFFSET(composemap_filename)  },
    { "preedit_foreground", CONF_TYPE_STR, OFFSET(preedit_fg)           },
    { "preedit_background", CONF_TYPE_STR, OFFSET(preedit_bg)           },
    { NULL,                 0,             0                            }
};

void
set_value_bool(guint offset, gchar* value)
{
    gboolean *member = BOOL_BY_OFFSET(offset);

    if (g_ascii_strcasecmp(value, "true") == 0) {
	*member = TRUE;
    } else {
	*member = FALSE;
    }
}

void
set_value_int(guint offset, gchar* value)
{
    int *member = INT_BY_OFFSET(offset);

    *member = strtol(value, NULL, 10);
}

void
set_value_str(guint offset, gchar* value)
{
    char **member = CHAR_BY_OFFSET(offset);

    g_free(*member);
    *member = g_strdup(value);
}

void
write_value_bool(FILE* file, gchar* key, guint offset)
{
    gboolean *member = BOOL_BY_OFFSET(offset);

    if (*member)
	fprintf(file, "%s=%s\n", key, "true");
    else
	fprintf(file, "%s=%s\n", key, "false");
}

void
write_value_int(FILE* file, gchar* key, guint offset)
{
    int *member = INT_BY_OFFSET(offset);

    fprintf(file, "%s=%d\n", key, *member);
}

void
write_value_str(FILE* file, gchar* key, guint offset)
{
    char **member = CHAR_BY_OFFSET(offset);

    fprintf(file, "%s=%s\n", key, *member);
}

void
load_config_item(gchar* key, gchar* value)
{
    gint i;

    for (i = 0; config_items[i].key != NULL; i++) {
	if (strcmp(key, config_items[i].key) == 0) {
	    switch (config_items[i].type) {
	    case CONF_TYPE_BOOL:
		set_value_bool(config_items[i].offset, value);
		break;
	    case CONF_TYPE_INT:
		set_value_int(config_items[i].offset, value);
		break;
	    case CONF_TYPE_STR:
		set_value_str(config_items[i].offset, value);
		break;
	    default:
		break;
	    }
	}
    }
}

void
load_config_file(void)
{
    gchar *line, *saved_position;
    gchar *key, *value;
    const gchar* homedir;
    gchar* config_filename;
    gchar buf[256];
    FILE *file;

    /* set default values */
    nabi->theme = g_strdup("SimplyRed");
    nabi->keyboardmap_filename = g_strconcat(NABI_DATA_DIR,
			     "/keyboard/2qwerty",
			     NULL);
    nabi->composemap_filename = g_strconcat(NABI_DATA_DIR,
			    "/compose/default",
			    NULL);
    nabi->preedit_fg = g_strdup("#FFFFFF");
    nabi->preedit_bg = g_strdup("#000000");

    /* load conf file */
    homedir = g_get_home_dir();
    config_filename = g_strconcat(homedir, "/.nabi/config", NULL);
    file = fopen(config_filename, "r");
    if (file == NULL) {
	fprintf(stderr, _("Nabi: Can't load config file\n"));
	return;
    }

    for (line = fgets(buf, sizeof(buf), file);
	 line != NULL;
	 line = fgets(buf, sizeof(buf), file)) {
	key = strtok_r(line, " =\t\n", &saved_position);
	value = strtok_r(NULL, " \t\n", &saved_position);
	if (key == NULL || value == NULL)
	    continue;
	    load_config_item(key, value);
    }
    fclose(file);
    g_free(config_filename);
}

void
save_config_file(void)
{
    gint i;
    const gchar* homedir;
    gchar* config_dir;
    gchar* config_filename;
    FILE *file;

    homedir = g_get_home_dir();
    config_dir = g_strconcat(homedir, "/.nabi", NULL);

    /* chech for nabi conf dir */
    if (!g_file_test(config_dir, G_FILE_TEST_EXISTS)) {
	int ret;
	/* we make conf dir */
	ret = mkdir(config_dir, S_IRUSR | S_IWUSR | S_IXUSR);
	if (ret != 0) {
	    perror("nabi");
	}
    }

    config_filename = g_strconcat(homedir, "/.nabi/config", NULL);
    file = fopen(config_filename, "w");
    if (file == NULL) {
	fprintf(stderr, _("Nabi: Can't write config file\n"));
	return;
    }

    for (i = 0; config_items[i].key != NULL; i++) {
	switch (config_items[i].type) {
	case CONF_TYPE_BOOL:
	    write_value_bool(file, config_items[i].key,
			   config_items[i].offset);
	    break;
	case CONF_TYPE_INT:
	    write_value_int(file, config_items[i].key,
			  config_items[i].offset);
	    break;
	case CONF_TYPE_STR:
	    write_value_str(file, config_items[i].key,
			  config_items[i].offset);
	    break;
	default:
	    break;
	}
    }

    fclose(file);
    g_free(config_filename);
}

void
load_keyboardmap(void)
{
    gboolean ret;

    ret = load_keyboardmap_from_file(nabi->keyboardmap_filename,
		     &nabi->keyboardmap);
    if (ret) {
	server->keyboard_map = nabi->keyboardmap.map;
	nabi_server_set_automata(server,
		     nabi->keyboardmap.type);
    } else {
	exit(1);
    }
}

void
load_composemap(void)
{
    gboolean ret;

    ret = load_composemap_from_file(nabi->composemap_filename,
		    &nabi->composemap);
    if (ret) {
	server->compose_map = nabi->composemap.map;
	server->compose_map_size = nabi->composemap.size;
    } else {
	exit(1);
    }
}

void
load_colors(void)
{
    gboolean ret;
    GdkColor color;
    GdkColormap *colormap;

    colormap = gdk_colormap_get_system();

    /* preedit foreground */
    gdk_color_parse(nabi->preedit_fg, &color);
    ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
    if (ret)
	server->preedit_fg = color.pixel;
    else {
	color.pixel = 1;
	color.red = 0xffff;
	color.green = 0xffff;
	color.blue = 0xffff;
	ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
	server->preedit_fg = color.pixel;
	fprintf(stderr, _("Can't allocate color: %s\n"),
		nabi->preedit_fg);
	fprintf(stderr, _("Use default color: #FFFFFF\n"));
    }

    /* preedit background */
    gdk_color_parse(nabi->preedit_bg, &color);
    ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
    if (ret)
	server->preedit_bg = color.pixel;
    else {
	color.pixel = 0;
	color.red = 0;
	color.green = 0;
	color.blue = 0;
	ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
	server->preedit_fg = color.pixel;
	fprintf(stderr, _("Can't allocate color: %s\n"),
		nabi->preedit_fg);
	fprintf(stderr, _("Use default color: #000000\n"));
    }
}

void
nabi_app_new(void)
{
    nabi = g_malloc(sizeof(NabiApplication));
    memset(nabi, 0, sizeof(NabiApplication));

    load_config_file();

    /* set atoms for hangul status */
    nabi->mode_info_atom = gdk_atom_intern("_HANGUL_INPUT_MODE", TRUE);
    nabi->mode_info_type = gdk_atom_intern("INTEGER", TRUE);
    nabi->mode_info_xatom = gdk_x11_atom_to_xatom(nabi->mode_info_atom);
}

void
nabi_app_init(void)
{
}

void
nabi_app_setup_server(void)
{
    load_keyboardmap();
    load_composemap();

    load_colors();
}

void
nabi_app_free(void)
{
    int i;

    save_config_file();

    g_free(nabi->theme);

    g_free(nabi->keyboardmap_filename);
    g_free(nabi->keyboardmap.name);
    g_free(nabi->keyboardmap.map);

    g_free(nabi->composemap_filename);
    g_free(nabi->composemap.name);
    for (i = 0; i < nabi->composemap.size; i++) {
	g_free(nabi->composemap.map[i]);
    }
    g_free(nabi->composemap.map);

    g_free(nabi->preedit_fg);
    g_free(nabi->preedit_bg);
}

void
nabi_quit(void)
{
    gtk_main_quit();
}

gboolean
on_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    g_object_unref(G_OBJECT(none_pixbuf));
    g_object_unref(G_OBJECT(hangul_pixbuf));
    g_object_unref(G_OBJECT(english_pixbuf));

    remove_event_filter();

    nabi_quit();
    return TRUE;
}

gboolean
on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (event->type != GDK_BUTTON_PRESS)
	return FALSE;

    switch (event->button) {
    case 1:
	/* start moving */
	nabi->x_clicked = (gint)event->x;
	nabi->y_clicked = (gint)event->y;
	nabi->start_moving = TRUE;
	return TRUE;
	break;
    case 3:
	/* popup menu */
	gtk_menu_popup(GTK_MENU(data), NULL, NULL, NULL, NULL,
		   event->button, event->time);
	return TRUE;
    default:
	break;
    }

    return FALSE;
}

gboolean
on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (event->type != GDK_BUTTON_RELEASE)
	return FALSE;

    nabi->start_moving = FALSE;

    return TRUE;
}

gboolean
on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    gint x, y;

    if (event->type != GDK_MOTION_NOTIFY)
	return FALSE;

    if (!nabi->start_moving)
	return FALSE;

    x = (gint)(event->x_root - nabi->x_clicked);
    y = (gint)(event->y_root - nabi->y_clicked);

    return TRUE;
}

GtkWidget*
create_pref_window(void)
{
    GtkWidget *window;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_widget_show(window);
    return window;
}

void
on_menu_about(GtkWidget *widget)
{
    g_print("About\n");
}

void
on_menu_pref(GtkWidget *widget)
{
    g_print("Pref\n");
}

void
on_menu_quit(GtkWidget *widget)
{
    nabi_quit();
}

GtkWidget*
create_menu(void)
{
    GtkWidget* menu;
    GtkWidget* menu_item;

    menu = gtk_menu_new();
    gtk_widget_show(menu);

    /* menu about */
    menu_item = gtk_menu_item_new_with_mnemonic(_("_About..."));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect_swapped(G_OBJECT(menu_item), "activate",
		 G_CALLBACK(on_menu_about), menu_item);

    /* separator */
    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);

    /* menu preferences */
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Preferences"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect_swapped(G_OBJECT(menu_item), "activate",
		 G_CALLBACK(on_menu_pref), menu_item);

    /* menu quit */
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Quit"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect_swapped(G_OBJECT(menu_item), "activate",
		 G_CALLBACK(on_menu_quit), menu_item);

    return menu;
}

void
load_icons(const gchar *theme)
{
    gchar buf[1024];
    GError    *gerror = NULL;

    if (theme == NULL)
    	theme = "SimplyRed";

    g_snprintf(buf, sizeof(buf), "%s/%s/none.png", NABI_THEMES_DIR, theme);
    none_pixbuf = gdk_pixbuf_new_from_file(buf, &gerror);
    if (gerror != NULL) {
	g_print("Error on reading image file: %s\n", gerror->message);
	g_error_free(gerror);
	gerror = NULL;
	none_pixbuf = gdk_pixbuf_new_from_xpm_data(none_default_xpm);
    }

    g_snprintf(buf, sizeof(buf), "%s/%s/hangul.png", NABI_THEMES_DIR, theme);
    hangul_pixbuf = gdk_pixbuf_new_from_file(buf, &gerror);
    if (gerror != NULL) {
	g_print("Error on reading image file: %s\n", gerror->message);
	g_error_free(gerror);
	gerror = NULL;
	hangul_pixbuf = gdk_pixbuf_new_from_xpm_data(hangul_default_xpm);
    }

    g_snprintf(buf, sizeof(buf), "%s/%s/english.png", NABI_THEMES_DIR, theme);
    english_pixbuf = gdk_pixbuf_new_from_file(buf, &gerror);
    if (gerror != NULL) {
	g_print("Error on reading image file: %s\n", gerror->message);
	g_error_free(gerror);
	gerror = NULL;
	english_pixbuf = gdk_pixbuf_new_from_xpm_data(english_default_xpm);
    }
}

void
create_icons(gint default_size)
{
    double factor;
    gint new_width, new_height;
    gint orig_width, orig_height;
    gint default_width, default_height;
    GdkPixbuf *pixbuf;

    default_width = default_size;
    default_height = default_size;

    load_icons(nabi->theme);

    orig_width = gdk_pixbuf_get_width(none_pixbuf);
    orig_height = gdk_pixbuf_get_height(none_pixbuf);

    if (orig_width > orig_height) {
	factor =  (double)default_width / (double)orig_width;
	new_width = default_width;
	new_height = (int)(orig_height * factor);
    } else {
	factor = (double)default_height / (double)orig_height;
	new_width = (int)(orig_width * factor);
	new_height = default_height;
    }

    pixbuf = gdk_pixbuf_scale_simple(none_pixbuf, new_width, new_height,
	    			     GDK_INTERP_BILINEAR);
    none_image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(G_OBJECT(pixbuf));

    pixbuf = gdk_pixbuf_scale_simple(hangul_pixbuf, new_width, new_height,
	    			     GDK_INTERP_BILINEAR);
    hangul_image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(G_OBJECT(pixbuf));

    pixbuf = gdk_pixbuf_scale_simple(english_pixbuf, new_width, new_height,
	    			     GDK_INTERP_BILINEAR);
    english_image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(G_OBJECT(pixbuf));
}

static void
update_state (int state)
{
    switch (state) {
    case 0:
	gtk_widget_show(none_image);
	gtk_widget_hide(hangul_image);
	gtk_widget_hide(english_image);
	break;
    case 1:
	gtk_widget_hide(none_image);
	gtk_widget_hide(hangul_image);
	gtk_widget_show(english_image);
	break;
    case 2:
	gtk_widget_hide(none_image);
	gtk_widget_show(hangul_image);
	gtk_widget_hide(english_image);
	break;
    default:
	gtk_widget_show(none_image);
	gtk_widget_hide(hangul_image);
	gtk_widget_hide(english_image);
	break;
    }
}

static void
nabi_set_input_mode_info (int state)
{
    long data = state;

    if (nabi->root_window == NULL)
	return;

    gdk_property_change (nabi->root_window,
		         gdk_atom_intern ("_HANGUL_INPUT_MODE", FALSE),
		         gdk_atom_intern ("INTEGER", FALSE),
		         32, GDK_PROP_MODE_REPLACE,
		         (const guchar *)&data, 1);
}

static GdkFilterReturn
mode_info_cb (GdkXEvent *gxevent, GdkEvent *event, gpointer data)
{
	int *state;
	XEvent *xevent;
	XPropertyEvent *pevent;

	xevent = (XEvent*)gxevent;
	if (xevent->type != PropertyNotify)
		return GDK_FILTER_CONTINUE;

	pevent = (XPropertyEvent*)xevent;
	if (pevent->atom == nabi->mode_info_xatom) {
		gboolean ret;

		ret = gdk_property_get (nabi->root_window,
					nabi->mode_info_atom,
					nabi->mode_info_type,
					0, 32, 0,
					NULL, NULL, NULL,
					(guchar**)&state);
		update_state(*state);
		g_free(state);
	}

	return GDK_FILTER_CONTINUE;
}

static void
install_event_filter(GtkWidget *widget)
{
    GdkScreen *screen;

    screen = gdk_drawable_get_screen(GDK_DRAWABLE(widget->window));
    nabi->root_window = gdk_screen_get_root_window(screen);
    gdk_window_set_events(nabi->root_window, GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(nabi->root_window, mode_info_cb, NULL);
}

static void
remove_event_filter()
{
    gdk_window_remove_filter(nabi->root_window, mode_info_cb, NULL);
}

static void
on_realize(GtkWidget *widget, gpointer data)
{
    install_event_filter(widget);
    nabi_server_set_mode_info_cb(server, nabi_set_input_mode_info);
}

GtkWidget*
create_main_widget(void)
{
    EggTrayIcon *tray_icon;
    GtkWidget *eventbox;
    GtkWidget *hbox;
    GtkWidget *menu;

    menu = create_menu();

    tray_icon = egg_tray_icon_new("Tray icon");

    eventbox = gtk_event_box_new();
    gtk_widget_show(eventbox);
    gtk_container_add(GTK_CONTAINER(tray_icon), eventbox);
    g_signal_connect(G_OBJECT(eventbox), "button-press-event",
	     G_CALLBACK(on_button_press), menu);
    g_signal_connect(G_OBJECT(eventbox), "button-release-event",
	     G_CALLBACK(on_button_release), NULL);
    g_signal_connect(G_OBJECT(eventbox), "motion-notify-event",
	     G_CALLBACK(on_motion_notify), NULL);

    create_icons(24);

    hbox = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), none_image, TRUE, TRUE, 0);
    gtk_widget_show(none_image);
    gtk_box_pack_start(GTK_BOX(hbox), hangul_image, TRUE, TRUE, 0);
    gtk_widget_hide(hangul_image);
    gtk_box_pack_start(GTK_BOX(hbox), english_image, TRUE, TRUE, 0);
    gtk_widget_hide(english_image);
    gtk_container_add(GTK_CONTAINER(eventbox), hbox);
    gtk_widget_show(hbox);

    g_signal_connect_after(G_OBJECT(tray_icon), "realize",
	    		   G_CALLBACK(on_realize), NULL);
    g_signal_connect(G_OBJECT(tray_icon), "delete-event",
		     G_CALLBACK(on_delete), NULL);
    g_signal_connect(G_OBJECT(tray_icon), "destroy-event",
		     G_CALLBACK(on_delete), NULL);

    return GTK_WIDGET(tray_icon);
}

/* vim: set ts=8 sw=4 : */