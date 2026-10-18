// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_vector.h>
#include <ctype.h>
#include <limits.h>
#include "configuration.h"

typedef struct VLC_VECTOR(char *) startup_args_t;

static void args_clear(startup_args_t *args)
{
    for (size_t i = 0; i < args->size; ++i)
        free(args->data[i]);
    vlc_vector_destroy(args);
}

/* No shell, environment expansion or command substitution. Backslashes in
 * Windows paths stay literal, except for the usual backslash/quote pairing. */
static int split_options(const char *text, startup_args_t *args)
{
    const char *p = text;
    while (*p)
    {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        char *word = malloc(strlen(p) + 1);
        if (word == NULL) return VLC_ENOMEM;
        size_t used = 0;
        char quote = 0;
        while (*p && (quote || !isspace((unsigned char)*p)))
        {
            if (*p == '\\' && quote != '\'')
            {
                const char *first = p;
                while (*p == '\\') ++p;
                size_t count = p - first;
                if (*p == '"')
                {
                    for (size_t i = 0; i < count / 2; ++i) word[used++] = '\\';
                    if (count % 2) { word[used++] = *p++; continue; }
                    /* An even run precedes a delimiter quote. */
                }
                else
                {
                    for (size_t i = 0; i < count; ++i) word[used++] = '\\';
                    continue;
                }
            }
            if ((*p == '\'' || *p == '"') && (!quote || quote == *p))
            {
                quote = quote ? 0 : *p;
                ++p;
            }
            else word[used++] = *p++;
        }
        if (quote) { free(word); return VLC_EINVAL; }
        word[used] = '\0';
        /* This field contains options, not an end-of-options/file list. A
         * saved terminator would turn the real launch options into files. */
        if (!strcmp(word, "--")) { free(word); return VLC_EINVAL; }
        if (!vlc_vector_push(args, word)) { free(word); return VLC_ENOMEM; }
    }
    return VLC_SUCCESS;
}

void config_FreeStartupOptions(char **args)
{
    if (args != NULL)
    {
        for (size_t i = 0; args[i] != NULL; ++i) free(args[i]);
        free(args);
    }
}

/* Read a core setting before plugin/thread setup. Loading an instance
 * just to read its arguments would initialize the application twice. */
char *config_ReadStartupSetting(const char *filename, const char *name)
{
    char *allocated = NULL;
    if (filename == NULL)
    {
        char *dir = config_GetUserDir(VLC_CONFIG_DIR);
        if (dir == NULL) return NULL;
        int ret = asprintf(&allocated, "%s" DIR_SEP CONFIG_FILE, dir);
        free(dir);
        if (ret < 0) return NULL;
        filename = allocated;
    }
    FILE *file = vlc_fopen(filename, "rt");
    free(allocated);
    if (file == NULL) return NULL;
    char *line = NULL, *value = NULL;
    size_t capacity = 0;
    bool first_line = true;
    while (getline(&line, &capacity, file) >= 0)
    {
        char *p = line;
        if (first_line && !strncmp(p, "\xef\xbb\xbf", 3)) p += 3;
        first_line = false;
        const size_t key_length = strlen(name);
        if (strncmp(p, name, key_length) || p[key_length] != '=') continue;
        p += key_length + 1;
        p[strcspn(p, "\r\n")] = '\0';
        char *next = strdup(p);
        free(value); value = next;
    }
    free(line);
    fclose(file);
    return value;
}

struct config_startup_settings config_GetStartupSettings(int argc, const char *const argv[])
{
    struct config_startup_settings settings = {0};
    for (int i = 0; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (!strcmp(arg, "--")) break;
        if (!strcmp(arg, "--load-startup-options")) settings.enabled = true;
        else if (!strcmp(arg, "--no-load-startup-options") || !strcmp(arg, "--noload-startup-options")) settings.enabled = false;
        else if (!strcmp(arg, "--ignore-config")) settings.ignore = true;
        else if (!strcmp(arg, "--no-ignore-config") || !strcmp(arg, "--noignore-config")) settings.ignore = false;
        else if (!strcmp(arg, "--reset-config")) settings.reset = true;
        else if (!strcmp(arg, "--no-reset-config") || !strcmp(arg, "--noreset-config")) settings.reset = false;
        else if (!strncmp(arg, "--config=", 9)) settings.filename = arg + 9;
        else if (!strncmp(arg, "--startup-options=", 18)) settings.options = arg + 18;
        else if (!strncmp(arg, "--language=", 11)) settings.language = arg + 11;
        else if ((!strcmp(arg, "--config") || !strcmp(arg, "--startup-options") || !strcmp(arg, "--language")) && i + 1 < argc)
        {
            const char *value = argv[++i];
            if (!strcmp(arg, "--config")) settings.filename = value;
            else if (!strcmp(arg, "--language")) settings.language = value;
            else settings.options = value;
        }
    }
    return settings;
}

int config_ExpandStartupOptions(int argc, const char *const argv[], char ***expanded)
{
    *expanded = NULL;
    const struct config_startup_settings settings = config_GetStartupSettings(argc, argv);
    if (!settings.enabled || settings.ignore || settings.reset) return argc;
    char *saved = settings.options == NULL
               ? config_ReadStartupSetting(settings.filename, "startup-options") : strdup(settings.options);
    if (saved == NULL) return argc;
    startup_args_t args = VLC_VECTOR_INITIALIZER;
    int ret = split_options(saved, &args);
    free(saved);
    if (ret != VLC_SUCCESS) { args_clear(&args); return ret; }
    if (args.size == 0) { args_clear(&args); return argc; }
    if (args.size > (size_t)(INT_MAX - argc)) { args_clear(&args); return VLC_ENOMEM; }
    for (int i = 0; i < argc; ++i)
    {
        char *copy = strdup(argv[i]);
        if (copy == NULL || !vlc_vector_push(&args, copy))
        { free(copy); args_clear(&args); return VLC_ENOMEM; }
    }
    const int count = args.size;
    if (!vlc_vector_push(&args, NULL)) { args_clear(&args); return VLC_ENOMEM; }
    *expanded = args.data;
    return count;
}

int config_ValidateStartupOptions(vlc_object_t *parent, const char *text)
{
    /* Native config strings occupy one line. */
    if (strpbrk(text, "\r\n") != NULL) return VLC_EINVAL;
    startup_args_t args = VLC_VECTOR_INITIALIZER;
    char *name = strdup("startup-options");
    if (name == NULL || !vlc_vector_push(&args, name)) { free(name); return VLC_ENOMEM; }
    int ret = split_options(text, &args);
    if (ret == VLC_SUCCESS && (args.size > INT_MAX || !vlc_vector_push(&args, NULL)))
        ret = VLC_ENOMEM;
    if (ret == VLC_SUCCESS)
    {
        vlc_object_t *scratch = vlc_object_create(parent, sizeof(*scratch));
        if (scratch == NULL) ret = VLC_ENOMEM;
        else
        {
            int index;
            ret = config_LoadCmdLine((libvlc_int_t *)scratch, args.size - 1,
                                     (const char **)args.data, &index);
            if (ret == 0 && index != (int)args.size - 1) ret = VLC_EINVAL;
            vlc_object_delete(scratch);
        }
    }
    args_clear(&args);
    return ret;
}
