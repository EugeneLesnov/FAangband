/*
 * File: ui-options.c
 * Purpose: Text UI options handling code (everything accessible from '=')
 *
 * Copyright (c) 1997-2000 Robert A. Koeneke, James E. Wilson, Ben Harrison
 * Copyright (c) 2007 Pete Mack
 * Copyright (c) 2010 Andi Sidwell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "angband.h"
#include "button.h"
#include "cmds.h"
#include "macro.h"
#include "squelch.h"
#include "prefs.h"
#include "tvalsval.h"
#include "ui-menu.h"
#include "files.h"




static void dump_pref_file(void (*dump)(ang_file *), const char *title, int row)
{
	char ftmp[80];
	char buf[1024];

	screen_save();

	/* Prompt */
	prt(format("%s to a pref file", title), row, 0);
	
	/* Prompt */
	prt("File: ", row + 2, 0);
	
	/* Default filename */
	strnfmt(ftmp, sizeof ftmp, "%s.prf", op_ptr->base_name);
	
	/* Get a filename */
	if (askfor_aux(ftmp, sizeof ftmp, NULL))
	{
		/* Build the filename */
		path_build(buf, sizeof(buf), ANGBAND_DIR_USER, ftmp);
	
		prt("", 0, 0);
		if (prefs_save(buf, dump, title))
			msg_print(format("Dumped %s", strstr(title, " ") + 1));
		else
			msg_print("Failed");
	}

	screen_load();

	return;
}

static void do_cmd_pref_file_hack(long row);






/*** Options display and setting ***/



/*** Boolean option menu code ***/

/**
 * Displays an option entry.
 */
static void option_toggle_display(menu_type *m, int oid, bool cursor,
		int row, int col, int width)
{
	byte attr = curs_attrs[CURS_KNOWN][cursor != 0];
	bool *options = menu_priv(m);

	c_prt(attr, format("%-45s: %s  (%s)", option_desc(oid),
			options[oid] ? "yes" : "no ", option_name(oid)), row, col);
}

/**
 * Handle keypresses for an option entry.
 */
static bool option_toggle_handle(menu_type *m, const ui_event_data *event,
		int oid)
{
	bool next = FALSE;

	if (event->type == EVT_SELECT) {
		option_set(option_name(oid), !op_ptr->opt[oid]);
	} else if (event->type == EVT_KBRD) {
		if (event->key == 'y' || event->key == 'Y') {
			option_set(option_name(oid), TRUE);
			next = TRUE;
		} else if (event->key == 'n' || event->key == 'N') {
			option_set(option_name(oid), FALSE);
			next = TRUE;
		} else if (event->key == '?') {
			screen_save();
			show_file(format("option.txt#%s", option_name(oid)), NULL, 0, 0);
			screen_load();
		} else {
			return FALSE;
		}
	} else {
		return FALSE;
	}

	if (next) {
		m->cursor++;
		m->cursor = (m->cursor + m->filter_count) % m->filter_count;
	}

	return TRUE;
}

/** Toggle option menu display and handling functions */
static const menu_iter option_toggle_iter = {
	NULL,
	NULL,
	option_toggle_display,
	option_toggle_handle,
	NULL
};


/**
 * Interact with some options
 */
static void option_toggle_menu(const char *name, int page)
{
	int i;
	
	menu_type *m = menu_new(MN_SKIN_SCROLL, &option_toggle_iter);

	/* for all menus */
	m->prompt = "Set option (y/n/t), '?' for information";
	m->cmd_keys = "?YyNnTt";
	m->selections = "abcdefghijklmopqrsuvwxz";
	m->flags = MN_DBL_TAP;

	/* for this particular menu */
	m->title = name;

	/* Find the number of valid entries */
	for (i = 0; i < OPT_PAGE_PER; i++) {
		if (option_page[page][i] == OPT_NONE)
			break;
	}

	/* Set the data to the player's options */
	menu_setpriv(m, OPT_MAX, &op_ptr->opt);
	menu_set_filter(m, option_page[page], i);
	menu_layout(m, &SCREEN_REGION);

	/* Run the menu */
	screen_save();

	clear_from(0);
	menu_select(m, 0);

	screen_load();

	mem_free(m);
}


/*
 * Modify the "window" options
 */
