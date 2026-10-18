/*****************************************************************************
 * cachegen.c: LibVLC plugins cache generator
 *****************************************************************************
 * Copyright (C) 2010 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_tick.h>
#include <vlc_threads.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

static void version (void)
{
    puts ("LibVLC plugins cache generation version "VERSION);
}

static void usage (const char *path)
{
    printf (
"Usage: %s [--verify] [<path> ...]\n"
"Generate the LibVLC plugins cache for the specified plugins directory.\n",
            path);
    puts("With --verify, check that each cache was written. With no path, check all configured plugin directories.");
}

struct cache_check
{
    struct cache_check *next;
    char *filename;
    struct stat before;
    bool existed;
};

static void free_checks(struct cache_check *checks)
{
    while (checks != NULL)
    {
        struct cache_check *next = checks->next;
        free(checks->filename);
        free(checks);
        checks = next;
    }
}

static int prepare_check(struct cache_check **checks, const char *directory,
                         bool optional)
{
    struct stat st;
    if (vlc_stat(directory, &st) != 0)
        return optional && errno == ENOENT ? 0 : -1;

    struct cache_check *check = calloc(1, sizeof(*check));
    if (check == NULL)
        return -1;
    if (asprintf(&check->filename, "%s/plugins.dat", directory) == -1)
    {
        free(check);
        return -1;
    }
    check->existed = vlc_stat(check->filename, &check->before) == 0;
    if (!check->existed && errno != ENOENT)
        goto error;

    // Test directory access in this child process, never on the UI thread.
    char *probe;
    if (asprintf(&probe, "%s/.vlc-cache-check-XXXXXX", directory) == -1)
        goto error;
    int fd = vlc_mkstemp(probe);
    if (fd != -1)
    {
        close(fd);
        vlc_unlink(probe);
    }
    free(probe);
    if (fd == -1)
        goto error;

    // Some file systems expose modification times only in whole seconds.
    // Ensure that an unchanged cache cannot pass after a silent write failure.
    if (check->existed && check->before.st_mtime == time(NULL))
        vlc_tick_sleep(VLC_TICK_FROM_SEC(1));
    check->next = *checks;
    *checks = check;
    return 0;
error:
    free_checks(check);
    return -1;
}

static bool checks_written(const struct cache_check *checks)
{
    for (const struct cache_check *check = checks; check; check = check->next)
    {
        struct stat after;
        if (vlc_stat(check->filename, &after) != 0 || after.st_size == 0
         || (check->existed && after.st_mtime == check->before.st_mtime
                           && after.st_ino == check->before.st_ino))
            return false;
    }
    return true;
}

static int generate_cache(void)
{
    const char *const args[] = {"--ignore-config", "--quiet", "--plugins-scan",
                                "--reset-plugins-cache", "--"};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    if (vlc == NULL)
        return -1;
    libvlc_release(vlc);
    return 0;
}

static int scan_configured_directories(void)
{
    struct cache_check *checks = NULL;
    char *directory = config_GetSysPath(VLC_PKG_LIB_DIR, "plugins");
    int result = directory ? prepare_check(&checks, directory, true) : -1;
    free(directory);
    const char *extra = getenv("VLC_PLUGIN_PATH");
    char *paths = extra ? strdup(extra) : NULL;
    if (extra && !paths)
        result = -1;
    if (paths && result == 0)
    {
#ifdef _WIN32
        const char *separator = ";";
#else
        const char *separator = ":";
#endif
        for (char *state, *path = strtok_r(paths, separator, &state);
             path; path = strtok_r(NULL, separator, &state))
            if (prepare_check(&checks, path, false) != 0)
                result = -1;
    }
    free(paths);
    if (result == 0 && checks && generate_cache() == 0 && checks_written(checks))
        result = 0;
    else
        result = -1;
    free_checks(checks);
    return result;
}

int main (int argc, char *argv[])
{
    bool verify = false;
#ifdef HAVE_GETOPT_H
    static const struct option opts[] =
    {
        { "help",       no_argument,       NULL, 'h' },
        { "version",    no_argument,       NULL, 'V' },
        { "verify",     no_argument,       NULL, 'c' },
        { NULL,         no_argument,       NULL, '\0'}
    };

    int c;

    while ((c = getopt_long (argc, argv, "hVc", opts, NULL)) != -1)
        switch (c)
        {
            case 'h':
                usage (argv[0]);
                return 0;
            case 'V':
                version ();
                return 0;
            case 'c':
                verify = true;
                break;
            default:
                usage (argv[0]);
                return 1;
        }
#else
    int optind = 1;
#endif

    if (verify && optind == argc)
    {
        if (scan_configured_directories() == 0)
            return 0;
        fputs("VLC cannot update the plugin cache. Check folder access and try again.\n", stderr);
        return 1;
    }
    for (int i = optind; i < argc; i++)
    {
        const char *path = argv[i];
        struct cache_check *checks = NULL;
        if (verify && prepare_check(&checks, path, false) != 0)
        {
            fputs("VLC cannot write to the plugin folder.\n", stderr);
            return 1;
        }

        if (setenv ("VLC_PLUGIN_PATH", path, 1))
            abort ();

        int result = generate_cache();
        if (verify && !checks_written(checks))
            result = -1;
        free_checks(checks);
        if (result != 0)
        {
            fputs("VLC cannot update the plugin cache.\n", stderr);
            return 1;
        }
    }

    return 0;
}
