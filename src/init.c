/*
 * File: init2.c
 * Purpose: Various game initialistion routines
 *
 * Copyright (c) 1997 Ben Harrison
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
#include "cave.h"
#include "cmds.h"
#include "game-cmd.h"
#include "game-event.h"
#include "init.h"
#include "macro.h"
#include "monster/constants.h"
#include "object/tvalsval.h"
#include "option.h"
#include "parser.h"
#include "prefs.h"
#include "squelch.h"

/*
 * This file is used to initialize various variables and arrays for the
 * Angband game.  Note the use of "fd_read()" and "fd_write()" to bypass
 * the common limitation of "read()" and "write()" to only 32767 bytes
 * at a time.
 *
 * Several of the arrays for Angband are built from "template" files in
 * the "lib/edit" directory.
 *
 * Warning -- the "ascii" file parsers use a minor hack to collect the
 * name and text information in a single pass.  Thus, the game will not
 * be able to load any template file with more than 20K of names or 60K
 * of text, even though technically, up to 64K should be legal.
 */

struct file_parser {
	const char *name;
	struct parser *(*init)(void);
	errr (*run)(struct parser *p);
	errr (*finish)(struct parser *p);
};

static void print_error(struct file_parser *fp, struct parser *p) {
	struct parser_state s;
	parser_getstate(p, &s);
	msg_format("Parse error in %s line %d column %d: %s: %s", fp->name,
	           s.line, s.col, s.msg, parser_error_str[s.error]);
	message_flush();
	quit_fmt("Parse error in %s line %d column %d.", fp->name, s.line, s.col);
}

errr run_parser(struct file_parser *fp) {
	struct parser *p = fp->init();
	errr r;
	if (!p) {
		return PARSE_ERROR_GENERIC;
	}
	r = fp->run(p);
	if (r) {
		print_error(fp, p);
		return r;
	}
	r = fp->finish(p);
	if (r)
		print_error(fp, p);
	return r;
}

static const char *object_flags[] = {
	#define OF(a, b) #a,
	#include "list-object-flags.h"
	#undef OF
	NULL
};

static const char *curse_flags[] = {
	#define CF(a, b) #a,
	#include "list-curse-flags.h"
	#undef CF
	NULL
};

static const char *kind_flags[] = {
	#define KF(a, b) #a,
	#include "list-kind-flags.h"
	#undef KF
	NULL
};

static const char *id_flags[] = {
	#define IF(a, b) #a,
	#include "list-identify-flags.h"
	#undef IF
	NULL
};

static const char *effect_list[] = {
	#define EFFECT(x, y, r, z)    #x,
	#include "list-effects.h"
	#undef EFFECT
};

/**
 * Percentage resists
 */
static const char *player_resist_values[] = 
  {
    "RES_ACID",
    "RES_ELEC",
    "RES_FIRE",
    "RES_COLD",
    "RES_POIS",
    "RES_LITE",
    "RES_DARK",
    "RES_CONFU",
    "RES_SOUND",
    "RES_SHARD",
    "RES_NEXUS",
    "RES_NETHR",
    "RES_CHAOS",
    "RES_DISEN"
  };

/**
 * Stat bonuses
 */
static const char *bonus_stat_values[] =
  {
    "STR",
    "INT",
    "WIS",
    "DEX",
    "CON",
    "CHR"
  };

/**
 * Other bonuses
 */
static const char *bonus_other_values[] =
  {
    "MAGIC_MASTERY",
    "STEALTH",
    "SEARCH",
    "INFRA",
    "TUNNEL",
    "SPEED",
    "SHOTS",
    "MIGHT"
  };

/**
 * Slays
 */
static const char *slay_values[] = 
  {
    "SLAY_ANIMAL",
    "SLAY_EVIL",
    "SLAY_UNDEAD",
    "SLAY_DEMON",
    "SLAY_ORC",
    "SLAY_TROLL",
    "SLAY_GIANT",
    "SLAY_DRAGON"
  };

/**
 * Brands
 */
static const char *brand_values[] = 
  {
    "BRAND_ACID",
    "BRAND_ELEC",
    "BRAND_FIRE",
    "BRAND_COLD",
    "BRAND_POIS"
  };

static int lookup_flag(const char **flag_table, const char *flag_name) {
	int i = FLAG_START;

	while (flag_table[i] && !streq(flag_table[i], flag_name))
		i++;

	/* End of table reached without match */
	if (!flag_table[i]) i = FLAG_END;

	return i;
}

static errr grab_flag(bitflag *flags, const size_t size, const char **flag_table, const char *flag_name) {
	int flag = lookup_flag(flag_table, flag_name);

	if (flag == FLAG_END) return PARSE_ERROR_INVALID_FLAG;

	flag_on(flags, size, flag);

	return 0;
}

static u32b grab_one_effect(const char *what) {
	size_t i;

	/* Scan activations */
	for (i = 0; i < N_ELEMENTS(effect_list); i++)
	{
		if (streq(what, effect_list[i]))
			return i;
	}

	/* Oops */
	msg_format("Unknown effect '%s'.", what);

	/* Error */
	return 0;
}

static u32b grab_value(const char *what, const char *value_type, int *val) {
	size_t i;
	char *s;
	char *t;

	/* Parse the string */
	for (s = what; *s; )
	{
	        /* Find the first bracket */
	        for (t = s; *t && (*t != '['); ++t) /* loop */;
	  
		/* Get the value */
		if (1 != sscanf(t + 1, "%d", val))
		        return (PARSE_ERROR_INVALID_VALUE);

		/* Terminate the string */
		*t = '\0';
	}
  
	/* Check the possibilities */
	for (i = 0; i < N_ELEMENTS(value_type); i++)
	{
		if (streq(what, value_type[i]))
			return i;
	}

	/* Not found */
	return (0);
}

/*
 * Find the default paths to all of our important sub-directories.
 *
 * All of the sub-directories should, by default, be located inside
 * the main directory, whose location is very system dependant and is 
 * set by the ANGBAND_PATH environment variable, if it exists. (On multi-
 * user systems such as Linux this is not the default - see config.h for
 * more info.)
 *
 * This function takes a writable buffers, initially containing the
 * "path" to the "config", "lib" and "data" directories, for example, 
 * "/etc/angband/", "/usr/share/angband" and "/var/games/angband" -
 * or a system dependant string, for example, ":lib:".  The buffer
 * must be large enough to contain at least 32 more characters.
 *
 * Various command line options may allow some of the important
 * directories to be changed to user-specified directories, most
 * importantly, the "apex" and "user" and "save" directories,
 * but this is done after this function, see "main.c".
 *
 * In general, the initial path should end in the appropriate "PATH_SEP"
 * string.  All of the "sub-directory" paths (created below or supplied
 * by the user) will NOT end in the "PATH_SEP" string, see the special
 * "path_build()" function in "util.c" for more information.
 *
 * Hack -- first we free all the strings, since this is known
 * to succeed even if the strings have not been allocated yet,
 * as long as the variables start out as "NULL".  This allows
 * this function to be called multiple times, for example, to
 * try several base "path" values until a good one is found.
 */
void init_file_paths(const char *configpath, const char *libpath, const char *datapath)
{
#ifdef PRIVATE_USER_PATH
	char buf[1024];
#endif /* PRIVATE_USER_PATH */

	/*** Free everything ***/

	/* Free the sub-paths */
	string_free(ANGBAND_DIR_APEX);
	string_free(ANGBAND_DIR_EDIT);
	string_free(ANGBAND_DIR_FILE);
	string_free(ANGBAND_DIR_HELP);
	string_free(ANGBAND_DIR_INFO);
	string_free(ANGBAND_DIR_SAVE);
	string_free(ANGBAND_DIR_PREF);
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_XTRA);

	string_free(ANGBAND_DIR_XTRA_FONT);
	string_free(ANGBAND_DIR_XTRA_GRAF);
	string_free(ANGBAND_DIR_XTRA_SOUND);
	string_free(ANGBAND_DIR_XTRA_HELP);
	string_free(ANGBAND_DIR_XTRA_ICON);

	/*** Prepare the paths ***/

	/* Build path names */
	ANGBAND_DIR_EDIT = string_make(format("%sedit", configpath));
	ANGBAND_DIR_FILE = string_make(format("%sfile", libpath));
	ANGBAND_DIR_HELP = string_make(format("%shelp", libpath));
	ANGBAND_DIR_INFO = string_make(format("%sinfo", libpath));
	ANGBAND_DIR_PREF = string_make(format("%spref", configpath));
	ANGBAND_DIR_XTRA = string_make(format("%sxtra", libpath));

	/* Build xtra/ paths */
	ANGBAND_DIR_XTRA_FONT = string_make(format("%s" PATH_SEP "font", ANGBAND_DIR_XTRA));
	ANGBAND_DIR_XTRA_GRAF = string_make(format("%s" PATH_SEP "graf", ANGBAND_DIR_XTRA));
	ANGBAND_DIR_XTRA_SOUND = string_make(format("%s" PATH_SEP "sound", ANGBAND_DIR_XTRA));
	ANGBAND_DIR_XTRA_HELP = string_make(format("%s" PATH_SEP "help", ANGBAND_DIR_XTRA));
	ANGBAND_DIR_XTRA_ICON = string_make(format("%s" PATH_SEP "icon", ANGBAND_DIR_XTRA));

#ifdef PRIVATE_USER_PATH

	/* Build the path to the user specific directory */
	if (strncmp(ANGBAND_SYS, "test", 4) == 0)
		path_build(buf, sizeof(buf), PRIVATE_USER_PATH, "Test");
	else
		path_build(buf, sizeof(buf), PRIVATE_USER_PATH, VERSION_NAME);
	ANGBAND_DIR_USER = string_make(buf);

	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, "scores");
	ANGBAND_DIR_APEX = string_make(buf);

	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, "save");
	ANGBAND_DIR_SAVE = string_make(buf);

