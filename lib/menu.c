#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/sdf.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>
#include <toaru/icon_cache.h>

#include <toaru/menu.h>

#define MENU_ENTRY_HEIGHT 20
#define MENU_BACKGROUND rgb(239,238,232)
#define MENU_ICON_SIZE 16

#define HILIGHT_BORDER_TOP rgb(54,128,205)
#define HILIGHT_GRADIENT_TOP rgb(93,163,236)
#define HILIGHT_GRADIENT_BOTTOM rgb(56,137,220)
#define HILIGHT_BORDER_BOTTOM rgb(47,106,167)

static hashmap_t * menu_windows = NULL;
static yutani_t * my_yctx = NULL;

static struct MenuList * hovered_menu = NULL;

int menu_definitely_close(struct MenuList * menu);

__attribute__((constructor))
static void _init_menus(void) {
	menu_windows = hashmap_create_int(10);
}

hashmap_t * menu_get_windows_hash(void) {
	return menu_windows;
}

void _menu_draw_MenuEntry_Normal(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	struct MenuEntry_Normal * _self = (struct MenuEntry_Normal *)self;

	_self->offset = offset;

	/* Background gradient */
	if (_self->hilight) {
		draw_line(ctx, 1, _self->width-2, offset, offset, HILIGHT_BORDER_TOP);
		draw_line(ctx, 1, _self->width-2, offset + _self->height - 1, offset + _self->height - 1, HILIGHT_BORDER_BOTTOM);
		for (int i = 1; i < self->height-1; ++i) {
			int thing = ((i - 1) * 256) / (_self->height - 2);
			if (thing > 255) thing = 255;
			if (thing < 0) thing = 0;
			uint32_t c = interp_colors(HILIGHT_GRADIENT_TOP, HILIGHT_GRADIENT_BOTTOM, thing);
			draw_line(ctx, 1, self->width-2, offset + i, offset + i, c);
		}
	}

	/* Icon */
	if (_self->icon) {
		sprite_t * icon = icon_get_16(_self->icon);
		if (icon->width == MENU_ICON_SIZE) {
			draw_sprite(ctx, icon, 4, offset + 2);
		} else {
			draw_sprite_scaled(ctx, icon, 4, offset + 2, MENU_ICON_SIZE, MENU_ICON_SIZE);
		}
	}

	/* Foreground text color */
	uint32_t color = _self->hilight ? rgb(255,255,255) : rgb(0,0,0);

	/* Draw title */
	draw_sdf_string(ctx, 22, offset + 1, _self->title, 16, color, SDF_FONT_THIN);
}

void _menu_focus_MenuEntry_Normal(struct MenuEntry * self, int focused) {
	if (focused) {
		if (self->_owner && self->_owner->child) {
			menu_definitely_close(self->_owner->child);
			self->_owner->child = NULL;
		}
	}
}

void _menu_activate_MenuEntry_Normal(struct MenuEntry * self, int flags) {
	struct MenuEntry_Normal * _self = (struct MenuEntry_Normal *)self;

	list_t * menu_keys = hashmap_keys(menu_windows);
	hovered_menu = NULL;
	foreach(_key, menu_keys) {
		yutani_window_t * window = hashmap_get(menu_windows, (void*)_key->value);
		if (window) {
			struct MenuList * menu = window->user_data;
			menu_definitely_close(menu);
			if (menu->parent && menu->parent->child == menu) {
				menu->parent->child = NULL;
			}
		}
	}

	list_free(menu_keys);
	free(menu_keys);

	if (_self->callback) {
		_self->callback(_self);
	}
}

struct MenuEntry * menu_create_normal(const char * icon, const char * action, const char * title, void (*callback)(struct MenuEntry *)) {
	struct MenuEntry_Normal * out = malloc(sizeof(struct MenuEntry_Normal));

	out->_type = MenuEntry_Normal;
	out->height = MENU_ENTRY_HEIGHT;
	out->hilight = 0;
	out->renderer = _menu_draw_MenuEntry_Normal;
	out->focus_change = _menu_focus_MenuEntry_Normal;
	out->activate = _menu_activate_MenuEntry_Normal;
	out->icon = icon ? strdup(icon) : NULL;
	out->title = strdup(title);
	out->action = action ? strdup(action) : NULL;
	out->callback = callback;