static void do_cmd_options_win(const char *name, int row)
{
	int i, j, d;

	int y = 0;
	int x = 0;

	ui_event_data ke;

	u32b new_flags[ANGBAND_TERM_MAX];


  u32b old_flag[ANGBAND_TERM_MAX];
  
  
  /* Memorize old flags */
  for (j = 0; j < ANGBAND_TERM_MAX; j++)
    {
      old_flag[j] = op_ptr->window_flag[j];
    }
  
  
	/* Set new flags to the old values */
	for (j = 0; j < ANGBAND_TERM_MAX; j++)
	{
		new_flags[j] = op_ptr->window_flag[j];
	}


	/* Clear screen */
	screen_save();
	clear_from(0);

	/* Interact */
	while (1)
	{
		/* Prompt */
		prt("Window flags (<dir> to move, 't'/Enter to toggle, or ESC)", 0, 0);

		/* Display the windows */
		for (j = 0; j < ANGBAND_TERM_MAX; j++)
		{
			byte a = TERM_WHITE;

			cptr s = angband_term_name[j];

			/* Use color */
			if (j == x) a = TERM_L_BLUE;

			/* Window name, staggered, centered */
			Term_putstr(35 + j * 5 - strlen(s) / 2, 2 + j % 2, -1, a, s);
		}

		/* Display the options */
		for (i = 0; i < PW_MAX_FLAGS; i++)
		{
			byte a = TERM_WHITE;

			cptr str = window_flag_desc[i];

			/* Use color */
			if (i == y) a = TERM_L_BLUE;

			/* Unused option */
			if (!str) str = "(Unused option)";

			/* Flag name */
			Term_putstr(0, i + 5, -1, a, str);

			/* Display the windows */
			for (j = 0; j < ANGBAND_TERM_MAX; j++)
			{
				char c = '.';

				a = TERM_WHITE;

				/* Use color */
				if ((i == y) && (j == x)) a = TERM_L_BLUE;

				/* Active flag */
				if (new_flags[j] & (1L << i)) c = 'X';

				/* Flag value */
				Term_putch(35 + j * 5, i + 5, a, c);
			}
		}

		/* Place Cursor */
		Term_gotoxy(35 + x * 5, y + 5);

		/* Get key */
		ke = inkey_ex();

		/* Allow escape */
		if ((ke.key == ESCAPE) || (ke.key == 'q')) break;

		/* Mouse interaction */
		if (ke.type == EVT_MOUSE)
		{
			int choicey = ke.mousey - 5;
			int choicex = (ke.mousex - 35)/5;

			if ((choicey >= 0) && (choicey < PW_MAX_FLAGS)
				&& (choicex > 0) && (choicex < ANGBAND_TERM_MAX)
				&& !(ke.mousex % 5))
			{
				y = choicey;
				x = (ke.mousex - 35)/5;
			}
		}

		/* Toggle */
		else if ((ke.key == '5') || (ke.key == 't') ||
				(ke.key == '\n') || (ke.key == '\r') ||
				(ke.type == EVT_MOUSE))
		{
			/* Hack -- ignore the main window */
			if (x == 0)
			{
				bell("Cannot set main window flags!");
			}

			/* Toggle flag (off) */
			else if (new_flags[x] & (1L << y))
			{
				new_flags[x] &= ~(1L << y);
			}

			/* Toggle flag (on) */
			else
			{
				new_flags[x] |= (1L << y);
			}

			/* Continue */
			continue;
		}

		/* Extract direction */
		d = target_dir(ke.key);

		/* Move */
		if (d != 0)
		{
			x = (x + ddx[d] + 8) % ANGBAND_TERM_MAX;
			y = (y + ddy[d] + 16) % PW_MAX_FLAGS;
		}

		/* Oops */
		else
		{
			bell("Illegal command for window options!");
		}
	}

  /* Notice changes */
  for (j = 0; j < ANGBAND_TERM_MAX; j++)
    {
      term *old = Term;
      
      /* Dead window */
      if (!angband_term[j]) continue;
      
      /* Ignore non-changes */
      if (op_ptr->window_flag[j] == old_flag[j]) continue;
      
      /* Activate */
      Term_activate(angband_term[j]);
      
      /* Erase */
      Term_clear();
      
      /* Refresh */
      Term_fresh();
      
      /* Restore */
      Term_activate(old);
    }
	screen_load();
}



/*** Interact with macros and keymaps ***/

#ifdef ALLOW_MACROS

/*
 * Hack -- ask for a "trigger" (see below)
 *
 * Note the complex use of the "inkey()" function from "util.c".
 *
 * Note that both "flush()" calls are extremely important.  This may
 * no longer be true, since "util.c" is much simpler now.  XXX XXX XXX
 */
