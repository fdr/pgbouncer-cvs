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
 * Launcer for all the rest.
 */

#include "bouncer.h"

#include <sys/resource.h>

#include <signal.h>
#include <getopt.h>

static bool set_mode(ConfElem *elem, const char *val, PgSocket *console);
static const char *get_mode(ConfElem *elem);
static bool set_auth(ConfElem *elem, const char *val, PgSocket *console);
static const char *get_auth(ConfElem *elem);

static const char *usage_str =
"usage: pgbouncer [-d] [-R] [-q] [-v] [-h|-V] config.ini\n";

static void usage(int err)
{
	printf(usage_str);
	exit(err);
}

/*
 * configuration storage
 */

int cf_quiet = 0; /* if set, no log is printed to stdout/err */
int cf_verbose = 0;
int cf_daemon = 0;
int cf_pause_mode = P_NONE;
int cf_shutdown = 0;
int cf_reboot = 0;
static char *cf_config_file;

char *cf_listen_addr = NULL;
int cf_listen_port = 6000;
char *cf_unix_socket_dir = "/tmp";

int cf_pool_mode = POOL_SESSION;

/* sbuf config */
int cf_sbuf_len = 2048;
int cf_tcp_socket_buffer = 0;
#ifdef TCP_DEFER_ACCEPT
int cf_tcp_defer_accept = 45;
#else
int cf_tcp_defer_accept = 0;
#endif
int cf_tcp_keepalive = 0;
int cf_tcp_keepcnt = 0;
int cf_tcp_keepidle = 0;
int cf_tcp_keepintvl = 0;

int cf_auth_type = AUTH_MD5;
char *cf_auth_file = "unconfigured_file";

int cf_max_client_conn = 100;
int cf_default_pool_size = 20;

char *cf_server_reset_query = "";
char *cf_server_check_query = "select 1";
usec_t cf_server_check_delay = 30 * USEC;
int cf_server_round_robin = 0;

char *cf_ignore_startup_params = "";

usec_t cf_server_lifetime = 60*60*USEC;
usec_t cf_server_idle_timeout = 10*60*USEC;
usec_t cf_server_connect_timeout = 15*USEC;
usec_t cf_server_login_retry = 15*USEC;
usec_t cf_query_timeout = 0*USEC;
usec_t cf_client_idle_timeout = 0*USEC;
usec_t cf_client_login_timeout = 60*USEC;

char *cf_logfile = NULL;
char *cf_pidfile = NULL;
static char *cf_jobname = NULL;

char *cf_admin_users = "";
char *cf_stats_users = "";
int cf_stats_period = 60;

int cf_log_connections = 1;
int cf_log_disconnections = 1;
int cf_log_pooler_errors = 1;

/*
 * config file description
 */
ConfElem bouncer_params[] = {
{"job_name",		true, CF_STR, &cf_jobname},
{"conffile",		true, CF_STR, &cf_config_file},
{"logfile",		true, CF_STR, &cf_logfile},
{"pidfile",		false, CF_STR, &cf_pidfile},
{"listen_addr",		false, CF_STR, &cf_listen_addr},
{"listen_port",		false, CF_INT, &cf_listen_port},
{"unix_socket_dir",	false, CF_STR, &cf_unix_socket_dir},
{"auth_type",		true, {get_auth, set_auth}},
{"auth_file",		true, CF_STR, &cf_auth_file},
{"pool_mode",		true, {get_mode, set_mode}},
{"max_client_conn",	true, CF_INT, &cf_max_client_conn},
{"default_pool_size",	true, CF_INT, &cf_default_pool_size},

{"server_reset_query",	true, CF_STR, &cf_server_reset_query},
{"server_check_query",	true, CF_STR, &cf_server_check_query},
{"server_check_delay",	true, CF_TIME, &cf_server_check_delay},
{"query_timeout",	true, CF_TIME, &cf_query_timeout},
{"client_idle_timeout",	true, CF_TIME, &cf_client_idle_timeout},
{"client_login_timeout",true, CF_TIME, &cf_client_login_timeout},
{"server_lifetime",	true, CF_TIME, &cf_server_lifetime},
{"server_idle_timeout",	true, CF_TIME, &cf_server_idle_timeout},
{"server_connect_timeout",true, CF_TIME, &cf_server_connect_timeout},
{"server_login_retry",	true, CF_TIME, &cf_server_login_retry},
{"server_round_robin",	true, CF_INT, &cf_server_round_robin},
{"ignore_startup_parameters",	true, CF_STR, &cf_ignore_startup_params},

{"pkt_buf",		false, CF_INT, &cf_sbuf_len},
{"tcp_defer_accept",	false, CF_INT, &cf_tcp_defer_accept},
{"tcp_socket_buffer",	true, CF_INT, &cf_tcp_socket_buffer},
{"tcp_keepalive",	true, CF_INT, &cf_tcp_keepalive},
{"tcp_keepcnt",		true, CF_INT, &cf_tcp_keepcnt},
{"tcp_keepidle",	true, CF_INT, &cf_tcp_keepidle},
{"tcp_keepintvl",	true, CF_INT, &cf_tcp_keepintvl},
{"verbose",		true, CF_INT, &cf_verbose},
{"admin_users",		true, CF_STR, &cf_admin_users},
{"stats_users",		true, CF_STR, &cf_stats_users},
{"stats_period",	true, CF_INT, &cf_stats_period},
{"log_connections",	true, CF_INT, &cf_log_connections},
{"log_disconnections",	true, CF_INT, &cf_log_disconnections},
{"log_pooler_errors",	true, CF_INT, &cf_log_pooler_errors},
{NULL},
};

