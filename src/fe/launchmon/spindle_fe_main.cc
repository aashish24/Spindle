/*
This file is part of Spindle.  For copyright information see the COPYRIGHT 
file in the top level directory, or at 
https://github.com/hpc/Spindle/blob/master/COPYRIGHT

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License (as published by the Free Software
Foundation) version 2.1 dated February 1999.  This program is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY; without even the IMPLIED
WARRANTY OF MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the terms 
and conditions of the GNU General Public License for more details.  You should 
have received a copy of the GNU Lesser General Public License along with this 
program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <pwd.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>

#include "config.h"
#include "spindle_debug.h"
#include "parseargs.h"
#include "spindle_launch.h"
#include "keyfile.h"
#include "ldcs_cobo.h"
#include "ldcs_api.h"

using namespace std;

static void setupLogging(int argc, char **argv);
static void getAppCommandLine(int argc, char *argv[], spindle_args_t *args, int *mod_argc, char ***mod_argv);
static void getDaemonCommandLine(int *daemon_argc, char ***daemon_argv, spindle_args_t *args);
static void parseCommandLine(int argc, char *argv[], spindle_args_t *args);
static void logUser();
static unsigned int get_shared_secret();
static void setupSecurity(spindle_args_t *params);

#if defined(USAGE_LOGGING_FILE)
static const char *logging_file = USAGE_LOGGING_FILE;
#else
static const char *logging_file = NULL;
#endif

extern int startLaunchmonFE(int app_argc, char *app_argv[],
                            int daemon_argc, char *daemon_argv[],
                            spindle_args_t *params);
extern int startSerialFE(int app_argc, char *app_argv[],
                         int daemon_argc, char *daemon_argv[],
                         spindle_args_t *params);

int main(int argc, char *argv[])
{
   int result;

   setupLogging(argc, argv);

   spindle_args_t params;
   parseCommandLine(argc, argv, &params);

   if (isLoggingEnabled())
      logUser();

   int app_argc;
   char **app_argv;
   getAppCommandLine(argc, argv, &params, &app_argc, &app_argv);     
   debug_printf2("Application CmdLine: ");
   for (int i = 0; i < app_argc; i++) {
      bare_printf2("%s ", app_argv[i]);
   }
   bare_printf2("\n");

   setupSecurity(&params);

   int daemon_argc;
   char **daemon_argv;
   getDaemonCommandLine(&daemon_argc, &daemon_argv, &params);
   debug_printf2("Daemon CmdLine: ");
   for (int i = 0; i < daemon_argc; i++) {
      bare_printf2("%s ", daemon_argv[i]);
   }
   bare_printf2("\n");

   if (params.opts & OPT_NOMPI)
      result = startSerialFE(app_argc, app_argv, daemon_argc, daemon_argv, &params);
   else
      result = startLaunchmonFE(app_argc, app_argv, daemon_argc, daemon_argv, &params);

   if (OPT_GET_SEC(params.opts) == OPT_SEC_KEYFILE) {
      clean_keyfile(params.number);
   }

   LOGGING_FINI;
   return result;
}

static void parseCommandLine(int argc, char *argv[], spindle_args_t *args)
{
   unsigned int opts = parseArgs(argc, argv);   

   args->number = getpid();
   args->port = getPort();
   args->opts = opts;
   args->shared_secret = get_shared_secret();
   args->location = strdup(getLocation(args->number).c_str());
   args->pythonprefix = strdup(getPythonPrefixes().c_str());
   args->preloadfile = getPreloadFile();

   debug_printf("Spindle options bitmask: %u\n", opts);
}

static void setupLogging(int argc, char **argv)
{
   LOGGING_INIT(const_cast<char *>("FE"));
   debug_printf("Spindle Command Line: ");
   for (int i = 0; i < argc; i++) {
      bare_printf("%s ", argv[i]);
   }
   bare_printf("\n");
}

static unsigned int get_shared_secret()
{
   unsigned int shared_secret;
   int fd = open("/dev/urandom", O_RDONLY);
   if (fd == -1)
      fd = open("/dev/random", O_RDONLY);
   if (fd == -1) {
      fprintf(stderr, "Error: Could not open /dev/urandom or /dev/random for shared secret. Aborting Spindle\n");
      exit(-1);
   }
      
   int result = read(fd, &shared_secret, sizeof(shared_secret));
   close(fd);
   if (result == -1) {
      fprintf(stderr, "Error: Could not read from /dev/urandom or /dev/random for shared secret. Aborting Spindle\n");
      exit(EXIT_FAILURE);
   }

   return shared_secret;
}


static void getAppCommandLine(int argc, char *argv[], spindle_args_t *params, int *mod_argc, char ***mod_argv)
{
   int app_argc;
   char **app_argv;

   int launchers_to_use = TEST_PRESETUP;
   if (params->opts & OPT_NOMPI) {
      launchers_to_use |= TEST_SERIAL;
   }
   else {
      launchers_to_use |= TEST_SLURM;
      launchers_to_use |= TEST_FLUX; 
   }

   getAppArgs(&app_argc, &app_argv);

   int result = createNewCmdLine(app_argc, app_argv, mod_argc, mod_argv, 
                                 params, launchers_to_use);
   if (result != 0) {
      fprintf(stderr, "Error parsing command line:\n");
      if (result == NO_LAUNCHER) {
         fprintf(stderr, "Could not find a job launcher (mpirun, srun, wreckrun...) in the command line\n");
      }
      if (result == NO_EXEC) {
         fprintf(stderr, "Could not find an executable in the given command line\n");
      }
      exit(EXIT_FAILURE);
   }
}

#if !defined(BINDIR)
#error Expected BINDIR to be defined
#endif
char spindle_daemon[] = BINDIR "/spindle_be";
char spindle_serial_arg[] = "--spindle_serial";
char spindle_lmon_arg[] = "--spindle_lmon";
static void getDaemonCommandLine(int *daemon_argc, char ***daemon_argv, spindle_args_t *args)
{
   char **daemon_opts = (char **) malloc(8 * sizeof(char *));
   char number_s[32];
   int i = 0;

   snprintf(number_s, 32, "%d", args->number);

   //daemon_opts[i++] = "/usr/local/bin/valgrind";
   //daemon_opts[i++] = "--tool=memcheck";
   //daemon_opts[i++] = "--leak-check=full";
   daemon_opts[i++] = spindle_daemon;
   if (args->opts & OPT_NOMPI)
      daemon_opts[i++] = spindle_serial_arg;
   else
      daemon_opts[i++] = spindle_lmon_arg;

#define STR2(X) #X
#define STR(X) STR2(X)
#define STR_CASE(X) case X: daemon_opts[i++] = const_cast<char *>(STR(X)); break
   switch (OPT_GET_SEC(args->opts)) {
      STR_CASE(OPT_SEC_MUNGE);
      STR_CASE(OPT_SEC_KEYLMON);
      STR_CASE(OPT_SEC_KEYFILE);
      STR_CASE(OPT_SEC_NULL);
   }
   daemon_opts[i++] = strdup(number_s);

   daemon_opts[i++] = NULL;

   *daemon_argc = i;
   *daemon_argv = daemon_opts;
}

static void logUser()
{
   /* Collect username */
   char *username = NULL;
   if (getenv("USER")) {
      username = getenv("USER");
   }
   if (!username) {
      struct passwd *pw = getpwuid(getuid());
      if (pw) {
         username = pw->pw_name;
      }
   }
   if (!username) {
      username = getlogin();
   }
   if (!username) {
      err_printf("Could not get username for logging\n");
   }
      
   /* Collect time */
   struct timeval tv;
   gettimeofday(&tv, NULL);
   struct tm *lt = localtime(& tv.tv_sec);
   char time_str[256];
   time_str[0] = '\0';
   strftime(time_str, sizeof(time_str), "%c", lt);
   time_str[255] = '\0';
   
   /* Collect version */
   const char *version = VERSION;

   /* Collect hostname */
   char hostname[256];
   hostname[0] = '\0';
   gethostname(hostname, sizeof(hostname));
   hostname[255] = '\0';

   string log_message = string(username) + " ran Spindle v" + version + 
                        " at " + time_str + " on " + hostname;
   debug_printf("Logging usage: %s\n", log_message.c_str());

   FILE *f = fopen(logging_file, "a");
   if (!f) {
      err_printf("Could not open logging file %s\n", logging_file);
      return;
   }
   fprintf(f, "%s\n", log_message.c_str());
   fclose(f);
}