static void do_cmd_macro_aux(char *buf)
{
	ui_event_data e;

	int n = 0;
	int curs_x, curs_y;

	char tmp[1024] = "";

	/* Get cursor position */
	Term_locate(&curs_x, &curs_y);

	/* Flush */
	flush();


	/* Do not process macros */
	inkey_base = TRUE;

	/* First key */
	e = inkey_ex();

	/* Read the pattern */
	while (e.key != 0 && e.type != EVT_MOUSE)
	{
		/* Save the key */
		buf[n++] = e.key;
		buf[n] = 0;

		/* Get representation of the sequence so far */
		ascii_to_text(tmp, sizeof(tmp), buf);

		/* Echo it after the prompt */
		Term_erase(curs_x, curs_y, 80);
		Term_gotoxy(curs_x, curs_y);
		Term_addstr(-1, TERM_WHITE, tmp);
		
		/* Do not process macros */
		inkey_base = TRUE;

		/* Do not wait for keys */
		inkey_scan = SCAN_INSTANT;

		/* Attempt to read a key */
		e = inkey_ex();
	}

	/* Convert the trigger */
	ascii_to_text(tmp, sizeof(tmp), buf);
}


/*
 * Ask for, and display, a keymap trigger.
 *
 * Returns the trigger input.
 *
 * Note that both "flush()" calls are extremely important.  This may
 * no longer be true, since "util.c" is much simpler now.  XXX XXX XXX
 */
static char keymap_get_trigger(void)
{
	char tmp[80];
	char buf[2];

	/* Flush */
	flush();

	/* Get a key */
	buf[0] = inkey();
	buf[1] = '\0';

	/* Convert to ascii */
	ascii_to_text(tmp, sizeof(tmp), buf);

	/* Hack -- display the trigger */
	Term_addstr(-1, TERM_WHITE, tmp);

	/* Flush */
	flush();

	/* Return trigger */
	return buf[0];
}


/*
 * Macro menu action functions
 */

static void macro_pref_load(const char *title, int row)
{
	do_cmd_pref_file_hack(16);
}

static void macro_pref_append(const char *title, int row)
{
	(void)dump_pref_file(macro_dump, "Dump macros", 15);
}

static void macro_query(const char *title, int row)
{
	int k;
	char buf[1024];
	
	prt("Command: Query a macro", 16, 0);
	prt("Trigger: ", 18, 0);
	
	/* Get a macro trigger */
	do_cmd_macro_aux(buf);
	
	/* Get the action */
	k = macro_find_exact(buf);
	
	/* Nothing found */
	if (k < 0)
	{
		/* Prompt */
		prt("", 0, 0);
		msg_print("Found no macro.");
	}
	
	/* Found one */
	else
	{
		/* Obtain the action */
		my_strcpy(macro_buffer, macro__act[k], sizeof(macro_buffer));
	
		/* Analyze the current action */
		ascii_to_text(buf, sizeof(buf), macro_buffer);
	
		/* Display the current action */
		prt(buf, 22, 0);
	
		/* Prompt */
		prt("", 0, 0);
		msg_print("Found a macro.");
	}
}

static void macro_create(const char *title, int row)
{
	char pat[1024];
	char tmp[1024];

	prt("Command: Create a macro", 16, 0);
	prt("Trigger: ", 18, 0);
	
	/* Get a macro trigger */
	do_cmd_macro_aux(pat);
	
	/* Clear */
	clear_from(20);
	
	/* Prompt */
	prt("Action: ", 20, 0);
	
	/* Convert to text */
	ascii_to_text(tmp, sizeof(tmp), macro_buffer);
	
	/* Get an encoded action */
	if (askfor_aux(tmp, sizeof tmp, NULL))
	{
		/* Convert to ascii */
		text_to_ascii(macro_buffer, sizeof(macro_buffer), tmp);
		
		/* Link the macro */
		macro_add(pat, macro_buffer);
		
		/* Prompt */
		prt("", 0, 0);
		msg_print("Added a macro.");
	}					
}

static void macro_remove(const char *title, int row)
{
	char pat[1024];

	prt("Command: Remove a macro", 16, 0);
	prt("Trigger: ", 18, 0);
	
	/* Get a macro trigger */
	do_cmd_macro_aux(pat);
	
	/* Link the macro */
	macro_add(pat, pat);
	
	/* Prompt */
	prt("", 0, 0);
	msg_print("Removed a macro.");
}