static ConfSection bouncer_config [] = {
{"pgbouncer", bouncer_params, NULL},
{"databases", NULL, parse_database},
{NULL}
};

static const char *get_mode(ConfElem *elem)
{
	switch (cf_pool_mode) {
	case POOL_STMT: return "statement";
	case POOL_TX: return "transaction";
	case POOL_SESSION: return "session";
	default:
		fatal("borken mode? should not happen");
		return NULL;
	}
}

static bool set_mode(ConfElem *elem, const char *val, PgSocket *console)
{
	if (strcasecmp(val, "session") == 0)
		cf_pool_mode = POOL_SESSION;
	else if (strcasecmp(val, "transaction") == 0)
		cf_pool_mode = POOL_TX;
	else if (strcasecmp(val, "statement") == 0)
		cf_pool_mode = POOL_STMT;
	else {
		admin_error(console, "bad mode: %s", val);
		return false;
	}
	return true;
}

static const char *get_auth(ConfElem *elem)
{
	switch (cf_auth_type) {
	case AUTH_ANY: return "any";
	case AUTH_TRUST: return "trust";
	case AUTH_PLAIN: return "plain";
	case AUTH_CRYPT: return "crypt";
	case AUTH_MD5: return "md5";
	default:
		fatal("borken auth? should not happen");
		return NULL;
	}
}

static bool set_auth(ConfElem *elem, const char *val, PgSocket *console)
{
	if (strcasecmp(val, "any") == 0)
		cf_auth_type = AUTH_ANY;
	else if (strcasecmp(val, "trust") == 0)
		cf_auth_type = AUTH_TRUST;
	else if (strcasecmp(val, "plain") == 0)
		cf_auth_type = AUTH_PLAIN;
	else if (strcasecmp(val, "crypt") == 0)
		cf_auth_type = AUTH_CRYPT;
	else if (strcasecmp(val, "md5") == 0)
		cf_auth_type = AUTH_MD5;
	else {
		admin_error(console, "bad auth type: %s", val);
		return false;
	}
	return true;
}

/* config loading, tries to be tolerant to errors */
void load_config(bool reload)
{
	/* actual loading */
	iniparser(cf_config_file, bouncer_config, reload);

	/* load users if needed */
	if (cf_auth_type >= AUTH_TRUST)
		load_auth_file(cf_auth_file);

	/* reset pool_size */
	config_postprocess();

	/* reopen logfile */
	if (reload)
		close_logfile();
}

/*
 * signal handling.
 *
 * handle_* functions are not actual signal handlers but called from
 * event_loop() so they have no restrictions what they can do.
 */
static struct event ev_sigterm;
static struct event ev_sigint;
static struct event ev_sigusr1;
static struct event ev_sigusr2;
static struct event ev_sighup;

static void handle_sigterm(int sock, short flags, void *arg)
{
	log_info("Got SIGTERM, fast exit");
	/* pidfile cleanup happens via atexit() */
	exit(1);
}

static void handle_sigint(int sock, short flags, void *arg)
{
	log_info("Got SIGINT, shutting down");
	if (cf_reboot)
		fatal("Takeover was in progress, going down immediately");
	cf_pause_mode = P_PAUSE;
	cf_shutdown = 1;
}

static void handle_sigusr1(int sock, short flags, void *arg)
{
	if (cf_pause_mode == 0) {
		log_info("Got SIGUSR1, pausing all activity");
		cf_pause_mode = P_PAUSE;
	} else {
		log_info("Got SIGUSR1, but already paused/suspended");
	}
}

static void handle_sigusr2(int sock, short flags, void *arg)
{
	switch (cf_pause_mode) {
	case P_SUSPEND:
		log_info("Got SIGUSR2, continuing from SUSPEND");
		resume_all();
		cf_pause_mode = 0;
		break;
	case P_PAUSE:
		log_info("Got SIGUSR2, continuing from PAUSE");
		cf_pause_mode = 0;
		break;
	case P_NONE:
		log_info("Got SIGUSR1, but not paused/suspended");
	}
}