#else /* !PRIVATE_USER_PATH */

	/* Build pathnames */
    ANGBAND_DIR_USER = string_make(format("%suser", datapath));
	ANGBAND_DIR_APEX = string_make(format("%sapex", datapath));
	ANGBAND_DIR_SAVE = string_make(format("%ssave", datapath));

#endif /* PRIVATE_USER_PATH */
}


/*
 * Create any missing directories. We create only those dirs which may be
 * empty (user/, save/, apex/, info/, help/). The others are assumed 
 * to contain required files and therefore must exist at startup 
 * (edit/, pref/, file/, xtra/).
 *
 * ToDo: Only create the directories when actually writing files.
 */
void create_needed_dirs(void)
{
	char dirpath[512];

	path_build(dirpath, sizeof(dirpath), ANGBAND_DIR_USER, "");
	if (!dir_create(dirpath)) quit_fmt("Cannot create '%s'", dirpath);

	path_build(dirpath, sizeof(dirpath), ANGBAND_DIR_SAVE, "");
	if (!dir_create(dirpath)) quit_fmt("Cannot create '%s'", dirpath);

	path_build(dirpath, sizeof(dirpath), ANGBAND_DIR_APEX, "");
	if (!dir_create(dirpath)) quit_fmt("Cannot create '%s'", dirpath);

	path_build(dirpath, sizeof(dirpath), ANGBAND_DIR_INFO, "");
	if (!dir_create(dirpath)) quit_fmt("Cannot create '%s'", dirpath);

	path_build(dirpath, sizeof(dirpath), ANGBAND_DIR_HELP, "");
	if (!dir_create(dirpath)) quit_fmt("Cannot create '%s'", dirpath);
}

errr parse_file(struct parser *p, const char *filename) {
	char path[1024];
	char buf[1024];
	ang_file *fh;
	errr r = 0;

	path_build(path, sizeof(path), ANGBAND_DIR_EDIT, format("%s.txt", filename));
	fh = file_open(path, MODE_READ, -1);
	if (!fh)
		quit(format("Cannot open '%s.txt'", filename));
	while (file_getl(fh, buf, sizeof(buf))) {
		r = parser_parse(p, buf);
		if (r)
			break;
	}
	file_close(fh);
	return r;
}

static enum parser_error ignored(struct parser *p) {
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_z(struct parser *p) {
	maxima *z;
	const char *label;
	int value;

	z = parser_priv(p);
	label = parser_getsym(p, "label");
	value = parser_getint(p, "value");

	if (value < 0)
		return PARSE_ERROR_INVALID_VALUE;

	if (streq(label, "F"))
		z->f_max = value;
	else if (streq(label, "K"))
		z->k_max = value;
	else if (streq(label, "A"))
		z->a_max = value;
	else if (streq(label, "E"))
		z->e_max = value;
	else if (streq(label, "R"))
		z->r_max = value;
	else if (streq(label, "V"))
		z->v_max = value;
	else if (streq(label, "P"))
		z->p_max = value;
	else if (streq(label, "C"))
		z->c_max = value;
	else if (streq(label, "H"))
		z->h_max = value;
	else if (streq(label, "B"))
		z->b_max = value;
	else if (streq(label, "S"))
		z->s_max = value;
	else if (streq(label, "O"))
		z->o_max = value;
	else if (streq(label, "M"))
		z->m_max = value;
	else if (streq(label, "L"))
		z->flavor_max = value;
	else if (streq(label, "N"))
		z->fake_name_size = value;
	else if (streq(label, "T"))
		z->fake_text_size = value;
	else
		return PARSE_ERROR_UNDEFINED_DIRECTIVE;

	return 0;
}

struct parser *init_parse_z(void) {
	struct maxima *z = mem_zalloc(sizeof *z);
	struct parser *p = parser_new();

	parser_setpriv(p, z);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "M sym label int value", parse_z);
	return p;
}

static errr run_parse_z(struct parser *p) {
	return parse_file(p, "limits");
}

static errr finish_parse_z(struct parser *p) {
	z_info = parser_priv(p);
	parser_destroy(p);
	return 0;
}

static struct file_parser z_parser = {
	"limits",
	init_parse_z,
	run_parse_z,
	finish_parse_z
};

static enum parser_error parse_k_n(struct parser *p) {
	int idx = parser_getint(p, "index");
	const char *name = parser_getstr(p, "name");
	struct object_kind *h = parser_priv(p);