static void keymap_pref_append(const char *title, int row)
{
	(void)dump_pref_file(keymap_dump, "Dump keymaps", 13);
}

static void keymap_query(const char *title, int row)
{
	char tmp[1024];
	int mode = OPT(rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
	char c;
	const char *act;

	prt(title, 13, 0);
	prt("Key: ", 14, 0);
	
	/* Get a keymap trigger & mapping */
	c = keymap_get_trigger();
	act = keymap_act[mode][(byte) c];
	
	/* Nothing found */
	if (!act)
	{
		/* Prompt */
		prt("No keymap with that trigger.  Press any key to continue.", 16, 0);
		inkey();
	}
	
	/* Found one */
	else
	{
		/* Obtain the action */
		my_strcpy(macro_buffer, act, sizeof(macro_buffer));
	
		/* Analyze the current action */
		ascii_to_text(tmp, sizeof(tmp), macro_buffer);
	
		/* Display the current action */
		prt("Found: ", 15, 0);
		Term_addstr(-1, TERM_WHITE, tmp);

		prt("Press any key to continue.", 17, 0);
		inkey();
	}
}

static void keymap_create(const char *title, int row)
{
	char c;
	char tmp[1024];
	int mode = OPT(rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;

	prt(title, 13, 0);
	prt("Key: ", 14, 0);

	c = keymap_get_trigger();

	prt("Action: ", 15, 0);

	/* Get an encoded action, with a default response */
	ascii_to_text(tmp, sizeof(tmp), macro_buffer);
	if (askfor_aux(tmp, sizeof tmp, NULL))
	{
		/* Convert to ascii */
		text_to_ascii(macro_buffer, sizeof(macro_buffer), tmp);
	
		/* Make new keymap */
		string_free(keymap_act[mode][(byte) c]);
		keymap_act[mode][(byte) c] = string_make(macro_buffer);

		/* Prompt */
		prt("Keymap added.  Press any key to continue.", 17, 0);
		inkey();
	}
}

static void keymap_remove(const char *title, int row)
{
	char c;
	int mode = OPT(rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;

	prt(title, 13, 0);
	prt("Key: ", 14, 0);

	c = keymap_get_trigger();

	if (keymap_act[mode][(byte) c])
	{
		/* Free old keymap */
		string_free(keymap_act[mode][(byte) c]);
		keymap_act[mode][(byte) c] = NULL;

		prt("Removed.", 16, 0);
	}
	else
	{
		prt("No keymap to remove!", 16, 0);
	}

	/* Prompt */
	prt("Press any key to continue.", 17, 0);
	inkey();
}

static void macro_enter(const char *title, int row)
{
	char tmp[1024];

	prt(title, 16, 0);
	prt("Action: ", 17, 0);

	/* Get an action, with a default response */
	ascii_to_text(tmp, sizeof(tmp), macro_buffer);
	if (askfor_aux(tmp, sizeof tmp, NULL))
	{
		/* Save to global macro buffer */
		text_to_ascii(macro_buffer, sizeof(macro_buffer), tmp);
	}
}

static void macro_browse_hook(int oid, void *db, const region *loc)
{
	char tmp[1024];

	message_flush();

	clear_from(13);

	/* Show current action */
	prt("Current action (if any) shown below:", 13, 0);
	ascii_to_text(tmp, sizeof(tmp), macro_buffer);
	prt(tmp, 14, 0);
}

static menu_type *macro_menu;
static menu_action macro_actions[] =
{
	{ 0, 0, "Load a user pref file",    macro_pref_load },
	{ 0, 0, "Append macros to a file",  macro_pref_append },
	{ 0, 0, "Query a macro",            macro_query },
	{ 0, 0, "Create a macro",           macro_create },
	{ 0, 0, "Remove a macro",           macro_remove },
	{ 0, 0, "Append keymaps to a file", keymap_pref_append },
	{ 0, 0, "Query a keymap",           keymap_query },
	{ 0, 0, "Create a keymap",          keymap_create },
	{ 0, 0, "Remove a keymap",          keymap_remove },
	{ 0, 0, "Enter a new action",       macro_enter },
};

static void do_cmd_macros(const char *title, int row)
{
	region loc = {0, 0, 0, 12};

	screen_save();
	clear_from(0);

	if (!macro_menu)
	{
		macro_menu = menu_new_action(macro_actions,
				N_ELEMENTS(macro_actions));
	
		macro_menu->title = title;
		macro_menu->selections = lower_case;
		macro_menu->browse_hook = macro_browse_hook;
	}

	menu_layout(macro_menu, &loc);
	menu_select(macro_menu, 0);

	screen_load();
}

#endif /* ALLOW_MACROS */



/*** Interact with visuals ***/

static void visuals_pref_load(const char *title, int row)
{
	do_cmd_pref_file_hack(15);
}

#ifdef ALLOW_VISUALS

static void visuals_dump_monsters(const char *title, int row)
{
	dump_pref_file(dump_monsters, title, 15);
}

static void visuals_dump_objects(const char *title, int row)
{
	dump_pref_file(dump_objects, title, 15);
}

static void visuals_dump_features(const char *title, int row)
{
	dump_pref_file(dump_features, title, 15);
}

static void visuals_dump_flavors(const char *title, int row)
{
	dump_pref_file(dump_flavors, title, 15);
}

#endif /* ALLOW_VISUALS */

static void visuals_reset(const char *title, int row)
{
	/* Reset */
	reset_visuals(TRUE);

	/* Message */
	prt("", 0, 0);
	msg_print("Visual attr/char tables reset.");
	message_flush();
}


static menu_type *visual_menu;
static menu_action visual_menu_items [] =
{
	{ 0, 0, "Load a user pref file",   visuals_pref_load },
#ifdef ALLOW_VISUALS
	{ 0, 0, "Dump monster attr/chars", visuals_dump_monsters },
	{ 0, 0, "Dump object attr/chars",  visuals_dump_objects },
	{ 0, 0, "Dump feature attr/chars", visuals_dump_features },
	{ 0, 0, "Dump flavor attr/chars",  visuals_dump_flavors },
#endif /* ALLOW_VISUALS */
	{ 0, 0, "Reset visuals",           visuals_reset },
};


static void visuals_browse_hook(int oid, void *db, const region *loc)
{
	message_flush();
	clear_from(1);
}


/*
 * Interact with "visuals"
 */
static void do_cmd_visuals(const char *title, int row)
{
	screen_save();
	clear_from(0);

	if (!visual_menu)
	{
		visual_menu = menu_new_action(visual_menu_items,
				N_ELEMENTS(visual_menu_items));

		visual_menu->title = title;
		visual_menu->selections = lower_case;
		visual_menu->browse_hook = visuals_browse_hook;
		visual_menu->header = "To edit visuals, use the knowledge menu";
	}

	menu_layout(visual_menu, &SCREEN_REGION);
	menu_select(visual_menu, 0);

	screen_load();
}


/*** Interact with colours ***/

#ifdef ALLOW_COLORS

static void colors_pref_load(const char *title, int row)
{
	/* Ask for and load a user pref file */
	do_cmd_pref_file_hack(8);
	
	/* XXX should probably be a cleaner way to tell UI about
	 * colour changes - how about doing this in the pref file
	 * loading code too? */
	Term_xtra(TERM_XTRA_REACT, 0);
	Term_redraw();
}

static void colors_pref_dump(const char *title, int row)
{
	dump_pref_file(dump_colors, title, 15);
}

static void colors_modify(const char *title, int row)
{
	int i;
	int cx;

	static byte a = 0;

	/* Prompt */
	prt("Command: Modify colors", 8, 0);

	/* Hack -- query until done */
	while (1)
	{
		cptr name;
		char index;

		/* Clear */
		clear_from(10);

		/* Exhibit the normal colors */
		for (i = 0; i < BASIC_COLORS; i++)
		{
			/* Exhibit this color */
			Term_putstr(i*3, 20, -1, a, "##");

			/* Exhibit character letter */
			Term_putstr(i*3, 21, -1, (byte)i,
						format(" %c", color_table[i].index_char));

			/* Exhibit all colors */
			Term_putstr(i*3, 22, -1, (byte)i, format("%2d", i));
		}

		/* Describe the color */
		name = ((a < BASIC_COLORS) ? color_table[a].name : "undefined");
		index = ((a < BASIC_COLORS) ? color_table[a].index_char : '?');

		/* Describe the color */
		Term_putstr(5, 10, -1, TERM_WHITE,
					format("Color = %d, Name = %s, Index = %c", a, name, index));

		/* Label the Current values */
		Term_putstr(5, 12, -1, TERM_WHITE,
				format("K = 0x%02x / R,G,B = 0x%02x,0x%02x,0x%02x",
				   angband_color_table[a][0],
				   angband_color_table[a][1],
				   angband_color_table[a][2],
				   angband_color_table[a][3]));

		/* Prompt */
		Term_putstr(0, 14, -1, TERM_WHITE,
				"Command (n/N/k/K/r/R/g/G/b/B): ");

		/* Get a command */
		cx = inkey();

		/* All done */
		if (cx == ESCAPE) break;

		/* Analyze */
		if (cx == 'n') a = (byte)(a + 1);
		if (cx == 'N') a = (byte)(a - 1);
		if (cx == 'k') angband_color_table[a][0] = (byte)(angband_color_table[a][0] + 1);
		if (cx == 'K') angband_color_table[a][0] = (byte)(angband_color_table[a][0] - 1);
		if (cx == 'r') angband_color_table[a][1] = (byte)(angband_color_table[a][1] + 1);
		if (cx == 'R') angband_color_table[a][1] = (byte)(angband_color_table[a][1] - 1);
		if (cx == 'g') angband_color_table[a][2] = (byte)(angband_color_table[a][2] + 1);
		if (cx == 'G') angband_color_table[a][2] = (byte)(angband_color_table[a][2] - 1);
		if (cx == 'b') angband_color_table[a][3] = (byte)(angband_color_table[a][3] + 1);
		if (cx == 'B') angband_color_table[a][3] = (byte)(angband_color_table[a][3] - 1);

		/* Hack -- react to changes */
		Term_xtra(TERM_XTRA_REACT, 0);

		/* Hack -- redraw */
		Term_redraw();
	}
}

static void colors_browse_hook(int oid, void *db, const region *loc)
{
	message_flush();
	clear_from(1);
}


static menu_type *color_menu;
static menu_action color_events [] =
{
	{ 0, 0, "Load a user pref file", colors_pref_load },
	{ 0, 0, "Dump colors",           colors_pref_dump },
	{ 0, 0, "Modify colors",         colors_modify }
};

/*
 * Interact with "colors"
 */
void do_cmd_colors(const char *title, int row)
{
	screen_save();
	clear_from(0);

	if (!color_menu)
	{
		color_menu = menu_new_action(color_events,
			N_ELEMENTS(color_events));

		color_menu->title = title;
		color_menu->selections = lower_case;
		color_menu->browse_hook = colors_browse_hook;
	}

	menu_layout(color_menu, &SCREEN_REGION);
	menu_select(color_menu, 0);

	screen_load();
}

#endif


/*** Non-complex menu actions ***/

static bool askfor_aux_numbers(char *buf, size_t buflen, size_t *curs, size_t *len, char keypress, bool firsttime)
{
	switch (keypress)
	{
		case ESCAPE:
		case '\n':
		case '\r':
		case ARROW_LEFT:
		case ARROW_RIGHT:
		case 0x7F:
		case '\010':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			return askfor_aux_keypress(buf, buflen, curs, len, keypress, firsttime);
	}

	return FALSE;
}


/*
 * Set base delay factor
 */
static void do_cmd_delay(const char *name, int row)
{
	bool res;
	char tmp[4] = "";
	int msec = op_ptr->delay_factor * op_ptr->delay_factor;

	strnfmt(tmp, sizeof(tmp), "%i", op_ptr->delay_factor);

	screen_save();

	/* Prompt */
	prt("Command: Base Delay Factor", 20, 0);

	prt(format("Current base delay factor: %d (%d msec)",
			   op_ptr->delay_factor, msec), 22, 0);
	prt("New base delay factor (0-255): ", 21, 0);

	/* Ask the user for a string */
	res = askfor_aux(tmp, sizeof(tmp), askfor_aux_numbers);

	/* Process input */
	if (res)
	{
		op_ptr->delay_factor = (u16b) strtoul(tmp, NULL, 0);
	}

	screen_load();
}


/*
 * Set hitpoint warning level
 */
static void do_cmd_hp_warn(const char *name, int row)
{
	bool res;
	char tmp[4] = "";
	u16b warn;

	strnfmt(tmp, sizeof(tmp), "%i", op_ptr->hitpoint_warn);

	screen_save();

	/* Prompt */
	prt("Command: Hitpoint Warning", 20, 0);

	prt(format("Current hitpoint warning: %d (%d%%)",
			   op_ptr->hitpoint_warn, op_ptr->hitpoint_warn * 10), 22, 0);
	prt("New hitpoint warning (0-9): ", 21, 0);

	/* Ask the user for a string */
	res = askfor_aux(tmp, sizeof(tmp), askfor_aux_numbers);

	/* Process input */
	if (res)
	{
		warn = (u16b) strtoul(tmp, NULL, 0);
		
		/* Reset nonsensical warnings */
		if (warn > 9)
			warn = 0;

		op_ptr->hitpoint_warn = warn;
	}

	screen_load();
}


/** Hack -- hitpoint warning factor */
void do_cmd_panel_change(const char *name, int row)
{
  /* Prompt */
  prt("Command: Panel Change", 20, 0);
  button_add("+", '+');      
  button_add("-", '-');      
  
  /* Get a new value */
  while (1)
    {
      int pdist = (op_ptr->panel_change + 1) * 2;
      ui_event_data ke;
      prt(format("Current panel change: %d (%d / %d)",
                 op_ptr->panel_change, pdist, pdist * 2), 22, 0);
      prt("New panel change (0-4, +, - or ESC to accept): ", 21, 0);

      ke = inkey_ex();
      if (ke.key == ESCAPE) break;
      if (isdigit(ke.key)) op_ptr->panel_change = D2I(ke.key);
      if (ke.key == '+') op_ptr->panel_change++;
      if (ke.key == '-') op_ptr->panel_change--;
      if (op_ptr->panel_change > 4) op_ptr->panel_change = 4;
      if (op_ptr->panel_change < 0) op_ptr->panel_change = 0;
    }

  button_kill('+');      
  button_kill('-');
}



/**
 * Set "lazy-movement" delay
 */
static void do_cmd_lazymove_delay(const char *name, int row)
{
	bool res;
	char tmp[4] = "";

	strnfmt(tmp, sizeof(tmp), "%i", lazymove_delay);

	screen_save();

	/* Prompt */
	prt("Command: Movement Delay Factor", 20, 0);

	prt(format("Current movement delay: %d (%d msec)",
			   lazymove_delay, lazymove_delay * 10), 22, 0);
	prt("New movement delay: ", 21, 0);

	/* Ask the user for a string */
	res = askfor_aux(tmp, sizeof(tmp), askfor_aux_numbers);

	/* Process input */
	if (res)
	{
		lazymove_delay = (u16b) strtoul(tmp, NULL, 0);
	}

	screen_load();
}



/*
 * Ask for a "user pref file" and process it.
 *
 * This function should only be used by standard interaction commands,
 * in which a standard "Command:" prompt is present on the given row.
 *
 * Allow absolute file names?  XXX XXX XXX
 */
static void do_cmd_pref_file_hack(long row)
{
	char ftmp[80];

	screen_save();

	/* Prompt */
	prt("Command: Load a user pref file", row, 0);

	/* Prompt */
	prt("File: ", row + 2, 0);

	/* Default filename */
	strnfmt(ftmp, sizeof ftmp, "%s.prf", op_ptr->base_name);

	/* Ask for a file (or cancel) */
	if (askfor_aux(ftmp, sizeof ftmp, NULL))
	{
		/* Process the given filename */
		if (process_pref_file(ftmp, FALSE) == FALSE)
		{
			/* Mention failure */
			prt("", 0, 0);
			msg_format("Failed to load '%s'!", ftmp);
		}
		else
		{
			/* Mention success */
			prt("", 0, 0);
			msg_format("Loaded '%s'.", ftmp);
		}
	}

	screen_load();
}


/*
 * Write options to a file.
 */
static void do_dump_options(const char *title, int row)
{
	dump_pref_file(option_dump, "Dump options", 20);
}

/*
 * Load a pref file.
 */
static void options_load_pref_file(const char *n, int row)
{
	do_cmd_pref_file_hack(20);
}





/**
 * Autosave options -- textual names
 */
static cptr autosave_text[1] =
{
  "autosave"
};

/**
 * Autosave options -- descriptions
 */
static cptr autosave_desc[1] =
  {
    "Timed autosave"
  };

s16b toggle_frequency(s16b current)
{
  if (current == 0) return 50;
  if (current == 50) return 100;
  if (current == 100) return 250;
  if (current == 250) return 500;
  if (current == 500) return 1000;
  if (current == 1000) return 2500;
  if (current == 2500) return 5000;
  if (current == 5000) return 10000;
  if (current == 10000) return 25000;
  
  else return 0;
}


/**
 * Interact with autosave options.  From Zangband.
 */
static void do_cmd_options_autosave(const char *name, int row)
{
  ui_event_data ke;
  
  int i, k = 0, n = 1;
  
  char buf[80];
  
  
  /* Clear screen */
  Term_clear();
  
  /* Interact with the player */
  while (TRUE)
    {
      /* Prompt - return taken out as there's only one option... -NRM- */
      sprintf(buf, "Autosave options (y/n to set, 'F' for frequency, ESC to accept) ");
      prt(buf, 0, 0);
      
      /* Display the options */
      for (i = 0; i < n; i++)
        {
          byte a = TERM_WHITE;
          
          /* Color current option */
          if (i == k) a = TERM_L_BLUE;
          
          /* Display the option text */
	  sprintf(buf, "%-48s: %s  (%s)", autosave_desc[i],
		  autosave ? "yes" : "no ", autosave_text[i]);
          c_prt(a, buf, i + 2, 0);
          
          prt(format("Timed autosave frequency: every %d turns", 
                     autosave_freq), 5, 0);
        }
      
      
      /* Hilight current option */
      Term_gotoxy(50, k + 2);

      button_add("F", 'F');
      button_add("n", 'n');
      button_add("y", 'y');
      
      /* Get a key */
      ke = inkey_ex();
      
      /* Analyze */
      switch (ke.key)
        {
        case ESCAPE:
          {
	    button_kill('F');
	    button_kill('n');
	    button_kill('y');
            return;
          }
          
        case '-':
        case '8':
          {
            k = (n + k - 1) % n;
            break;
          }
          
        case ' ':
        case '\n':
        case '\r':
        case '2':
          {
            k = (k + 1) % n;
            break;
          }
          
        case 'y':
        case 'Y':
        case '6':
          {
            
            autosave = TRUE;
            k = (k + 1) % n;
            break;
          }
          
        case 'n':
        case 'N':
        case '4':
          {
            autosave = FALSE;
            k = (k + 1) % n;
            break;
          }
          
        case 'f':
        case 'F':
          {
            autosave_freq = toggle_frequency(autosave_freq);
            prt(format("Timed autosave frequency: every %d turns",
                       autosave_freq), 5, 0);
            break;
          }
          
        default:
          {
            bell("Illegal command for Autosave options!");
            break;
          }
        }
    }
}



/*** Main menu definitions and display ***/

static menu_type *option_menu;
static menu_action option_actions[] = 
{
	{ 0, 'a', "Interface options", option_toggle_menu },
	{ 0, 'b', "Display options", option_toggle_menu },
	{ 0, 'e', "Warning and disturbance options", option_toggle_menu },
	{ 0, 'f', "Birth (difficulty) options", option_toggle_menu },
	{ 0, 'g', "Cheat options", option_toggle_menu },
	{0, 0, 0, 0}, /* Load and append */
	{ 0, 'w', "Subwindow display settings", do_cmd_options_win },
	{ 0, 's', "Item squelch settings", do_cmd_options_item },
	{ 0, 'd', "Set base delay factor", do_cmd_delay },
	{ 0, 'h', "Set hitpoint warning", do_cmd_hp_warn },
	{ 0, 'p', "Set panel change factor", do_cmd_panel_change },
	{ 0, 'i', "Set movement delay", do_cmd_lazymove_delay },
	{ 0, 'l', "Load a user pref file", options_load_pref_file },
	{ 0, 'o', "Save options", do_dump_options }, 
	{ 0, 'x', "Autosave options", do_cmd_options_autosave },
	{0, 0, 0, 0}, /* Interact with */	

#ifdef ALLOW_MACROS
	{ 0, 'm', "Interact with macros (advanced)", do_cmd_macros },
#endif /* ALLOW_MACROS */

	{ 0, 'v', "Interact with visuals (advanced)", do_cmd_visuals },

#ifdef ALLOW_COLORS
	{ 0, 'c', "Interact with colours (advanced)", do_cmd_colors },
#endif /* ALLOW_COLORS */
};


/*
 * Display the options main menu.
 */
void do_cmd_options(void)
{
	if (!option_menu)
	{
		/* Main option menu */
		option_menu = menu_new_action(option_actions,
				N_ELEMENTS(option_actions));

		option_menu->title = "Options Menu";
		option_menu->flags = MN_CASELESS_TAGS;
	}


	screen_save();
	clear_from(0);

	menu_layout(option_menu, &SCREEN_REGION);
	menu_select(option_menu, 0);

	screen_load();
}