	out->rwidth = 50 + draw_sdf_string_width(out->title, 16, SDF_FONT_THIN);

	return (struct MenuEntry *)out;
}

void _menu_draw_MenuEntry_Submenu(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	_menu_draw_MenuEntry_Normal(ctx,self,offset);
	struct MenuEntry_Submenu * _self = (struct MenuEntry_Submenu *)self;

	/* Draw the tick on the right side to indicate this is a submenu */
	uint32_t color = _self->hilight ? rgb(255,255,255) : rgb(0,0,0);
	sprite_t * tick = icon_get_16("menu-tick");
	draw_sprite_alpha_paint(ctx, tick, _self->width - 16, offset + 2, 1.0, color);
}

void _menu_focus_MenuEntry_Submenu(struct MenuEntry * self, int focused) {
	if (focused) {
		self->activate(self, focused);
	}
}

void _menu_activate_MenuEntry_Submenu(struct MenuEntry * self, int focused) {
	struct MenuEntry_Submenu * _self = (struct MenuEntry_Submenu *)self;

	if (_self->_owner && _self->_owner->set) {
		/* Show a menu */
		struct MenuList * new_menu = menu_set_get_menu(_self->_owner->set, (char *)_self->action);
		if (_self->_owner->child && _self->_owner->child != new_menu) {
			menu_definitely_close(_self->_owner->child);
			_self->_owner->child = NULL;
		}
		new_menu->parent = _self->_owner;
		new_menu->parent->child = new_menu;
		if (new_menu->closed) {
			menu_show(new_menu, _self->_owner->window->ctx);
			if (_self->_owner->window->width + _self->_owner->window->x - 2 + new_menu->window->width > _self->_owner->window->ctx->display_width) {
				yutani_window_move(_self->_owner->window->ctx, new_menu->window, _self->_owner->window->x + 2 - new_menu->window->width, _self->_owner->window->y + _self->offset - 4);
			} else {
				yutani_window_move(_self->_owner->window->ctx, new_menu->window, _self->_owner->window->width + _self->_owner->window->x - 2, _self->_owner->window->y + _self->offset - 4);
			}
		}
	}

}

struct MenuEntry * menu_create_submenu(const char * icon, const char * action, const char * title) {
	struct MenuEntry_Submenu * out = malloc(sizeof(struct MenuEntry_Submenu));

	out->_type = MenuEntry_Submenu;
	out->height = MENU_ENTRY_HEIGHT;
	out->hilight = 0;
	out->renderer = _menu_draw_MenuEntry_Submenu;
	out->focus_change = _menu_focus_MenuEntry_Submenu;
	out->activate = _menu_activate_MenuEntry_Submenu;
	out->icon = icon ? strdup(icon) : NULL;
	out->title = strdup(title);
	out->action = action ? strdup(action) : NULL;

	out->rwidth = 50 + draw_sdf_string_width(out->title, 16, SDF_FONT_THIN);

	return (struct MenuEntry *)out;
}

void _menu_draw_MenuEntry_Separator(gfx_context_t * ctx, struct MenuEntry * self, int offset) {
	self->offset = offset;
	draw_line(ctx, 2, self->width-4, offset+3, offset+3, rgb(178,178,178));
	draw_line(ctx, 2, self->width-5, offset+4, offset+4, rgb(250,250,250));
}

void _menu_focus_MenuEntry_Separator(struct MenuEntry * self, int focused) {
	if (focused) {
		if (self->_owner && self->_owner->child) {
			menu_definitely_close(self->_owner->child);
			self->_owner->child = NULL;
		}
	}
}

void _menu_activate_MenuEntry_Separator(struct MenuEntry * self, int focused) {

}

struct MenuEntry * menu_create_separator(void) {
	struct MenuEntry_Separator * out = malloc(sizeof(struct MenuEntry_Separator));

	out->_type = MenuEntry_Separator;
	out->height = 6;
	out->hilight = 0;
	out->renderer = _menu_draw_MenuEntry_Separator;
	out->focus_change = _menu_focus_MenuEntry_Separator;
	out->rwidth = 10; /* at least a bit please */
	out->activate = _menu_activate_MenuEntry_Separator;

