/*
 * PgBouncer - Lightweight connection pooler for PostgreSQL.
 * 
 * Copyright (c) 2007 Marko Kreen, Skype Technologies OÜ
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Config and pg_auth file reading.
 */

#include "bouncer.h"

/*
 * ConnString parsing
 */

/* get key=val pair from connstring */
static char * getpair(char *p,
		      char **key_p, int *key_len,
		      char **val_p, int *val_len)
{
	while (*p && *p == ' ')
		p++;
	*key_p = p;
	while (*p && *p != '=' && *p != ' ')
		p++;
	*key_len = p - *key_p;
	if (*p == '=')
		p++;
	*val_p = p;
	while (*p && *p != ' ')
		p++;
	*val_len = p - *val_p;

	while (*p && *p == ' ')
		p++;
	return p;
}

/* fill PgDatabase from connstr */
void parse_database(char *name, char *connstr)
{
	char *p, *key, *val;
	int klen, vlen;
	PktBuf buf;
	PgDatabase *db;
	int pool_size = -1;

	char *dbname = name;
	char *host = NULL;
	char *port = "5432";
	char *username = NULL;
	char *password = "";
	char *client_encoding = NULL;
	char *datestyle = NULL;

	in_addr_t v_addr = INADDR_NONE;
	int v_port;

	p = connstr;
	while (*p) {
		p = getpair(p, &key, &klen, &val, &vlen);
		if (*key == 0 || *val == 0 || klen == 0 || vlen == 0)
			break;
		key[klen] = 0;
		val[vlen] = 0;

		if (strcmp("dbname", key) == 0)
			dbname = val;
		else if (strcmp("host", key) == 0)
			host = val;
		else if (strcmp("port", key) == 0)
			port = val;
		else if (strcmp("user", key) == 0)
			username = val;
		else if (strcmp("password", key) == 0)
			password = val;
		else if (strcmp("client_encoding", key) == 0)
			client_encoding = val;
		else if (strcmp("datestyle", key) == 0)
			datestyle = val;
		else if (strcmp("pool_size", key) == 0)
			pool_size = atoi(val);
		else {
			log_error("skipping database %s because"
				  " of bad connstring: %s", name, connstr);
			return;
		}
	}

	if (!host) {
		if (!cf_unix_socket_dir) {
			log_error("skipping database %s because"
				" unix socket not configured", name);
			return;
		}
	} else {
		v_addr = inet_addr(host);
		if (v_addr == INADDR_NONE) {
			log_error("skipping database %s because"
					" of bad host: %s", name, host);
			return;
		}
	}
	v_port = atoi(port);
	if (v_port == 0) {
		log_error("skipping database %s because"
			  " of bad port: %s", name, port);
		return;
	}

	db = add_database(name);
	if (!db) {
		log_error("cannot create database, no mem?");
		return;
	}

	if (db->dbname) {
		bool changed = false;
		if (strcmp(db->dbname, dbname) != 0)
			changed = true;
		else if (host && db->addr.is_unix)
			changed = true;
		else if (!host && !db->addr.is_unix)
			changed = true;
		else if (host && v_addr != db->addr.ip_addr.s_addr)
			changed = true;
		else if (v_port != db->addr.port)
			changed = true;
		else if (username && !db->forced_user)
			changed = true;
		else if (username && strcmp(username, db->forced_user->name))
			changed = true;
		else if (!username && db->forced_user)
			changed = true;

		if (changed)
			tag_database_dirty(db);
	}

	/* if pool_size < 0 it will be set later */
	db->pool_size = pool_size;
	db->addr.port = v_port;
	db->addr.ip_addr.s_addr = v_addr;
	db->addr.is_unix = host ? 0 : 1;

	pktbuf_static(&buf, db->startup_params, sizeof(db->startup_params));

	pktbuf_put_string(&buf, "database");
	db->dbname = (char *)db->startup_params + pktbuf_written(&buf);
	pktbuf_put_string(&buf, dbname);

	if (client_encoding) {
		pktbuf_put_string(&buf, "client_encoding");
		pktbuf_put_string(&buf, client_encoding);
	}

	if (datestyle) {
		pktbuf_put_string(&buf, "datestyle");
		pktbuf_put_string(&buf, datestyle);
	}

	db->startup_params_len = pktbuf_written(&buf);

	/* if user is forces, create fake object for it */
	if (username != NULL) {
		if (!force_user(db, username, password))
			log_warning("db setup failed, trying to continue");
	} else if (db->forced_user)
		log_warning("losing forced user not supported,"
			    " keeping old setting");
}

