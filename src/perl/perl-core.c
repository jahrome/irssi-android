/*
 perl-core.c : irssi

    Copyright (C) 1999-2001 Timo Sirainen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define NEED_PERL_H
#include "module.h"
#include "signals.h"
#include "misc.h"

#include "perl-core.h"
#include "perl-common.h"
#include "perl-signals.h"
#include "perl-sources.h"

#include "irssi-core.pl.h"

/* For compatibility with perl 5.004 and older */
#ifndef HAVE_PL_PERL
#  define PL_perl_destruct_level perl_destruct_level
#endif

extern void xs_init(void);

GSList *perl_scripts;
PerlInterpreter *my_perl;

#define IS_PERL_SCRIPT(file) \
	(strlen(file) > 3 && strcmp(file+strlen(file)-3, ".pl") == 0)

static void perl_script_destroy_package(PERL_SCRIPT_REC *script)
{
	dSP;

	ENTER;
	SAVETMPS;

	PUSHMARK(SP);
	XPUSHs(sv_2mortal(new_pv(script->package)));
	PUTBACK;

	perl_call_pv("Irssi::Core::destroy", G_VOID|G_EVAL|G_DISCARD);

	SPAGAIN;

	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void perl_script_destroy(PERL_SCRIPT_REC *script)
{
	perl_scripts = g_slist_remove(perl_scripts, script);

	signal_emit("script destroyed", 1, script);

	perl_signal_remove_package(script->package);
	perl_source_remove_package(script->package);

	g_free(script->name);
	g_free(script->package);
        g_free_not_null(script->path);
        g_free_not_null(script->data);
        g_free(script);
}

/* Initialize perl interpreter */
void perl_scripts_init(void)
{
	char *args[] = {"", "-e", "0"};
	char *code, *use_code;

	perl_scripts = NULL;
        perl_sources_start();
	perl_signals_start();

	my_perl = perl_alloc();
	perl_construct(my_perl);

	perl_parse(my_perl, xs_init, 3, args, NULL);

        perl_common_start();

	use_code = perl_get_use_list();
        code = g_strdup_printf(irssi_core_code, use_code);
	perl_eval_pv(code, TRUE);

	g_free(code);
        g_free(use_code);
}

/* Destroy all perl scripts and deinitialize perl interpreter */
void perl_scripts_deinit(void)
{
	/* destroy all scripts */
        while (perl_scripts != NULL)
		perl_script_destroy(perl_scripts->data);

        perl_signals_stop();
	perl_sources_stop();
        perl_common_stop();

	/* perl interpreter */
	perl_destruct(my_perl);
	perl_free(my_perl);
	my_perl = NULL;
}

/* Unload perl script */
void perl_script_unload(PERL_SCRIPT_REC *script)
{
        g_return_if_fail(script != NULL);

	perl_script_destroy_package(script);
        perl_script_destroy(script);
}

static char *script_file_get_name(const char *path)
{
	char *name, *ret, *p;

        ret = name = g_strdup(g_basename(path));

	p = strrchr(name, '.');
	if (p != NULL) *p = '\0';

	while (*name != '\0') {
		if (*name != '_' && !isalnum(*name))
			*name = '_';
		name++;
	}

        return ret;
}

static char *script_data_get_name(void)
{
	GString *name;
        char *ret;
	int n;

	name = g_string_new(NULL);
        n = 1;
	do {
		g_string_sprintf(name, "data%d", n);
                n++;
	} while (perl_script_find(name->str) != NULL);

	ret = name->str;
        g_string_free(name, FALSE);
        return ret;
}

static int perl_script_eval(PERL_SCRIPT_REC *script)
{
	dSP;
	char *error;
	int retcount;

	ENTER;
	SAVETMPS;

	PUSHMARK(SP);
	XPUSHs(sv_2mortal(new_pv(script->path != NULL ? script->path :
				 script->data)));
	XPUSHs(sv_2mortal(new_pv(script->name)));
	PUTBACK;

	retcount = perl_call_pv(script->path != NULL ?
				"Irssi::Core::eval_file" :
				"Irssi::Core::eval_data",
				G_EVAL|G_SCALAR);
	SPAGAIN;

        error = NULL;
	if (SvTRUE(ERRSV)) {
		STRLEN n_a;

                error = SvPV(ERRSV, n_a);
	} else if (retcount > 0) {
		error = POPp;
	}

	if (error != NULL) {
		if (*error == '\0')
			error = NULL;
		else
			signal_emit("script error", 2, script, error);
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

        return error == NULL;
}

/* NOTE: name must not be free'd */
static PERL_SCRIPT_REC *script_load(char *name, const char *path,
				    const char *data)
{
        PERL_SCRIPT_REC *script;

	/* if there's a script with a same name, destroy it */
	script = perl_script_find(name);
	if (script != NULL)
		perl_script_destroy(script);

	script = g_new0(PERL_SCRIPT_REC, 1);
	script->name = name;
	script->package = g_strdup_printf("Irssi::Script::%s", name);
	script->path = g_strdup(path);
        script->data = g_strdup(data);

	perl_scripts = g_slist_append(perl_scripts, script);
	signal_emit("script created", 1, script);

	if (!perl_script_eval(script))
                script = NULL; /* the script is destroyed in "script error" signal */
        return script;
}

/* Load a perl script, path must be a full path. */
PERL_SCRIPT_REC *perl_script_load_file(const char *path)
{
	char *name;

        g_return_val_if_fail(path != NULL, NULL);

        name = script_file_get_name(path);
        return script_load(name, path, NULL);
}

/* Load a perl script from given data */
PERL_SCRIPT_REC *perl_script_load_data(const char *data)
{
	char *name;

        g_return_val_if_fail(data != NULL, NULL);

	name = script_data_get_name();
	return script_load(name, NULL, data);
}

/* Find loaded script by name */
PERL_SCRIPT_REC *perl_script_find(const char *name)
{
	GSList *tmp;

        g_return_val_if_fail(name != NULL, NULL);

	for (tmp = perl_scripts; tmp != NULL; tmp = tmp->next) {
		PERL_SCRIPT_REC *rec = tmp->data;

		if (strcmp(rec->name, name) == 0)
                        return rec;
	}

        return NULL;
}

/* Find loaded script by package */
PERL_SCRIPT_REC *perl_script_find_package(const char *package)
{
	GSList *tmp;

        g_return_val_if_fail(package != NULL, NULL);

	for (tmp = perl_scripts; tmp != NULL; tmp = tmp->next) {
		PERL_SCRIPT_REC *rec = tmp->data;

		if (strcmp(rec->package, package) == 0)
                        return rec;
	}

        return NULL;
}

/* Returns full path for the script */
char *perl_script_get_path(const char *name)
{
	struct stat statbuf;
	char *file, *path;

	if (g_path_is_absolute(name) || (name[0] == '~' && name[1] == '/')) {
		/* full path specified */
                return convert_home(name);
	}

	/* add .pl suffix if it's missing */
	file = IS_PERL_SCRIPT(name) ? g_strdup(name) :
		g_strdup_printf("%s.pl", name);

	/* check from ~/.irssi/scripts/ */
	path = g_strdup_printf("%s/scripts/%s", get_irssi_dir(), file);
	if (stat(path, &statbuf) != 0) {
		/* check from SCRIPTDIR */
		g_free(path);
		path = g_strdup_printf(SCRIPTDIR"/%s", file);
		if (stat(path, &statbuf) != 0)
                        path = NULL;
	}
	g_free(file);
        return path;
}

static void perl_scripts_autorun(void)
{
	DIR *dirp;
	struct dirent *dp;
	struct stat statbuf;
	char *path, *fname;

        /* run *.pl scripts from ~/.irssi/scripts/autorun/ */
	path = g_strdup_printf("%s/scripts/autorun", get_irssi_dir());
	dirp = opendir(path);
	if (dirp == NULL) {
		g_free(path);
		return;
	}

	while ((dp = readdir(dirp)) != NULL) {
		if (!IS_PERL_SCRIPT(dp->d_name))
			continue;

		fname = g_strdup_printf("%s/%s", path, dp->d_name);
		if (stat(fname, &statbuf) == 0 && !S_ISDIR(statbuf.st_mode))
			perl_script_load_file(fname);
		g_free(fname);
	}
	closedir(dirp);
	g_free(path);
}

static void sig_script_error(PERL_SCRIPT_REC *script)
{
	if (script != NULL) {
		perl_script_destroy(script);
                signal_stop();
	}
}

void perl_core_init(void)
{
	PL_perl_destruct_level = 1;
	perl_signals_init();
        signal_add_last("script error", (SIGNAL_FUNC) sig_script_error);

	perl_scripts_init();
	perl_scripts_autorun();
}

void perl_core_deinit(void)
{
	perl_signals_deinit();
        perl_scripts_deinit();

	signal_remove("script error", (SIGNAL_FUNC) sig_script_error);
}