static void setupSecurity(spindle_args_t *params)
{
   handshake_protocol_t handshake;
   int result;
   /* Setup security */
   switch (OPT_GET_SEC(params->opts)) {
      case OPT_SEC_MUNGE:
         debug_printf("Initializing FE with munge-based security\n");
         handshake.mechanism = hs_munge;
         break;
      case OPT_SEC_KEYFILE: {
         char *path;
         int len;
         debug_printf("Initializing FE with keyfile-based security\n");
         create_keyfile(params->number);
         len = MAX_PATH_LEN+1;
         path = (char *) malloc(len);
         get_keyfile_path(path, len, params->number);
         handshake.mechanism = hs_key_in_file;
         handshake.data.key_in_file.key_filepath = path;
         handshake.data.key_in_file.key_length_bytes = KEY_SIZE_BYTES;
         break;
      }
      case OPT_SEC_KEYLMON:
         debug_printf("Initializing BE with launchmon-based security\n");
         handshake.mechanism = hs_explicit_key;
         fprintf(stderr, "Error, launchmon based keys not yet implemented\n");
         exit(-1);
         break;
      case OPT_SEC_NULL:
         debug_printf("Initializing BE with NULL security\n");
         handshake.mechanism = hs_none;
         break;
   }
   result = initialize_handshake_security(&handshake);
   if (result == -1) {
      err_printf("Could not initialize security\n");
      exit(-1);
   }
}