	return (struct MenuEntry *)out;
}

static int _close_enough(struct yutani_msg_window_mouse_event * me) {
	if (me->command == YUTANI_MOUSE_EVENT_RAISE && sqrt(pow(me->new_x - me->old_x, 2) + pow(me->new_y - me->old_y, 2)) < 10) {
		return 1;
	}
	return 0;
}

static char read_buf[1024];
static size_t available = 0;
static size_t offset = 0;
static size_t read_from = 0;
static char * read_line(FILE * f, char * out, ssize_t len) {
	while (len > 0) {
		if (available == 0) {
			if (offset == 1024) {
				offset = 0;
			}
			size_t r = read(fileno(f), &read_buf[offset], 1024 - offset);
			read_from = offset;
			available = r;
			offset += available;
		}

		if (available == 0) {
			*out = '\0';
			return out;
		}

		while (read_from < offset && len > 0) {
			*out = read_buf[read_from];
			len--;
			read_from++;
			available--;
			if (*out == '\n') {
				return out;
			}
			out++;
		}
	}

	return out;
}

static void _menu_calculate_dimensions(struct MenuList * menu, int * height, int * width) {
	list_t * list = menu->entries;
	*width = 0;
	*height = 8; /* TODO top and height */
	foreach(node, list) {
		struct MenuEntry * entry = node->value;
		*height += entry->height;
		if (*width < entry->rwidth) {
			*width = entry->rwidth;
		}
	}
	/* Go back through and update actual widths */
	foreach(node, list) {
		struct MenuEntry * entry = node->value;
		entry->width = *width;
	}
}

struct MenuList * menu_set_get_root(struct MenuSet * menu) {
	return (void*)hashmap_get(menu->_menus,"_");
}

struct MenuList * menu_set_get_menu(struct MenuSet * menu, char * submenu) {
	return (void*)hashmap_get(menu->_menus, submenu);
}

void menu_insert(struct MenuList * menu, struct MenuEntry * entry) {
	list_insert(menu->entries, entry);
	entry->_owner = menu;
}

struct MenuList * menu_create(void) {
	struct MenuList * p = malloc(sizeof(struct MenuList));
	p->entries = list_create();
	p->ctx = NULL;
	p->window = NULL;
	p->set = NULL;
	p->child = NULL;
	p->parent = NULL;
	p->closed = 1;
	return p;
}

struct MenuSet * menu_set_create(void) {
	struct MenuSet * _out = malloc(sizeof(struct MenuSet));
	_out->_menus = hashmap_create(10);
	return _out;
}

void menu_set_insert(struct MenuSet * set, char * action, struct MenuList * menu) {
	hashmap_set(set->_menus, action, menu);
	menu->set = set;
}

struct MenuSet * menu_set_from_description(const char * path, void (*callback)(struct MenuEntry *)) {
	FILE * f;
	if (!strcmp(path,"-")) {
		f = stdin;
	} else {
		f = fopen(path,"r");
	}

	if (!f) {
		return NULL;
	}

	struct MenuSet * _out = malloc(sizeof(struct MenuSet));
	hashmap_t * out = hashmap_create(10);
	_out->_menus = out;

	struct MenuList * current_menu = NULL;