/*
 * User file parsing
 */

/* find next " in string, skipping escaped ones */
static char *find_quote(char *p)
{
loop:
	while (*p && *p != '\\' && *p != '"') p++;
	if (*p == '\\' && p[1]) {
		p += 2;
		goto loop;
	}

	return p;
}

/* string is unquoted while copying */
static void copy_quoted(char *dst, const char *src, int len)
{
	char *end = dst + len - 1;
	while (*src && dst < end) {
		if (*src != '\\')
			*dst++ = *src++;
		else
			src++;
	}
	*dst = 0;
}

static void unquote_add_user(const char *username, const char *password)
{
	char real_user[MAX_USERNAME];
	char real_passwd[MAX_PASSWORD];
	PgUser *user;

	copy_quoted(real_user, username, sizeof(real_user));
	copy_quoted(real_passwd, password, sizeof(real_passwd));

	user = add_user(real_user, real_passwd);
	if (!user)
		log_warning("cannot create user, no mem");
}

static bool auth_loaded(const char *fn)
{
	static struct stat cache;
	struct stat cur;

	/* hack for resetting */
	if (fn == NULL) {
		memset(&cache, 0, sizeof(cache));
		return false;
	}

	if (stat(fn, &cur) < 0)
		return false;

	if (cache.st_dev == cur.st_dev
	&& cache.st_ino == cur.st_ino
	&& cache.st_mode == cur.st_mode
	&& cache.st_uid == cur.st_uid
	&& cache.st_gid == cur.st_gid
	&& cache.st_mtime == cur.st_mtime
	&& cache.st_size == cur.st_size)
		return true;
	cache = cur;
	return false;
}

bool loader_users_check(void)
{
	if (auth_loaded(cf_auth_file))
		return true;

	return load_auth_file(cf_auth_file);
}

/* load list of users from pg_auth/pg_psw file */
bool load_auth_file(const char *fn)
{
	char *user, *password, *buf, *p;

	buf = load_file(fn);
	if (buf == NULL) {
		/* reset file info */
		auth_loaded(NULL);
		return false;
	}

	p = buf;
	while (*p) {
		/* skip whitespace and empty lines */
		while (*p && isspace(*p)) p++;
		if (!*p)
			break;

		/* start of line */
		if (*p != '"') {
			log_error("broken auth file");
			break;
		}
		user = ++p;
		p = find_quote(p);
		if (*p != '"') {
			log_error("broken auth file");
			break;
		}
		if (p - user >= MAX_USERNAME) {
			log_error("too long username");
			break;
		}
		*p++ = 0; /* tag username end */
		
		/* get password */
		p = find_quote(p);
		if (*p != '"') {
			log_error("broken auth file");
			break;
		}
		password = ++p;
		p = find_quote(p);
		if (*p != '"') {
			log_error("broken auth file");
			break;
		}
		if (p - password >= MAX_PASSWORD) {
			log_error("too long password");
			break;
		}
		*p++ = 0; /* tag password end */

		/* send them away */
		unquote_add_user(user, password);

		/* skip rest of the line */
		while (*p && *p != '\n') p++;
	}
	free(buf);

	create_auth_cache();

	return true;
}

/*
 * INI file parser
 */

bool cf_set_int(ConfElem *elem, const char *val, PgSocket *console)
{
	int *int_p = elem->dst;
	if (*val < '0' || *val > '9') {
		admin_error(console, "bad value: %s", val);
		return false;
	}
	*int_p = atoi(val);
	return true;
}