	struct object_kind *k = mem_alloc(sizeof *k);
	memset(k, 0, sizeof(*k));
	k->next = h;
	parser_setpriv(p, k);
	k->kidx = idx;
	k->name = string_make(name);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_g(struct parser *p) {
	const char *sym = parser_getsym(p, "char");
	const char *color = parser_getsym(p, "color");
	struct object_kind *k = parser_priv(p);
	assert(k);

	k->d_char = sym[0];
	if (strlen(color) > 1)
		k->d_attr = color_text_to_attr(color);
	else
		k->d_attr = color_char_to_attr(color[0]);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_i(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	int tval;

	assert(k);

	tval = tval_find_idx(parser_getsym(p, "tval"));
	if (tval < 0)
		return PARSE_ERROR_UNRECOGNISED_TVAL;

	k->tval = tval;
	k->sval = parser_getint(p, "sval");
	k->pval = parser_getrand(p, "pval");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_w(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	assert(k);

	k->level = parser_getint(p, "level");
	k->weight = parser_getint(p, "weight");
	k->cost = parser_getint(p, "cost");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_a(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	char *s = string_make(parser_getstr(p, "pairs"));
	char *t;
	int depth, rarity, i = 0;
	assert(k);

	t = strtok(s, ":");
	while (t) {
	        if (sscanf(tmp, "%d/%d", &depth, &rarity) != 2)
		        return PARSE_ERROR_GENERIC;
		k->chance[i] = rarity;
		k->locale[i++] = depth;
		t = strtok(NULL, ":");
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_VALUE : PARSE_ERROR_NONE;
}

static enum parser_error parse_k_p(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	struct random hd = parser_getrand(p, "hd");
	assert(k);

	k->ac = parser_getint(p, "ac");
	k->dd = hd.dice;
	k->ds = hd.sides;
	k->to_h = parser_getrand(p, "to-h");
	k->to_d = parser_getrand(p, "to-d");
	k->to_a = parser_getrand(p, "to-a");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_m(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	assert(k);

	k->gen_mult_prob = parser_getint(p, "prob");
	k->stack_size = parser_getrand(p, "stack");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_f(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	char *s = string_make(parser_getstr(p, "flags"));
	char *t;
	assert(k);

	t = strtok(s, " |");
	while (t) {
	        bool found = FALSE;
		if (!grab_flag(k->flags_obj, OF_SIZE, object_flags, t)) 
		        found = TRUE;
		if (!grab_flag(k->flags_curse, CF_SIZE, curse_flags, t)) 
		        found = TRUE;
		if (!grab_flag(k->flags_kind, KF_SIZE, kind_flags, t)) 
		        found = TRUE;
		if (!found) break;
		t = strtok(NULL, " |");
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_FLAG : PARSE_ERROR_NONE;

}

static enum parser_error parse_k_b(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	char *s = string_make(parser_getstr(p, "values"));
	char *t;
	int val, which = 0;
	assert(k);

	t = strtok(s, " |");
	while (t) {
	        which = grab_value(t, player_resist_values, &val);
		if (which) {
		        k->percent_res[which] = RES_LEVEL_BASE - val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_stat_values, &val);
		if (which) {
		        k->bonus_stat[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_other_values, &val);
		if (which) {
		        k->bonus_other[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, slay_values, &val);
		if (which) {
		        k->multiple_slay[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, brand_values, &val);
		if (which) {
		        k->multiple_brand[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
		break;
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_VALUE : PARSE_ERROR_NONE;
}

static enum parser_error parse_k_e(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	assert(k);

	k->effect = grab_one_effect(parser_getsym(p, "name"));
	if (parser_hasval(p, "time"))
		k->time = parser_getrand(p, "time");
	if (!k->effect)
		return PARSE_ERROR_GENERIC;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_k_d(struct parser *p) {
	struct object_kind *k = parser_priv(p);
	assert(k);
	k->text = string_append(k->text, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_k(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N int index str name", parse_k_n);
	parser_reg(p, "G sym char sym color", parse_k_g);
	parser_reg(p, "I sym tval int sval rand pval", parse_k_i);
	parser_reg(p, "W int level int extra int weight int cost", parse_k_w);
	parser_reg(p, "A str pairs", parse_k_a);
	parser_reg(p, "P int ac rand hd rand to-h rand to-d rand to-a", parse_k_p);
	parser_reg(p, "M int prob rand stack", parse_k_m);
	parser_reg(p, "F str flags", parse_k_f);
	parser_reg(p, "B str values", parse_k_b);
	parser_reg(p, "E sym name ?rand time", parse_k_e);
	parser_reg(p, "D str text", parse_k_d);
	return p;
}

static errr run_parse_k(struct parser *p) {
	return parse_file(p, "object");
}

static errr finish_parse_k(struct parser *p) {
	struct object_kind *k, *n;

	k_info = mem_zalloc(z_info->k_max * sizeof(*k));
	for (k = parser_priv(p); k; k = k->next) {
		if (k->kidx >= z_info->k_max)
			continue;
		memcpy(&k_info[k->kidx], k, sizeof(*k));
	}

	k = parser_priv(p);
	while (k) {
		n = k->next;
		mem_free(k);
		k = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser k_parser = {
	"object",
	init_parse_k,
	run_parse_k,
	finish_parse_k
};

static enum parser_error parse_a_n(struct parser *p) {
	int idx = parser_getint(p, "index");
	const char *name = parser_getstr(p, "name");
	struct artifact *h = parser_priv(p);

	struct artifact *a = mem_zalloc(sizeof *a);
	a->next = h;
	parser_setpriv(p, a);
	a->aidx = idx;
	a->name = string_make(name);

	/* Ignore all elements */
	flags_set(a->flags, OF_SIZE, OF_IGNORE_MASK, FLAG_END);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_a_i(struct parser *p) {
	struct artifact *a = parser_priv(p);
	int tval, sval;

	assert(a);

	tval = tval_find_idx(parser_getsym(p, "tval"));
	if (tval < 0)
		return PARSE_ERROR_UNRECOGNISED_TVAL;
	a->tval = tval;

	sval = lookup_sval(a->tval, parser_getsym(p, "sval"));
	if (sval < 0)
		return PARSE_ERROR_UNRECOGNISED_SVAL;
	a->sval = sval;

	a->pval = parser_getint(p, "pval");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_a_w(struct parser *p) {
	struct artifact *a = parser_priv(p);
	assert(a);

	a->level = parser_getint(p, "level");
	a->rarity = parser_getint(p, "rarity");
	a->weight = parser_getint(p, "weight");
	a->cost = parser_getint(p, "cost");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_a_p(struct parser *p) {
	struct artifact *a = parser_priv(p);
	struct random hd = parser_getrand(p, "hd");
	assert(a);

	a->ac = parser_getint(p, "ac");
	a->dd = hd.dice;
	a->ds = hd.sides;
	a->to_h = parser_getint(p, "to-h");
	a->to_d = parser_getint(p, "to-d");
	a->to_a = parser_getint(p, "to-a");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_a_f(struct parser *p) {
	struct artifact *a = parser_priv(p);
	char *s;
	char *t;
	assert(a);

	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	s = string_make(parser_getstr(p, "flags"));

	t = strtok(s, " |");
	while (t) {
	        bool found = FALSE;
		if (!grab_flag(a->flags, OF_SIZE, object_flags, t))
		        found = TRUE;
		if (!grab_flag(a->flags_curse, CF_SIZE, curse_flags, t)) 
		        found = TRUE;
		if (!grab_flag(a->flags_kind, KF_SIZE, kind_flags, t)) 
		        found = TRUE;
		if (!found) break;
		t = strtok(NULL, " |");
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_FLAG : PARSE_ERROR_NONE;
}

static enum parser_error parse_a_b(struct parser *p) {
	struct artifact *a = parser_priv(p);
	char *s = string_make(parser_getstr(p, "values"));
	char *t;
	int val, which = 0;
	assert(a);

	t = strtok(s, " |");
	while (t) {
	        which = grab_value(t, player_resist_values, &val);
		if (which) {
		        a->percent_res[which] = RES_LEVEL_BASE - val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_stat_values, &val);
		if (which) {
		        a->bonus_stat[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_other_values, &val);
		if (which) {
		        a->bonus_other[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, slay_values, &val);
		if (which) {
		        a->multiple_slay[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, brand_values, &val);
		if (which) {
		        a->multiple_brand[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
		break;
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_VALUE : PARSE_ERROR_NONE;
}

static enum parser_error parse_a_e(struct parser *p) {
	struct artifact *a = parser_priv(p);
	assert(a);

	a->effect = grab_one_effect(parser_getsym(p, "name"));
	a->time = parser_getrand(p, "time");
	if (!a->effect)
		return PARSE_ERROR_GENERIC;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_a_m(struct parser *p) {
	struct artifact *a = parser_priv(p);
	assert(a);

	a->effect_msg = string_append(a->effect_msg, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_a_d(struct parser *p) {
	struct artifact *a = parser_priv(p);
	assert(a);

	a->text = string_append(a->text, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_a(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N int index str name", parse_a_n);
	parser_reg(p, "I sym tval sym sval int pval", parse_a_i);
	parser_reg(p, "W int level int rarity int weight int cost", parse_a_w);
	parser_reg(p, "P int ac rand hd int to-h int to-d int to-a", parse_a_p);
	parser_reg(p, "F ?str flags", parse_a_f);
	parser_reg(p, "B ?str values", parse_a_b);
	parser_reg(p, "E sym name rand time", parse_a_e);
	parser_reg(p, "M str text", parse_a_m);
	parser_reg(p, "D str text", parse_a_d);
	return p;
}

static errr run_parse_a(struct parser *p) {
	return parse_file(p, "artifact");
}

static errr finish_parse_a(struct parser *p) {
	struct artifact *a, *n;

	a_info = mem_zalloc(z_info->a_max * sizeof(*a));
	for (a = parser_priv(p); a; a = a->next) {
		if (a->aidx >= z_info->a_max)
			continue;
		memcpy(&a_info[a->aidx], a, sizeof(*a));
	}

	a = parser_priv(p);
	while (a) {
		n = a->next;
		mem_free(a);
		a = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser a_parser = {
	"artifact",
	init_parse_a,
	run_parse_a,
	finish_parse_a
};

struct name {
	struct name *next;
	char *str;
};

struct names_parse {
	unsigned int section;
	unsigned int nnames[RANDNAME_NUM_TYPES];
	struct name *names[RANDNAME_NUM_TYPES];
};

static enum parser_error parse_names_n(struct parser *p) {
	unsigned int section = parser_getint(p, "section");
	struct names_parse *s = parser_priv(p);
	if (s->section >= RANDNAME_NUM_TYPES)
		return PARSE_ERROR_GENERIC;
	s->section = section;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_names_d(struct parser *p) {
	const char *name = parser_getstr(p, "name");
	struct names_parse *s = parser_priv(p);
	struct name *ns = mem_zalloc(sizeof *ns);

	s->nnames[s->section]++;
	ns->next = s->names[s->section];
	ns->str = string_make(name);
	s->names[s->section] = ns;
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_names(void) {
	struct parser *p = parser_new();
	struct names_parse *n = mem_zalloc(sizeof *n);
	n->section = 0;
	parser_setpriv(p, n);
	parser_reg(p, "N int section", parse_names_n);
	parser_reg(p, "D str name", parse_names_d);
	return p;
}

static errr run_parse_names(struct parser *p) {
	return parse_file(p, "names");
}

static errr finish_parse_names(struct parser *p) {
	int i;
	unsigned int j;
	struct names_parse *n = parser_priv(p);
	struct name *nm;
	name_sections = mem_zalloc(sizeof(char**) * RANDNAME_NUM_TYPES);
	for (i = 0; i < RANDNAME_NUM_TYPES; i++) {
		name_sections[i] = mem_alloc(sizeof(char*) * (n->nnames[i] + 1));
		for (nm = n->names[i], j = 0; nm && j < n->nnames[i]; nm = nm->next, j++) {
			name_sections[i][j] = nm->str;
		}
		name_sections[i][n->nnames[i]] = NULL;
		while (n->names[i]) {
			nm = n->names[i]->next;
			mem_free(n->names[i]);
			n->names[i] = nm;
		}
	}
	mem_free(n);
	parser_destroy(p);
	return 0;
}

struct file_parser names_parser = {
	"names",
	init_parse_names,
	run_parse_names,
	finish_parse_names
};

static const char *terrain_flags[] =
{
	#define TF(a, b) #a,
	#include "list-terrain-flags.h"
	#undef TF
	NULL
};

static enum parser_error parse_f_n(struct parser *p) {
	int idx = parser_getuint(p, "index");
	const char *name = parser_getstr(p, "name");
	struct feature *h = parser_priv(p);

	struct feature *f = mem_zalloc(sizeof *f);
	f->next = h;
	f->fidx = idx;
	f->mimic = idx;
	f->name = string_make(name);
	parser_setpriv(p, f);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_f_g(struct parser *p) {
	char glyph = parser_getchar(p, "glyph");
	const char *color = parser_getsym(p, "color");
	int attr = 0;
	struct feature *f = parser_priv(p);

	if (!f)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	f->d_char = glyph;
	if (strlen(color) > 1)
		attr = color_text_to_attr(color);
	else
		attr = color_char_to_attr(color[0]);
	if (attr < 0)
		return PARSE_ERROR_INVALID_COLOR;
	f->d_attr = attr;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_f_m(struct parser *p) {
	unsigned int idx = parser_getuint(p, "index");
	struct feature *f = parser_priv(p);

	if (!f)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	f->mimic = idx;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_f_f(struct parser *p) {
	char *flags;
	struct feature *f = parser_priv(p);
	char *s;

	if (!f)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	flags = string_make(parser_getstr(p, "flags"));

	s = strtok(flags, " |");
	while (s) {
	  if (grab_flag(f->flags, TF_SIZE, terrain_flags, s)) {
			mem_free(s);
			return PARSE_ERROR_INVALID_FLAG;
		}
		s = strtok(NULL, " |");
	}

	mem_free(flags);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_f_d(struct parser *p) {
	struct feature *f = parser_priv(p);
	assert(f);

	f->text = string_append(f->text, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_f(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index str name", parse_f_n);
	parser_reg(p, "G char glyph sym color", parse_f_g);
	parser_reg(p, "M uint index", parse_f_m);
	parser_reg(p, "F ?str flags", parse_f_f);
	parser_reg(p, "D str text", parse_f_d);
	return p;
}

static errr run_parse_f(struct parser *p) {
	return parse_file(p, "terrain");
}

static errr finish_parse_f(struct parser *p) {
	struct feature *f, *n;

	f_info = mem_zalloc(z_info->f_max * sizeof(*f));
	for (f = parser_priv(p); f; f = f->next) {
		if (f->fidx >= z_info->f_max)
			continue;
		memcpy(&f_info[f->fidx], f, sizeof(*f));
	}

	f = parser_priv(p);
	while (f) {
		n = f->next;
		mem_free(f);
		f = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser f_parser = {
	"terrain",
	init_parse_f,
	run_parse_f,
	finish_parse_f
};

static enum parser_error parse_e_n(struct parser *p) {
	int idx = parser_getint(p, "index");
	const char *name = parser_getstr(p, "name");
	struct ego_item *h = parser_priv(p);

	struct ego_item *e = mem_alloc(sizeof *e);
	memset(e, 0, sizeof(*e));
	e->next = h;
	parser_setpriv(p, e);
	e->eidx = idx;
	e->name = string_make(name);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_w(struct parser *p) {
	int level = parser_getint(p, "level");
	int rarity = parser_getint(p, "rarity");
	int cost = parser_getint(p, "cost");
	struct ego_item *e = parser_priv(p);

	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	e->level = level;
	e->rarity = rarity;
	e->cost = cost;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_x(struct parser *p) {
	int rating = parser_getint(p, "rating");
	int xtra = parser_getint(p, "xtra");
	struct ego_item *e = parser_priv(p);

	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	e->rating = rating;
	e->xtra = xtra;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_t(struct parser *p) {
	int i;
	int tval;
	int min_sval, max_sval;

	struct ego_item *e = parser_priv(p);
	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	tval = tval_find_idx(parser_getsym(p, "tval"));
	if (tval < 0)
		return PARSE_ERROR_UNRECOGNISED_TVAL;

	min_sval = parser_getint(p, "min-sval");
	max_sval = parser_getint(p, "max-sval");

	for (i = 0; i < EGO_TVALS_MAX; i++) {
		if (!e->tval[i]) {
			e->tval[i] = tval;
			e->min_sval[i] = min_sval;
			e->max_sval[i] = max_sval;
			break;
		}
	}

	if (i == EGO_TVALS_MAX)
		return PARSE_ERROR_GENERIC;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_c(struct parser *p) {
	struct random th = parser_getrand(p, "th");
	struct random td = parser_getrand(p, "td");
	struct random ta = parser_getrand(p, "ta");
	struct random pval = parser_getrand(p, "pval");
	struct ego_item *e = parser_priv(p);

	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	e->to_h = th;
	e->to_d = td;
	e->to_a = ta;
	e->pval = pval;

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_m(struct parser *p) {
	int th = parser_getint(p, "th");
	int td = parser_getint(p, "td");
	int ta = parser_getint(p, "ta");
	int pval = parser_getint(p, "pval");
	struct ego_item *e = parser_priv(p);

	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	e->min_to_h = th;
	e->min_to_d = td;
	e->min_to_a = ta;
	e->min_pval = pval;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_f(struct parser *p) {
	struct ego_item *e = parser_priv(p);
	char *s;
	char *t;

	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	s = string_make(parser_getstr(p, "flags"));
	t = strtok(s, " |");
	while (t) {
	        bool found = FALSE;
		if (!grab_flag(e->flags, OF_SIZE, object_flags, t))
		        found = TRUE;
		if (!grab_flag(e->flags_curse, CF_SIZE, curse_flags, t)) 
		        found = TRUE;
		if (!grab_flag(e->flags_kind, KF_SIZE, kind_flags, t)) 
		        found = TRUE;
		if (!found) break;
		t = strtok(NULL, " |");
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_FLAG : PARSE_ERROR_NONE;
}

static enum parser_error parse_e_b(struct parser *p) {
	struct ego_item *e = parser_priv(p);
	char *s = string_make(parser_getstr(p, "values"));
	char *t;
	int val, which = 0;
	assert(e);

	t = strtok(s, " |");
	while (t) {
	        which = grab_value(t, player_resist_values, &val);
		if (which) {
		        e->percent_res[which] = RES_LEVEL_BASE - val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_stat_values, &val);
		if (which) {
		        e->bonus_stat[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_other_values, &val);
		if (which) {
		        e->bonus_other[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, slay_values, &val);
		if (which) {
		        e->multiple_slay[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, brand_values, &val);
		if (which) {
		        e->multiple_brand[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
		break;
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_VALUE : PARSE_ERROR_NONE;
}

static enum parser_error parse_e_e(struct parser *p) {
	struct ego_item *e = parser_priv(p);
	assert(e);

	e->effect = grab_one_effect(parser_getsym(p, "name"));
	if (parser_hasval(p, "time"))
		e->time = parser_getrand(p, "time");
	if (!e->effect)
		return PARSE_ERROR_GENERIC;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_e_d(struct parser *p) {
	struct ego_item *e = parser_priv(p);

	if (!e)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	e->text = string_append(e->text, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_e(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N int index str name", parse_e_n);
	parser_reg(p, "W int level int rarity int pad int cost", parse_e_w);
	parser_reg(p, "X int rating int xtra", parse_e_x);
	parser_reg(p, "T sym tval int min-sval int max-sval", parse_e_t);
	parser_reg(p, "C rand th rand td rand ta", parse_e_c);
	parser_reg(p, "F ?str flags", parse_e_f);
	parser_reg(p, "B ?str values", parse_e_b);
	parser_reg(p, "E sym name rand time", parse_e_e);
	parser_reg(p, "D str text", parse_e_d);
	return p;
}

static errr run_parse_e(struct parser *p) {
	return parse_file(p, "ego_item");
}

static errr finish_parse_e(struct parser *p) {
	struct ego_item *e, *n;

	e_info = mem_zalloc(z_info->e_max * sizeof(*e));
	for (e = parser_priv(p); e; e = e->next) {
		if (e->eidx >= z_info->e_max)
			continue;
		memcpy(&e_info[e->eidx], e, sizeof(*e));
	}

	e = parser_priv(p);
	while (e) {
		n = e->next;
		mem_free(e);
		e = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser e_parser = {
	"ego_item",
	init_parse_e,
	run_parse_e,
	finish_parse_e
};

static enum parser_error parse_r_n(struct parser *p) {
	struct monster_race *h = parser_priv(p);
	struct monster_race *r = mem_alloc(sizeof *r);
	memset(r, 0, sizeof(*r));
	r->next = h;
	r->ridx = parser_getuint(p, "index");
	r->name = string_make(parser_getstr(p, "name"));
	parser_setpriv(p, r);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_r_g(struct parser *p) {
	struct monster_race *r = parser_priv(p);
	const char *color;
	int attr;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
		color = parser_getsym(p, "color");
	if (strlen(color) > 1)
		attr = color_text_to_attr(color);
	else
		attr = color_char_to_attr(color[0]);
	if (attr < 0)
		return PARSE_ERROR_INVALID_COLOR;
	r->d_attr = attr;
	r->d_char = parser_getchar(p, "glyph");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_r_i(struct parser *p) {
	struct monster_race *r = parser_priv(p);

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->speed = parser_getint(p, "speed");
	r->avg_hp = parser_getint(p, "hp");
	r->aaf = parser_getint(p, "aaf");
	r->ac = parser_getint(p, "ac");
	r->sleep = parser_getint(p, "sleep");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_r_w(struct parser *p) {
	struct monster_race *r = parser_priv(p);

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->level = parser_getint(p, "level");
	r->rarity = parser_getint(p, "rarity");
	r->mana = parser_getint(p, "mana");
	r->mexp = parser_getint(p, "mexp");
	return PARSE_ERROR_NONE;
}

static const char *r_info_blow_method[] =
{
	#define RBM(a, b) #a,
	#include "list-blow-methods.h"
	#undef RBM
	NULL
};

static int find_blow_method(const char *name) {
	int i;
	for (i = 0; r_info_blow_method[i]; i++)
		if (streq(name, r_info_blow_method[i]))
			break;
	return i;
}

static const char *r_info_blow_effect[] =
{
	#define RBE(a, b) #a,
	#include "list-blow-effects.h"
	#undef RBE
	NULL
};

static int find_blow_effect(const char *name) {
	int i;
	for (i = 0; r_info_blow_effect[i]; i++)
		if (streq(name, r_info_blow_effect[i]))
			break;
	return i;
}

static enum parser_error parse_r_b(struct parser *p) {
	struct monster_race *r = parser_priv(p);
	int i;
	struct random dam;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	for (i = 0; i < MONSTER_BLOW_MAX; i++)
		if (!r->blow[i].method)
			break;
	if (i == MONSTER_BLOW_MAX)
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	r->blow[i].method = find_blow_method(parser_getsym(p, "method"));
	if (!r_info_blow_method[r->blow[i].method])
		return PARSE_ERROR_UNRECOGNISED_BLOW;
	if (parser_hasval(p, "effect")) {
		r->blow[i].effect = find_blow_effect(parser_getsym(p, "effect"));
		if (!r_info_blow_effect[r->blow[i].effect])
			return PARSE_ERROR_INVALID_EFFECT;
	}
	if (parser_hasval(p, "damage")) {
		dam = parser_getrand(p, "damage");
		r->blow[i].d_dice = dam.dice;
		r->blow[i].d_side = dam.sides;
	}


	return PARSE_ERROR_NONE;
}

static const char *r_info_flags[] =
{
	#define RF(a, b) #a,
	#include "list-mon-flags.h"
	#undef RF
	NULL
};

static enum parser_error parse_r_f(struct parser *p) {
	struct monster_race *r = parser_priv(p);
	char *flags;
	char *s;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	flags = string_make(parser_getstr(p, "flags"));
	s = strtok(flags, " |");
	while (s) {
		if (grab_flag(r->flags, RF_SIZE, r_info_flags, s)) {
			mem_free(flags);
			return PARSE_ERROR_INVALID_FLAG;
		}
		s = strtok(NULL, " |");
	}

	mem_free(flags);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_r_d(struct parser *p) {
	struct monster_race *r = parser_priv(p);

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->text = string_append(r->text, parser_getstr(p, "desc"));
	return PARSE_ERROR_NONE;
}

static const char *r_info_spell_flags[] =
{
	#define RSF(a, b) #a,
	#include "list-mon-spells.h"
	#undef RSF
	NULL
};

static enum parser_error parse_r_s(struct parser *p) {
	struct monster_race *r = parser_priv(p);
	char *flags;
	char *s;
	int pct;
	int ret = PARSE_ERROR_NONE;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	flags = string_make(parser_getstr(p, "spells"));
	s = strtok(flags, " |");
	while (s) {
		if (1 == sscanf(s, "1_IN_%d", &pct)) {
			if (pct < 1 || pct > 100) {
				ret = PARSE_ERROR_INVALID_SPELL_FREQ;
				break;
			}
			r->freq_spell = 100 / pct;
			r->freq_innate = r->freq_spell;
		} else if (1 == sscanf(s, "POW_%d", &i)) {
		        r_ptr->spell_power = i;
		} else {
			if (grab_flag(r->spell_flags, RSF_SIZE, r_info_spell_flags, s)) {
				ret = PARSE_ERROR_INVALID_FLAG;
				break;
			}
		}
		s = strtok(NULL, " |");
	}

	mem_free(flags);
	return ret;
}

struct parser *init_parse_r(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);

	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index str name", parse_r_n);
	parser_reg(p, "G char glyph sym color", parse_r_g);
	parser_reg(p, "I int speed int hp int aaf int ac int sleep", parse_r_i);
	parser_reg(p, "W int level int rarity int mana int mexp", parse_r_w);
	parser_reg(p, "B sym method ?sym effect ?rand damage", parse_r_b);
	parser_reg(p, "F ?str flags", parse_r_f);
	parser_reg(p, "D str desc", parse_r_d);
	parser_reg(p, "S str spells", parse_r_s);
	return p;
}

static errr run_parse_r(struct parser *p) {
	return parse_file(p, "monster");
}

static errr finish_parse_r(struct parser *p) {
	struct monster_race *r, *n;
	int i;

	r_info = mem_zalloc(sizeof(*r) * z_info->r_max);
	for (r = parser_priv(p); r; r = r->next) {
		if (r->ridx >= z_info->r_max)
			continue;
		memcpy(&r_info[r->ridx], r, sizeof(*r));
	}

	r = parser_priv(p);
	while (r) {
		n = r->next;
		mem_free(r);
		r = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser r_parser = {
	"monster",
	init_parse_r,
	run_parse_r,
	finish_parse_r
};

static enum parser_error parse_p_n(struct parser *p) {
	struct player_race *h = parser_priv(p);
	struct player_race *r = mem_zalloc(sizeof *r);

	r->next = h;
	r->ridx = parser_getuint(p, "index");
	r->name = string_make(parser_getstr(p, "name"));
	parser_setpriv(p, r);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_s(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->r_adj[A_STR] = parser_getint(p, "str");
	r->r_adj[A_DEX] = parser_getint(p, "dex");
	r->r_adj[A_CON] = parser_getint(p, "con");
	r->r_adj[A_INT] = parser_getint(p, "int");
	r->r_adj[A_WIS] = parser_getint(p, "wis");
	r->r_adj[A_CHR] = parser_getint(p, "chr");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_r(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->r_skills[SKILL_DISARM] = parser_getint(p, "dis");
	r->r_skills[SKILL_DEVICE] = parser_getint(p, "dev");
	r->r_skills[SKILL_SAVE] = parser_getint(p, "sav");
	r->r_skills[SKILL_STEALTH] = parser_getint(p, "stl");
	r->r_skills[SKILL_SEARCH] = parser_getint(p, "srh");
	r->r_skills[SKILL_SEARCH_FREQUENCY] = parser_getint(p, "fos");
	r->r_skills[SKILL_TO_HIT_MELEE] = parser_getint(p, "thm");
	r->r_skills[SKILL_TO_HIT_BOW] = parser_getint(p, "thb");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_m(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->rx_skills[SKILL_DISARM] = parser_getint(p, "xdis");
	r->rx_skills[SKILL_DEVICE] = parser_getint(p, "xdev");
	r->rx_skills[SKILL_SAVE] = parser_getint(p, "xsav");
	r->rx_skills[SKILL_STEALTH] = parser_getint(p, "xstl");
	r->rx_skills[SKILL_SEARCH] = parser_getint(p, "xsrh");
	r->rx_skills[SKILL_SEARCH_FREQUENCY] = parser_getint(p, "xfos");
	r->rx_skills[SKILL_TO_HIT_MELEE] = parser_getint(p, "xthm");
	r->rx_skills[SKILL_TO_HIT_BOW] = parser_getint(p, "xthb");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_e(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->re_id = parser_getint(p, "id");
	r->re_mint = parser_getint(p, "mint");
	r->re_maxt = parser_getint(p, "maxt");
	r->re_skde = parser_getint(p, "skde");
	r->re_ac = parser_getint(p, "ac");
	r->re_bonus = parser_getint(p, "bonus");
	r->re_xtra1 = parser_getint(p, "xtra1");
	r->re_xtra2 = parser_getint(p, "xtra2");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_x(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->r_mhp = parser_getint(p, "mhp");
	r->r_exp = parser_getint(p, "exp");
	r->infra = parser_getint(p, "infra");
	r->start_lev = parser_getint(p, "start_lev");
	r->hometown = parser_getint(p, "hometown");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_i(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->hist = parser_getint(p, "hist");
	r->b_age = parser_getint(p, "b-age");
	r->m_age = parser_getint(p, "m-age");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_h(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->m_b_ht = parser_getint(p, "mbht");
	r->m_m_ht = parser_getint(p, "mmht");
	r->f_b_ht = parser_getint(p, "fbht");
	r->f_m_ht = parser_getint(p, "fmht");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_w(struct parser *p) {
	struct player_race *r = parser_priv(p);
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	r->m_b_wt = parser_getint(p, "mbwt");
	r->m_m_wt = parser_getint(p, "mmwt");
	r->f_b_wt = parser_getint(p, "fbwt");
	r->f_m_wt = parser_getint(p, "fmwt");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_p_f(struct parser *p) {
	struct player_race *r = parser_priv(p);
	char *flags;
	char *s;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	flags = string_make(parser_getstr(p, "flags"));
	s = strtok(flags, " |");
	while (s) {
		if (grab_flag(r->flags, OF_SIZE, k_info_flags, s))
			break;
		s = strtok(NULL, " |");
	}
	mem_free(flags);
	return s ? PARSE_ERROR_INVALID_FLAG : PARSE_ERROR_NONE;
}

static enum parser_error parse_p_b(struct parser *p) {
	struct player_race *r = parser_priv(p);
	char *s = string_make(parser_getstr(p, "values"));
	char *t;
	int val, which = 0;
	assert(r);

	t = strtok(s, " |");
	while (t) {
	        which = grab_value(t, player_resist_values, &val);
		if (which) {
		        r->percent_res[which] = RES_LEVEL_BASE - val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_stat_values, &val);
		if (which) {
		        r->bonus_stat[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, bonus_other_values, &val);
		if (which) {
		        r->bonus_other[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, slay_values, &val);
		if (which) {
		        r->multiple_slay[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
	        which = grab_value(t, brand_values, &val);
		if (which) {
		        r->multiple_brand[which] = val;
			t = strtok(NULL, " |");
			continue;
		}
		break;
	}
	mem_free(s);
	return t ? PARSE_ERROR_INVALID_VALUE : PARSE_ERROR_NONE;
}

static const char *player_info_flags[] =
{
	#define PF(a, b) #a,
	#include "list-player-flags.h"
	#undef PF
	NULL
};

static enum parser_error parse_p_u(struct parser *p) {
	struct player_race *r = parser_priv(p);
	char *flags;
	char *s;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	flags = string_make(parser_getstr(p, "flags"));
	s = strtok(flags, " |");
	while (s) {
		if (grab_flag(r->pflags, PF_SIZE, player_info_flags, s))
			break;
		s = strtok(NULL, " |");
	}
	mem_free(flags);
	return s ? PARSE_ERROR_INVALID_FLAG : PARSE_ERROR_NONE;
}

static enum parser_error parse_p_c(struct parser *p) {
	struct player_race *r = parser_priv(p);
	char *classes;
	char *s;

	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!parser_hasval(p, "classes"))
		return PARSE_ERROR_NONE;
	classes = string_make(parser_getstr(p, "classes"));
	s = strtok(classes, " |");
	while (s) {
		r->choice |= 1 << atoi(s);
		s = strtok(NULL, " |");
	}
	mem_free(classes);
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_p(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index str name", parse_p_n);
	parser_reg(p, "S int str int int int wis int dex int con int chr", parse_p_s);
	parser_reg(p, "R int dis int dev int sav int stl int srh int fos int thn int thb", parse_p_r);
	parser_reg(p, "M int xdis int xdev int xsav int xstl int xsrh int xfos int xthn int xthb", parse_p_m);
	parser_reg(p, "E int id int mint int maxt int skde int ac int pval int xtra1 int xtra2", parse_p_e);
	parser_reg(p, "X int mhp int exp int infra int start_lev int hometown", parse_p_x);
	parser_reg(p, "I int hist int b-age int m-age", parse_p_i);
	parser_reg(p, "H int mbht int mmht int fbht int fmht", parse_p_h);
	parser_reg(p, "W int mbwt int mmwt int fbwt int fmwt", parse_p_w);
	parser_reg(p, "F ?str flags", parse_p_f);
	parser_reg(p, "B ?str values", parse_p_b);
	parser_reg(p, "U ?str flags", parse_p_u);
	parser_reg(p, "C ?str classes", parse_p_c);
	return p;
}

static errr run_parse_p(struct parser *p) {
	return parse_file(p, "p_race");
}

static errr finish_parse_p(struct parser *p) {
	struct player_race *r, *n;

	p_info = mem_zalloc(sizeof(*r) * z_info->p_max);
	for (r = parser_priv(p); r; r = r->next) {
		if (r->ridx >= z_info->p_max)
			continue;
		memcpy(&p_info[r->ridx], r, sizeof(*r));
	}

	r = parser_priv(p);
	while (r) {
		n = r->next;
		mem_free(r);
		r = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser p_parser = {
	"p_race",
	init_parse_p,
	run_parse_p,
	finish_parse_p
};

static enum parser_error parse_c_n(struct parser *p) {
	struct player_class *h = parser_priv(p);
	struct player_class *c = mem_zalloc(sizeof *c);
	c->cidx = parser_getuint(p, "index");
	c->name = string_make(parser_getstr(p, "name"));
	c->next = h;
	parser_setpriv(p, c);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_s(struct parser *p) {
	struct player_class *c = parser_priv(p);

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	c->c_adj[A_STR] = parser_getint(p, "str");
	c->c_adj[A_INT] = parser_getint(p, "int");
	c->c_adj[A_WIS] = parser_getint(p, "wis");
	c->c_adj[A_DEX] = parser_getint(p, "dex");
	c->c_adj[A_CON] = parser_getint(p, "con");
	c->c_adj[A_CHR] = parser_getint(p, "chr");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_c(struct parser *p) {
	struct player_class *c = parser_priv(p);

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	c->c_skills[SKILL_DISARM] = parser_getint(p, "dis");
	c->c_skills[SKILL_DEVICE] = parser_getint(p, "dev");
	c->c_skills[SKILL_SAVE] = parser_getint(p, "sav");
	c->c_skills[SKILL_STEALTH] = parser_getint(p, "stl");
	c->c_skills[SKILL_SEARCH] = parser_getint(p, "srh");
	c->c_skills[SKILL_SEARCH_FREQUENCY] = parser_getint(p, "fos");
	c->c_skills[SKILL_TO_HIT_MELEE] = parser_getint(p, "thm");
	c->c_skills[SKILL_TO_HIT_BOW] = parser_getint(p, "thb");
	c->c_skills[SKILL_TO_HIT_THROW] = parser_getint(p, "throw");
	c->c_skills[SKILL_DIGGING] = parser_getint(p, "dig");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_x(struct parser *p) {
	struct player_class *c = parser_priv(p);

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	c->x_skills[SKILL_DISARM] = parser_getint(p, "dis");
	c->x_skills[SKILL_DEVICE] = parser_getint(p, "dev");
	c->x_skills[SKILL_SAVE] = parser_getint(p, "sav");
	c->x_skills[SKILL_STEALTH] = parser_getint(p, "stl");
	c->x_skills[SKILL_SEARCH] = parser_getint(p, "srh");
	c->x_skills[SKILL_SEARCH_FREQUENCY] = parser_getint(p, "fos");
	c->x_skills[SKILL_TO_HIT_MELEE] = parser_getint(p, "thm");
	c->x_skills[SKILL_TO_HIT_BOW] = parser_getint(p, "thb");
	c->x_skills[SKILL_TO_HIT_THROW] = parser_getint(p, "throw");
	c->x_skills[SKILL_DIGGING] = parser_getint(p, "dig");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_i(struct parser *p) {
	struct player_class *c = parser_priv(p);

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	c->c_mhp = parser_getint(p, "mhp");
	c->sense_base = parser_getint(p, "sense-base");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_a(struct parser *p) {
	struct player_class *c = parser_priv(p);

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	c->max_attacks = parser_getint(p, "max-attacks");
	c->min_weight = parser_getint(p, "min-weight");
	c->att_multiply = parser_getint(p, "att-multiply");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_m(struct parser *p) {
	struct player_class *c = parser_priv(p);

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	c->spell_book = parser_getuint(p, "book");
	c->spell_stat = parser_getuint(p, "stat");
	c->spell_first = parser_getuint(p, "first");
	c->spell_weight = parser_getuint(p, "weight");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_b(struct parser *p) {
	struct player_class *c = parser_priv(p);
	unsigned int spell;

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	spell = parser_getuint(p, "spell");
	if (spell >= PY_MAX_SPELLS)
		return PARSE_ERROR_OUT_OF_BOUNDS;
	c->spells.info[spell].slevel = parser_getint(p, "level");
	c->spells.info[spell].smana = parser_getint(p, "mana");
	c->spells.info[spell].sfail = parser_getint(p, "fail");
	c->spells.info[spell].sexp = parser_getint(p, "exp");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_t(struct parser *p) {
	struct player_class *c = parser_priv(p);
	int i;

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	for (i = 0; i < PY_MAX_LEVEL / 5; i++) {
		if (!c->title[i]) {
			c->title[i] = string_make(parser_getstr(p, "title"));
			break;
		}
	}

	if (i >= PY_MAX_LEVEL / 5)
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_e(struct parser *p) {
	struct player_class *c = parser_priv(p);
	int i;
	int tval, sval;

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	tval = tval_find_idx(parser_getsym(p, "tval"));
	if (tval < 0)
		return PARSE_ERROR_UNRECOGNISED_TVAL;

	sval = lookup_sval(tval, parser_getsym(p, "sval"));
	if (sval < 0)
		return PARSE_ERROR_UNRECOGNISED_SVAL;

	for (i = 0; i <= MAX_START_ITEMS; i++)
		if (!c->start_items[i].min)
			break;
	if (i > MAX_START_ITEMS)
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	c->start_items[i].kind = objkind_get(tval, sval);
	c->start_items[i].min = parser_getuint(p, "min");
	c->start_items[i].max = parser_getuint(p, "max");
	/* XXX: MAX_ITEM_STACK? */
	if (c->start_items[i].min > 99 || c->start_items[i].max > 99)
		return PARSE_ERROR_INVALID_ITEM_NUMBER;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_c_f(struct parser *p) {
	struct player_class *c = parser_priv(p);
	char *flags;
	char *s;

	if (!c)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!parser_hasval(p, "flags"))
		return PARSE_ERROR_NONE;
	flags = string_make(parser_getstr(p, "flags"));
	s = strtok(flags, " |");
	while (s) {
		if (grab_flag(c->pflags, PF_SIZE, player_info_flags, s))
			break;
		s = strtok(NULL, " |");
	}

	mem_free(flags);
	return s ? PARSE_ERROR_INVALID_FLAG : PARSE_ERROR_NONE;
}

struct parser *init_parse_c(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index str name", parse_c_n);
	parser_reg(p, "S int str int int int wis int dex int con int chr", parse_c_s);
	parser_reg(p, "C int dis int dev int sav int stl int srh int fos int thm int thb int throw int dig", parse_c_c);
	parser_reg(p, "X int dis int dev int sav int stl int srh int fos int thm int thb int throw int dig", parse_c_x);
	parser_reg(p, "I int mhp int sense-base", parse_c_i);
	parser_reg(p, "A int max_1 int max_50 int penalty int max_penalty int bonus int max_bonus", parse_c_a);
	parser_reg(p, "T str title", parse_c_t);
	parser_reg(p, "E sym tval sym sval uint min uint max", parse_c_e);
	parser_reg(p, "U ?str flags", parse_c_u);
	parser_reg(p, "L ?str flags", parse_c_l);
	return p;
}

static errr run_parse_c(struct parser *p) {
	return parse_file(p, "p_class");
}

static errr finish_parse_c(struct parser *p) {
	struct player_class *c, *n;

	c_info = mem_zalloc(sizeof(*c) * z_info->c_max);
	for (c = parser_priv(p); c; c = c->next) {
		if (c->cidx >= z_info->c_max)
			continue;
		memcpy(&c_info[c->cidx], c, sizeof(*c));
	}

	c = parser_priv(p);
	while (c) {
		n = c->next;
		mem_free(c);
		c = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser c_parser = {
	"p_class",
	init_parse_c,
	run_parse_c,
	finish_parse_c
};

static enum parser_error parse_v_n(struct parser *p) {
	struct vault *h = parser_priv(p);
	struct vault *v = mem_zalloc(sizeof *v);

	v->vidx = parser_getuint(p, "index");
	v->name = string_make(parser_getstr(p, "name"));
	v->next = h;
	parser_setpriv(p, v);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_v_x(struct parser *p) {
	struct vault *v = parser_priv(p);

	if (!v)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	v->typ = parser_getuint(p, "type");
	v->rat = parser_getint(p, "rating");
	v->hgt = parser_getuint(p, "height");
	v->wid = parser_getuint(p, "width");

	/* XXX: huh? These checks were in the original code and I have no idea
	 * why. */
	if (v->typ == 6 && (v->wid > 33 || v->hgt > 22))
		return PARSE_ERROR_VAULT_TOO_BIG;
	if (v->typ == 7 && (v->wid > 66 || v->hgt > 44))
		return PARSE_ERROR_VAULT_TOO_BIG;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_v_d(struct parser *p) {
	struct vault *v = parser_priv(p);

	if (!v)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	v->text = string_append(v->text, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_v(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index str name", parse_v_n);
	parser_reg(p, "X uint type int rating uint height uint width", parse_v_x);
	parser_reg(p, "D str text", parse_v_d);
	return p;
}

static errr run_parse_v(struct parser *p) {
	return parse_file(p, "vault");
}

static errr finish_parse_v(struct parser *p) {
	struct vault *v, *n;

	v_info = mem_zalloc(sizeof(*v) * z_info->v_max);
	for (v = parser_priv(p); v; v = v->next) {
		if (v->vidx >= z_info->v_max)
			continue;
		memcpy(&v_info[v->vidx], v, sizeof(*v));
	}

	v = parser_priv(p);
	while (v) {
		n = v->next;
		mem_free(v);
		v = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser v_parser = {
	"vault",
	init_parse_v,
	run_parse_v,
	finish_parse_v
};

static enum parser_error parse_h_n(struct parser *p) {
	struct history *oh = parser_priv(p);
	struct history *h = mem_zalloc(sizeof *h);

	h->chart = parser_getint(p, "chart");
	h->next = parser_getint(p, "next");
	h->roll = parser_getint(p, "roll");
	h->bonus = parser_getint(p, "bonus");
	h->nextp = oh;
	h->hidx = oh ? oh->hidx + 1 : 0;
	parser_setpriv(p, h);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_h_d(struct parser *p) {
	struct history *h = parser_priv(p);

	if (!h)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	h->text = string_append(h->text, parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_h(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N int chart int next int roll int bonus", parse_h_n);
	parser_reg(p, "D str text", parse_h_d);
	return p;
}

static errr run_parse_h(struct parser *p) {
	return parse_file(p, "p_hist");
}

static errr finish_parse_h(struct parser *p) {
	struct history *h, *n;

	h_info = mem_zalloc(sizeof(*h) * z_info->h_max);
	for (h = parser_priv(p); h; h = h->nextp) {
		if (h->hidx >= z_info->h_max) {
			printf("warning: skipping bad history %d\n", h->hidx);
			continue;
		}
		memcpy(&h_info[h->hidx], h, sizeof(*h));
	}

	h = parser_priv(p);
	while (h) {
		n = h->nextp;
		mem_free(h);
		h = n;
	}

	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser h_parser = {
	"p_hist",
	init_parse_h,
	run_parse_h,
	finish_parse_h
};

static enum parser_error parse_flavor_n(struct parser *p) {
	struct flavor *h = parser_priv(p);
	struct flavor *f = mem_zalloc(sizeof *f);

	f->next = h;
	f->fidx = parser_getuint(p, "index");
	f->tval = tval_find_idx(parser_getsym(p, "tval"));
	/* assert(f->tval); */
	if (parser_hasval(p, "sval"))
		f->sval = lookup_sval(f->tval, parser_getsym(p, "sval"));
	else
		f->sval = SV_UNKNOWN;
	parser_setpriv(p, f);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_flavor_g(struct parser *p) {
	struct flavor *f = parser_priv(p);
	int d_attr;
	const char *attr;

	if (!f)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	f->d_char = parser_getchar(p, "glyph");
	attr = parser_getsym(p, "attr");
	if (strlen(attr) == 1) {
		d_attr = color_char_to_attr(attr[0]);
	} else {
		d_attr = color_text_to_attr(attr);
	}
	if (d_attr < 0)
		return PARSE_ERROR_GENERIC;
	f->d_attr = d_attr;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_flavor_d(struct parser *p) {
	struct flavor *f = parser_priv(p);

	if (!f)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	f->text = string_append(f->text, parser_getstr(p, "desc"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_flavor(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index sym tval ?sym sval", parse_flavor_n);
	parser_reg(p, "G char glyph sym attr", parse_flavor_g);
	parser_reg(p, "D str desc", parse_flavor_d);
	return p;
}

static errr run_parse_flavor(struct parser *p) {
	return parse_file(p, "flavor");
}

static errr finish_parse_flavor(struct parser *p) {
	struct flavor *f, *n;

	flavor_info = mem_zalloc(z_info->flavor_max * sizeof(*f));

	for (f = parser_priv(p); f; f = f->next) {
		if (f->fidx >= z_info->flavor_max)
			continue;
		memcpy(&flavor_info[f->fidx], f, sizeof(*f));
	}

	f = parser_priv(p);
	while (f) {
		n = f->next;
		mem_free(f);
		f = n;
	}

	parser_destroy(p);
	return 0;
}

struct file_parser flavor_parser = {
	"flavor",
	init_parse_flavor,
	run_parse_flavor,
	finish_parse_flavor
};

static enum parser_error parse_s_n(struct parser *p) {
	struct spell *s = mem_zalloc(sizeof *s);
	s->next = parser_priv(p);
	s->sidx = parser_getuint(p, "index");
	s->name = string_make(parser_getstr(p, "name"));
	parser_setpriv(p, s);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_s_i(struct parser *p) {
	struct spell *s = parser_priv(p);

	if (!s)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	s->tval = parser_getuint(p, "tval");
	s->sval = parser_getuint(p, "sval");
	s->snum = parser_getuint(p, "snum");

	/* XXX elly: postprocess instead? */
	s->realm = s->tval - TV_MAGIC_BOOK;
	s->spell_index = s->sidx - (s->realm * PY_MAX_SPELLS);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_s_d(struct parser *p) {
	struct spell *s = parser_priv(p);

	if (!s)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	s->text = string_append(s->text, parser_getstr(p, "desc"));
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_s(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "V sym version", ignored);
	parser_reg(p, "N uint index str name", parse_s_n);
	parser_reg(p, "I uint tval uint sval uint snum", parse_s_i);
	parser_reg(p, "D str desc", parse_s_d);
	return p;
}

static errr run_parse_s(struct parser *p) {
	return parse_file(p, "spell");
}

static errr finish_parse_s(struct parser *p) {
	struct spell *s, *n;

	s_info = mem_zalloc(z_info->s_max * sizeof(*s_info));
	for (s = parser_priv(p); s; s = s->next) {
		if (s->sidx >= z_info->s_max)
			continue;
		memcpy(&s_info[s->sidx], s, sizeof(*s));
	}

	s = parser_priv(p);
	while (s) {
		n = s->next;
		mem_free(s);
		s = n;
	}

	parser_destroy(p);
	return 0;
}

static struct file_parser s_parser = {
	"spell",
	init_parse_s,
	run_parse_s,
	finish_parse_s
};

/*
 * Initialize the "spell_list" array
 */
static void init_books(void)
{
	byte realm, sval, snum;
	u16b spell;

	/* Since not all slots in all books are used, initialize to -1 first */
	for (realm = 0; realm < MAX_REALMS; realm++)
	{
		for (sval = 0; sval < BOOKS_PER_REALM; sval++)
		{
			for (snum = 0; snum < SPELLS_PER_BOOK; snum++)
			{
				spell_list[realm][sval][snum] = -1;
			}
		}
	}

	/* Place each spell in its own book */
	for (spell = 0; spell < z_info->s_max; spell++)
	{
		/* Get the spell */
		spell_type *s_ptr = &s_info[spell];

		/* Put it in the book */
		spell_list[s_ptr->realm][s_ptr->sval][s_ptr->snum] = spell;
	}
}


/* Initialise hints */
static enum parser_error parse_hint(struct parser *p) {
	struct hint *h = parser_priv(p);
	struct hint *new = mem_zalloc(sizeof *new);

	new->hint = string_make(parser_getstr(p, "text"));
	new->next = h;

	parser_setpriv(p, new);
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_hints(void) {
	struct parser *p = parser_new();
	parser_reg(p, "H str text", parse_hint);
	return p;
}

static errr run_parse_hints(struct parser *p) {
	return parse_file(p, "hints");
}

static errr finish_parse_hints(struct parser *p) {
	hints = parser_priv(p);
	parser_destroy(p);
	return 0;
}

static struct file_parser hints_parser = {
	"hints",
	init_parse_hints,
	run_parse_hints,
	finish_parse_hints,
};


/*** Initialize others ***/

static void autoinscribe_init(void)
{
	if (inscriptions)
		FREE(inscriptions);
 
	inscriptions = 0;
	inscriptions_count = 0;

	inscriptions = C_ZNEW(AUTOINSCRIPTIONS_MAX, autoinscription);
}


/*
 * Initialize some other arrays
 */
static errr init_other(void)
{
	int i;


	/*** Prepare the various "bizarre" arrays ***/

	/* Initialize the "macro" package */
	(void)macro_init();

	/* Initialize the "quark" package */
	(void)quarks_init();

	/* Initialize squelch things */
	autoinscribe_init();
	squelch_init();
	textui_knowledge_init();

	/* Initialize the "message" package */
	(void)messages_init();

	/*** Prepare grid arrays ***/

	/* Array of grids */
	view_g = C_ZNEW(VIEW_MAX, u16b);

	/* Array of grids */
	temp_g = C_ZNEW(TEMP_MAX, u16b);

	/* Hack -- use some memory twice */
	temp_y = ((byte*)(temp_g)) + 0;
	temp_x = ((byte*)(temp_g)) + TEMP_MAX;


	/*** Prepare dungeon arrays ***/

	/* Padded into array */
	cave_info = C_ZNEW(DUNGEON_HGT, byte_256);
	cave_info2 = C_ZNEW(DUNGEON_HGT, byte_256);

	/* Feature array */
	cave_feat = C_ZNEW(DUNGEON_HGT, byte_wid);

	/* Entity arrays */
	cave_o_idx = C_ZNEW(DUNGEON_HGT, s16b_wid);
	cave_m_idx = C_ZNEW(DUNGEON_HGT, s16b_wid);

	/* Flow arrays */
	cave_cost = C_ZNEW(DUNGEON_HGT, byte_wid);
	cave_when = C_ZNEW(DUNGEON_HGT, byte_wid);


	/*** Prepare "vinfo" array ***/

	/* Used by "update_view()" */
	(void)vinfo_init();


	/*** Prepare entity arrays ***/

	/* Objects */
	o_list = C_ZNEW(z_info->o_max, object_type);

	/* Monsters */
	mon_list = C_ZNEW(z_info->m_max, monster_type);


	/*** Prepare lore array ***/

	/* Lore */
	l_list = C_ZNEW(z_info->r_max, monster_lore);


	/*** Prepare mouse buttons ***/

	button_init(button_add_text, button_kill_text);


	/*** Prepare quest array ***/

	/* Quests */
	q_list = C_ZNEW(MAX_Q_IDX, quest);


	/*** Prepare the inventory ***/

	/* Allocate it */
	p_ptr->inventory = C_ZNEW(ALL_INVEN_TOTAL, object_type);



	/*** Prepare the options ***/
	option_set_defaults();

	/* Initialize the window flags */
	for (i = 0; i < ANGBAND_TERM_MAX; i++)
	{
		/* Assume no flags */
		op_ptr->window_flag[i] = 0L;
	}


	/*** Pre-allocate space for the "format()" buffer ***/

	/* Hack -- Just call the "format()" function */
	(void)format("I wish you could swim, like dolphins can swim...");

	/* Success */
	return (0);
}



/*
 * Initialize some other arrays
 */
static errr init_alloc(void)
{
	int i;

	monster_race *r_ptr;

	ego_item_type *e_ptr;

	alloc_entry *table;

	s16b num[MAX_DEPTH];

	s16b aux[MAX_DEPTH];


	/*** Initialize object allocation info ***/
	init_obj_alloc();

	/*** Analyze monster allocation info ***/

	/* Clear the "aux" array */
	(void)C_WIPE(aux, MAX_DEPTH, s16b);

	/* Clear the "num" array */
	(void)C_WIPE(num, MAX_DEPTH, s16b);

	/* Size of "alloc_race_table" */
	alloc_race_size = 0;

	/* Scan the monsters (not the ghost) */
	for (i = 1; i < z_info->r_max - 1; i++)
	{
		/* Get the i'th race */
		r_ptr = &r_info[i];

		/* Legal monsters */
		if (r_ptr->rarity)
		{
			/* Count the entries */
			alloc_race_size++;

			/* Group by level */
			num[r_ptr->level]++;
		}
	}

	/* Collect the level indexes */
	for (i = 1; i < MAX_DEPTH; i++)
	{
		/* Group by level */
		num[i] += num[i-1];
	}

	/* Paranoia */
	if (!num[0]) quit("No town monsters!");


	/*** Initialize monster allocation info ***/

	/* Allocate the alloc_race_table */
	alloc_race_table = C_ZNEW(alloc_race_size, alloc_entry);

	/* Get the table entry */
	table = alloc_race_table;

	/* Scan the monsters (not the ghost) */
	for (i = 1; i < z_info->r_max - 1; i++)
	{
		/* Get the i'th race */
		r_ptr = &r_info[i];

		/* Count valid pairs */
		if (r_ptr->rarity)
		{
			int p, x, y, z;

			/* Extract the base level */
			x = r_ptr->level;

			/* Extract the base probability */
			p = (100 / r_ptr->rarity);

			/* Skip entries preceding our locale */
			y = (x > 0) ? num[x-1] : 0;

			/* Skip previous entries at this locale */
			z = y + aux[x];

			/* Load the entry */
			table[z].index = i;
			table[z].level = x;
			table[z].prob1 = p;
			table[z].prob2 = p;
			table[z].prob3 = p;

			/* Another entry complete for this locale */
			aux[x]++;
		}
	}

	/*** Analyze ego_item allocation info ***/

	/* Clear the "aux" array */
	(void)C_WIPE(aux, MAX_DEPTH, s16b);

	/* Clear the "num" array */
	(void)C_WIPE(num, MAX_DEPTH, s16b);

	/* Size of "alloc_ego_table" */
	alloc_ego_size = 0;

	/* Scan the ego items */
	for (i = 1; i < z_info->e_max; i++)
	{
		/* Get the i'th ego item */
		e_ptr = &e_info[i];

		/* Legal items */
		if (e_ptr->rarity)
		{
			/* Count the entries */
			alloc_ego_size++;

			/* Group by level */
			num[e_ptr->level]++;
		}
	}

	/* Collect the level indexes */
	for (i = 1; i < MAX_DEPTH; i++)
	{
		/* Group by level */
		num[i] += num[i-1];
	}

	/*** Initialize ego-item allocation info ***/

	/* Allocate the alloc_ego_table */
	alloc_ego_table = C_ZNEW(alloc_ego_size, alloc_entry);

	/* Get the table entry */
	table = alloc_ego_table;

	/* Scan the ego-items */
	for (i = 1; i < z_info->e_max; i++)
	{
		/* Get the i'th ego item */
		e_ptr = &e_info[i];

		/* Count valid pairs */
		if (e_ptr->rarity)
		{
			int p, x, y, z;

			/* Extract the base level */
			x = e_ptr->level;

			/* Extract the base probability */
			p = (100 / e_ptr->rarity);

			/* Skip entries preceding our locale */
			y = (x > 0) ? num[x-1] : 0;

			/* Skip previous entries at this locale */
			z = y + aux[x];

			/* Load the entry */
			table[z].index = i;
			table[z].level = x;
			table[z].prob1 = p;
			table[z].prob2 = p;
			table[z].prob3 = p;

			/* Another entry complete for this locale */
			aux[x]++;
		}
	}


	/* Success */
	return (0);
}



/*
 * Hack -- main Angband initialization entry point
 *
 * Verify some files, display the "news.txt" file, create
 * the high score file, initialize all internal arrays, and
 * load the basic "user pref files".
 *
 * Be very careful to keep track of the order in which things
 * are initialized, in particular, the only thing *known* to
 * be available when this function is called is the "z-term.c"
 * package, and that may not be fully initialized until the
 * end of this function, when the default "user pref files"
 * are loaded and "Term_xtra(TERM_XTRA_REACT,0)" is called.
 *
 * Note that this function attempts to verify the "news" file,
 * and the game aborts (cleanly) on failure, since without the
 * "news" file, it is likely that the "lib" folder has not been
 * correctly located.  Otherwise, the news file is displayed for
 * the user.
 *
 * Note that this function attempts to verify (or create) the
 * "high score" file, and the game aborts (cleanly) on failure,
 * since one of the most common "extraction" failures involves
 * failing to extract all sub-directories (even empty ones), such
 * as by failing to use the "-d" option of "pkunzip", or failing
 * to use the "save empty directories" option with "Compact Pro".
 * This error will often be caught by the "high score" creation
 * code below, since the "lib/apex" directory, being empty in the
 * standard distributions, is most likely to be "lost", making it
 * impossible to create the high score file.
 *
 * Note that various things are initialized by this function,
 * including everything that was once done by "init_some_arrays".
 *
 * This initialization involves the parsing of special files
 * in the "lib/data" and sometimes the "lib/edit" directories.
 *
 * Note that the "template" files are initialized first, since they
 * often contain errors.  This means that macros and message recall
 * and things like that are not available until after they are done.
 *
 * We load the default "user pref files" here in case any "color"
 * changes are needed before character creation.
 *
 * Note that the "graf-xxx.prf" file must be loaded separately,
 * if needed, in the first (?) pass through "TERM_XTRA_REACT".
 */
bool init_angband(void)
{
	event_signal(EVENT_ENTER_INIT);


	/*** Initialize some arrays ***/

	/* Initialize size info */
	event_signal_string(EVENT_INITSTATUS, "Initializing array sizes...");
	if (run_parser(&z_parser)) quit("Cannot initialize sizes");

	/* Initialize feature info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (features)");
	if (run_parser(&f_parser)) quit("Cannot initialize features");

	/* Initialize object info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (objects)");
	if (run_parser(&k_parser)) quit("Cannot initialize objects");

	/* Initialize ego-item info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (ego-items)");
	if (run_parser(&e_parser)) quit("Cannot initialize ego-items");

	/* Initialize monster info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (monsters)");
	if (run_parser(&r_parser)) quit("Cannot initialize monsters");

	/* Initialize artifact info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (artifacts)");
	if (run_parser(&a_parser)) quit("Cannot initialize artifacts");

	/* Initialize feature info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (vaults)");
	if (run_parser(&v_parser)) quit("Cannot initialize vaults");

	/* Initialize history info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (histories)");
	if (run_parser(&h_parser)) quit("Cannot initialize histories");

	/* Initialize race info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (races)");
	if (run_parser(&p_parser)) quit("Cannot initialize races");

	/* Initialize class info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (classes)");
	if (run_parser(&c_parser)) quit("Cannot initialize classes");

	/* Initialize flavor info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (flavors)");
	if (run_parser(&flavor_parser)) quit("Cannot initialize flavors");

	/* Initialize spell info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (spells)");
	if (run_parser(&s_parser)) quit("Cannot initialize spells");

	/* Initialize hint text */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (hints)");
	if (run_parser(&hints_parser)) quit("Cannot initialize hints");

	/* Initialize spellbook info */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (spellbooks)");
	init_books();

	/* Initialise store stocking data */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (store stocks)");
	store_init();

	/* Initialise random name data */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (random names)");
	if (run_parser(&names_parser)) quit("Can't parse names");

	/* Initialize some other arrays */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (other)");
	if (init_other()) quit("Cannot initialize other stuff");

	/* Initialize some other arrays */
	event_signal_string(EVENT_INITSTATUS, "Initializing arrays... (alloc)");
	if (init_alloc()) quit("Cannot initialize alloc stuff");

	/*** Load default user pref files ***/

	/* Initialize feature info */
	event_signal_string(EVENT_INITSTATUS, "Loading basic user pref file...");

	/* Process that file */
	(void)process_pref_file("pref.prf", FALSE);

	/* Done */
	event_signal_string(EVENT_INITSTATUS, "Initialization complete");

	/* Sneakily init command list */
	cmd_init();

	/* Ask for a "command" until we get one we like. */
	while (1)
	{
		game_command *command_req;
		int failed = cmd_get(CMD_INIT, &command_req, TRUE);

		if (failed)
			continue;
		else if (command_req->command == CMD_QUIT)
			quit(NULL);
		else if (command_req->command == CMD_NEWGAME)
		{
			event_signal(EVENT_LEAVE_INIT);
			return TRUE;
		}
		else if (command_req->command == CMD_LOADFILE)
		{
			event_signal(EVENT_LEAVE_INIT);
			return FALSE;
		}
	}
}


void cleanup_angband(void)
{
	int i;


	/* Free the macros */
	macro_free();

	/* Free the macro triggers */
	macro_trigger_free();

	/* Free the allocation tables */
	free_obj_alloc();
	FREE(alloc_ego_table);
	FREE(alloc_race_table);

	if (store)
	{
		/* Free the store inventories */
		for (i = 0; i < MAX_STORES; i++)
		{
			/* Get the store */
			store_type *st_ptr = &store[i];

			/* Free the store inventory */
			FREE(st_ptr->stock);
			FREE(st_ptr->table);
		}
	}


	/* Free the stores */
	FREE(store);

	/* Free the quest list */
	FREE(q_list);

	FREE(p_ptr->inventory);

	/* Free the lore, monster, and object lists */
	FREE(l_list);
	FREE(mon_list);
	FREE(o_list);

	/* Flow arrays */
	FREE(cave_when);
	FREE(cave_cost);

	/* Free the cave */
	FREE(cave_o_idx);
	FREE(cave_m_idx);
	FREE(cave_feat);
	FREE(cave_info2);
	FREE(cave_info);

	/* Free the "update_view()" array */
	FREE(view_g);

	/* Free the temp array */
	FREE(temp_g);

	/* Free the messages */
	messages_free();

	/* Free the "quarks" */
	quarks_free();

	mem_free(k_info);
	mem_free(a_info);
	mem_free(e_info);
	mem_free(r_info);
	mem_free(c_info);

	/* Free the format() buffer */
	vformat_kill();

	/* Free the directories */
	string_free(ANGBAND_DIR_APEX);
	string_free(ANGBAND_DIR_EDIT);
	string_free(ANGBAND_DIR_FILE);
	string_free(ANGBAND_DIR_HELP);
	string_free(ANGBAND_DIR_INFO);
	string_free(ANGBAND_DIR_SAVE);
	string_free(ANGBAND_DIR_PREF);
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_XTRA);
}