	/* Read through the file */
	char line[256];
	while (1) {
		memset(line, 0, 256);
		read_line(f, line, 256);
		if (!*line) break;

		if (line[strlen(line)-1] == '\n') {
			line[strlen(line)-1] = '\0';
		}

		if (!*line) continue; /* skip blank */

		if (*line == ':') {
			/* New menu */
			struct MenuList * p = malloc(sizeof(struct MenuList));
			p->entries = list_create();
			p->ctx = NULL;
			p->window = NULL;
			p->set = _out;
			p->child = NULL;
			p->parent = NULL;
			p->closed = 1;
			hashmap_set(out, line+1, p);
			current_menu = p;
		} else if (*line == '#') {
			/* Comment */
			continue;
		} else if (*line == '-') {
			if (!current_menu) {
				fprintf(stderr, "Tried to add separator with no active menu.\n");
				goto failure;
			}
			menu_insert(current_menu, menu_create_separator());
		} else if (*line == '&') {
			if (!current_menu) {
				fprintf(stderr, "Tried to add submenu with no active menu.\n");
				goto failure;
			}
			char * action = line+1;
			char * icon = strstr(action,",");
			if (!icon) {
				fprintf(stderr, "Malformed line in submenu: no icon\n");
				goto failure;
			}
			*icon = '\0';
			icon++;
			char * title = strstr(icon,",");
			if (!title) {
				fprintf(stderr, "Malformed line in submenu: no title\n");
				goto failure;
			}
			*title = '\0';
			title++;
			menu_insert(current_menu, menu_create_submenu(icon,action,title));
		} else {
			if (!current_menu) {
				fprintf(stderr, "Tried to add item with no active menu.\n");
				goto failure;
			}
			char * action = line;
			char * icon = strstr(action,",");
			if (!icon) {
				fprintf(stderr, "Malformed line in action: no icon\n");
				goto failure;
			}
			*icon = '\0';
			icon++;
			char * title = strstr(icon,",");
			if (!title) {
				fprintf(stderr, "Malformed line in action: no title\n");
				goto failure;
			}
			*title = '\0';
			title++;
			menu_insert(current_menu, menu_create_normal(icon,action,title,callback));
		}
	}

	return _out;

failure:
	fprintf(stderr, "malformed description file\n");
	if (f != stdin) {
		fclose(f);
	}
	free(out);
	return NULL;
}

static void _menu_redraw(yutani_window_t * menu_window, yutani_t * yctx, struct MenuList * menu) {

	gfx_context_t * ctx = menu->ctx;
	list_t * entries = menu->entries;
	/* Window background */
	draw_fill(ctx, MENU_BACKGROUND);

	/* Window border */
	draw_line(ctx, 0, ctx->width-1, 0, 0, rgb(109,111,112));
	draw_line(ctx, 0, 0, 0, ctx->height-1, rgb(109,111,112));
	draw_line(ctx, ctx->width-1, ctx->width-1, 0, ctx->height-1, rgb(109,111,112));
	draw_line(ctx, 0, ctx->width-1, ctx->height-1, ctx->height-1, rgb(109,111,112));

	/* Draw menu entries */
	int offset = 4;
	foreach(node, entries) {
		struct MenuEntry * entry = node->value;
		if (entry->renderer) {
			entry->renderer(ctx, entry, offset);
		}

		offset += entry->height;
	}

	/* Expose menu */
	flip(ctx);
	yutani_flip(yctx, menu_window);
}

void menu_show(struct MenuList * menu, yutani_t * yctx) {
	/* Calculate window dimensions */
	int height, width;
	_menu_calculate_dimensions(menu,&height, &width);

	my_yctx = yctx;

	menu->closed = 0;

	/* Create window */
	yutani_window_t * menu_window = yutani_window_create_flags(yctx, width, height, YUTANI_WINDOW_FLAG_ALT_ANIMATION);
	if (menu->ctx) {
		reinit_graphics_yutani(menu->ctx, menu_window);
	} else {
		menu->ctx = init_graphics_yutani_double_buffer(menu_window);
	}

	menu_window->user_data = menu;
	menu->window = menu_window;

	_menu_redraw(menu_window, yctx, menu);

	hashmap_set(menu_windows, (void*)menu_window->wid, menu_window);
}

int menu_has_eventual_child(struct MenuList * root, struct MenuList * child) {

	if (!child) return 0;
	if (root == child) return 1;

	struct MenuList * candidate = root->child;

	while (candidate && candidate != child) {
		if (candidate == root->child) {
			root->child = NULL;
			return 1;
		}
		candidate = root->child;
	}

	return (candidate == child);
}

int menu_definitely_close(struct MenuList * menu) {

	if (menu->child) {
		menu_definitely_close(menu->child);
		menu->child = NULL;
	}

	if (menu->closed) {
		return 0;
	}

	/* if focused_widget, leave focus on widget */
	foreach(node, menu->entries) {
		struct MenuEntry * entry = node->value;
		entry->hilight = 0;
	}
	menu->closed = 1;
	yutani_wid_t wid = menu->window->wid;
	yutani_close(menu->window->ctx, menu->window);
	menu->window = NULL;
	hashmap_remove(menu_windows, (void*)wid);

	return 0;
}