const char *cf_get_int(ConfElem *elem)
{
	static char numbuf[32];
	int val;

	val = *(int *)elem->dst;
	sprintf(numbuf, "%d", val);
	return numbuf;
}
bool cf_set_time(ConfElem *elem, const char *val, PgSocket *console)
{
	usec_t *time_p = elem->dst;
	if (*val < '0' || *val > '9') {
		admin_error(console, "bad value: %s", val);
		return false;
	}
	*time_p = USEC * (usec_t)atoi(val);
	return true;
}

const char *cf_get_time(ConfElem *elem)
{
	static char numbuf[32];
	usec_t val;

	val = *(usec_t *)elem->dst;
	sprintf(numbuf, "%d", (int)(val / USEC));
	return numbuf;
}

bool cf_set_str(ConfElem *elem, const char *val, PgSocket *console)
{
	char **str_p = elem->dst;
	char *tmp;

	/* dont touch if not changed */
	if (*str_p && strcmp(*str_p, val) == 0)
		return true;

	/* if dynamically allocated, free it */
	if (elem->allocated)
		free(*str_p);

	tmp = strdup(val);
	if (!tmp)
		return false;

	*str_p = tmp;
	elem->allocated = true;
	return true;
}

const char * cf_get_str(ConfElem *elem)
{
	return *(char **)elem->dst;
}

bool set_config_param(ConfElem *elem_list,
		      const char *key, const char *val,
		      bool reload, PgSocket *console)
{
	ConfElem *desc;

	for (desc = elem_list; desc->name; desc++) {
		if (strcasecmp(key, desc->name))
			continue;
	
		/* if reload not allowed, skip it */
		if (reload && !desc->reloadable) {
			if (console)
				admin_error(console,
					"%s cannot be changed online", key);
			return false;
		}

		/* got config, parse it */
		return desc->io.fn_set(desc, val, console);
	}
	admin_error(console, "unknown config parameter: %s", key);
	return false;
}

static void map_config(ConfSection *sect, char *key, char *val, bool reload)
{
	if (sect == NULL)
		return;

	if (sect->data_fn)
		sect->data_fn(key, val);
	else
		set_config_param(sect->elem_list, key, val, reload, NULL);
}

const char *conf_to_text(ConfElem *elem)
{
	return elem->io.fn_get(elem);
}

static ConfSection *find_section(ConfSection *sect, const char *name)
{
	for (; sect->name; sect++)
		if (strcasecmp(sect->name, name) == 0)
			return sect;
	log_warning("unknown section in config: %s", name);
	return NULL;
}

void iniparser(const char *fn, ConfSection *sect_list, bool reload)
{
	char *buf;
	char *p, *key, *val;
	int klen, vlen;
	ConfSection *cur_section = NULL;

	buf = load_file(fn);
	if (buf == NULL) {
		if (!reload)
			exit(1);
		else
			return;
	}

	p = buf;
	while (*p) {
		/* space at the start of line - including empty lines */
		while (*p && isspace(*p)) p++;

		/* skip comment lines */
		if (*p == '#' || *p == ';') {
			while (*p && *p != '\n') p++;
			continue;
		}
		/* got new section */
		if (*p == '[') {
			key = ++p;
			while (*p && *p != ']' && *p != '\n') p++;
			if (*p != ']') {
				log_warning("bad section header");
				cur_section = NULL;
				continue;
			}
			*p++ = 0;

			cur_section = find_section(sect_list, key);
			continue;
		}

		/* done? */
		if (*p == 0) break;

		/* read key val */
		key = p;
		while (*p && (isalnum(*p) || *p == '_')) p++;
		klen = p - key;

		/* expect '=', skip it */
		while (*p && (*p == ' ' || *p == '\t')) p++;
		if (*p != '=') {
			log_error("syntax error in config, stopping loading");
			break;
		} else
			p++;
		while (*p && (*p == ' ' || *p == '\t')) p++;

		/* now read value */
		val = p;
		while (*p && (*p != '\n'))
			p++;
		vlen = p - val;
		/* eat space at end */
		while (vlen > 0 && isspace(val[vlen - 1]))
			vlen--;

		/* skip junk */
		while (*p && isspace(*p)) p++;

		/* our buf is r/w, so take it easy */
		key[klen] = 0;
		val[vlen] = 0;
		map_config(cur_section, key, val, reload);
	}

	free(buf);
}