static void handle_sighup(int sock, short flags, void *arg)
{
	log_info("Got SIGHUP re-reading config");
	load_config(true);
}

static void signal_setup(void)
{
	int err;
	sigset_t set;

	/* block SIGPIPE */
	sigemptyset(&set);
	sigaddset(&set, SIGPIPE);
	err = sigprocmask(SIG_BLOCK, &set, NULL);
	if (err < 0)
		fatal_perror("sigprocmask");

	/* install handlers */
	signal_set(&ev_sigterm, SIGTERM, handle_sigterm, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_set(&ev_sigint, SIGINT, handle_sigint, NULL);
	signal_add(&ev_sigint, NULL);
	signal_set(&ev_sigusr1, SIGUSR1, handle_sigusr1, NULL);
	signal_add(&ev_sigusr1, NULL);
	signal_set(&ev_sigusr2, SIGUSR2, handle_sigusr2, NULL);
	signal_add(&ev_sigusr2, NULL);
	signal_set(&ev_sighup, SIGHUP, handle_sighup, NULL);
	signal_add(&ev_sighup, NULL);
}

/*
 * daemon mode
 */
static void go_daemon(void)
{
	int pid, fd;

	if (!cf_pidfile)
		fatal("daemon needs pidfile configured");

	/* dont log to stdout anymore */
	cf_quiet = 1;

	/* send stdin, stdout, stderr to /dev/null */
	fd = open("/dev/null", O_RDWR);
	if (fd < 0)
		fatal_perror("/dev/null");
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2)
		close(fd);

	/* fork new process */
	pid = fork();
	if (pid < 0)
		fatal_perror("fork");
	if (pid > 0)
		_exit(0);

	/* create new session */
	pid = setsid();
	if (pid < 0)
		fatal_perror("setsid");

	/* fork again to avoid being session leader */
	pid = fork();
	if (pid < 0)
		fatal_perror("fork");
	if (pid > 0)
		_exit(0);

}

/*
 * write pidfile.  if exists, quit with error.
 */
static void check_pidfile(void)
{
	struct stat st;
	if (!cf_pidfile)
		return;
	if (stat(cf_pidfile, &st) >= 0)
		fatal("pidfile exists, another instance running?");
}

static void remove_pidfile(void)
{
	if (!cf_pidfile)
		return;
	unlink(cf_pidfile);
}

static void write_pidfile(void)
{
	char buf[64];
	pid_t pid;
	int res, fd;

	if (!cf_pidfile)
		return;

	pid = getpid();
	sprintf(buf, "%u", (unsigned)pid);

	fd = open(cf_pidfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0)
		fatal_perror(cf_pidfile);
	res = safe_write(fd, buf, strlen(buf));
	if (res < 0)
		fatal_perror(cf_pidfile);
	safe_close(fd);

	/* only remove when we have it actually written */
	atexit(remove_pidfile);
}

/* just print out max files, in the future may warn if something is off */
static void check_limits(void)
{
	struct rlimit lim;
	int err = getrlimit(RLIMIT_NOFILE, &lim);
	if (err < 0)
		log_error("could not get RLIMIT_NOFILE: %s", strerror(errno));
	else
		log_info("File descriptors limits: S:%d H:%d",
			 (int)lim.rlim_cur, (int)lim.rlim_max);
}

static void daemon_setup(void)
{
	if (!cf_reboot)
		check_pidfile();
	if (cf_daemon)
		go_daemon();
	if (!cf_reboot)
		write_pidfile();
}

static void main_loop_once(void)
{
	reset_time_cache();
	event_loop(EVLOOP_ONCE);
	per_loop_maint();
	reuse_just_freed_objects();
}

/* boot everything */
int main(int argc, char *argv[])
{
	int c;

	/* parse cmdline */
	while ((c = getopt(argc, argv, "avhdVR")) != EOF) {
		switch (c) {
		case 'R':
			cf_reboot = 1;
			break;
		case 'v':
			cf_verbose++;
			break;
		case 'V':
			printf("%s\n", FULLVER);
			return 0;
		case 'd':
			cf_daemon = 1;
			break;
		case 'q':
			cf_quiet = 1;
			break;
		case 'h':
		default:
			usage(1);
		}
	}
	if (optind + 1 != argc)
		usage(1);
	cf_config_file = argv[optind];
	load_config(false);

	/* need to do that after loading config */
	check_limits();

	/* init random */
	srandom(time(NULL) ^ getpid());

	/* initialize subsystems, order important */
	daemon_setup();
	event_init();
	signal_setup();
	janitor_setup();
	stats_setup();
	admin_setup();

	if (cf_reboot) {
		takeover_init();
		while (cf_reboot)
			main_loop_once();
		write_pidfile();
	} else
		pooler_setup();

	/* main loop */
	while (1)
		main_loop_once();
}