int menu_leave(struct MenuList * menu) {

	if (!hovered_menu) {
		while (menu->parent) {
			menu = menu->parent;
		}
		menu_definitely_close(menu);
		return 0;
	}

	if (!menu_has_eventual_child(menu, hovered_menu)) {
		/* Get all menus */
		list_t * menu_keys = hashmap_keys(menu_windows);
		foreach(_key, menu_keys) {
			yutani_window_t * window = hashmap_get(menu_windows, (void *)_key->value);
			if (window) {
				struct MenuList * menu = window->user_data;
				if (!hovered_menu || (menu != hovered_menu->child && !menu_has_eventual_child(menu, hovered_menu)))  {
					menu_definitely_close(menu);
					if (menu->parent && menu->parent->child == menu) {
						menu->parent->child = NULL;
					}
				}
			}
		}

		list_free(menu_keys);
		free(menu_keys);
	}

	return 0;
}

void menu_mouse_action(struct MenuList * menu, struct yutani_msg_window_mouse_event * me) {
	yutani_window_t * window = menu->window;
	yutani_t * yctx = window->ctx;

	int offset = 4;
	int changed = 0;
	foreach(node, menu->entries) {
		struct MenuEntry * entry = node->value;
		if (me->new_y >= offset && me->new_y < offset + entry->height &&
				me->new_x >= 0 && me->new_x < entry->width) {
			if (!entry->hilight) {
				changed = 1;
				entry->hilight = 1;
				entry->focus_change(entry, 1);
			}
			if (me->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(me)) {
				if (entry->activate) {
					entry->activate(entry, 0);
				}
			}
		} else {
			if (entry->hilight) {
				changed = 1;
				entry->hilight = 0;
				entry->focus_change(entry, 0);
			}
		}
		offset += entry->height;
	}
	if (changed) {
		_menu_redraw(window,yctx,menu);
	}
}

struct MenuList * menu_any_contains(int x, int y) {
	struct MenuList * out = NULL;
	list_t * menu_keys = hashmap_keys(menu_windows);
	foreach(_key, menu_keys) {
		yutani_window_t * window = hashmap_get(menu_windows, (void*)_key->value);
		if (window) {
			if (x >= (int)window->x && x < (int)window->x + (int)window->width && y >= (int)window->y && y < (int)window->y + (int)window->height) {
				out = window->user_data;
				break;

			}
		}
	}

	list_free(menu_keys);
	free(menu_keys);

	return out;
}

int menu_process_event(yutani_t * yctx, yutani_msg_t * m) {
	if (m) {
		switch (m->type) {
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					if (hashmap_has(menu_windows, (void*)me->wid)) {
						yutani_window_t * window = hashmap_get(menu_windows, (void *)me->wid);
						struct MenuList * menu = window->user_data;
						if (me->new_x >= 0 && me->new_x < (int)window->width && me->new_y >= 0 && me->new_y < (int)window->height) {
							if (hovered_menu != menu)  {
								hovered_menu = menu;
							}
						} else {
							if (hovered_menu) {
								struct MenuList * t = menu_any_contains(me->new_x + window->x, me->new_y + window->y);
								if (t) {
									hovered_menu = t;
								} else {
									hovered_menu = NULL;
								}
							}
						}
						menu_mouse_action(menu, me);

					}
				}
				break;
			case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
				{
					struct yutani_msg_window_focus_change * me = (void*)m->data;
					if (hashmap_has(menu_windows, (void*)me->wid)) {
						yutani_window_t * window = hashmap_get(menu_windows, (void *)me->wid);
						struct MenuList * menu = window->user_data;
						if (!me->focused) {
							/* XXX leave menu */
							menu_leave(menu);
							/* if root and not window.root.menus and window.root.focused */
							return 1;
						} else {
							window->focused = me->focused;
							/* Redraw? */
						}
					}
				}
				break;
			default:
				break;
		}
	}
	return 0;
}