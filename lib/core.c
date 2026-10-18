/*****************************************************************************
 * core.c: Core libvlc new API functions : initialization
 *****************************************************************************
 * Copyright (C) 2005 VLC authors and VideoLAN
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "libvlc_internal.h"
#include <vlc_modules.h>
#include <vlc/vlc.h>

#include <vlc_interface.h>
#include <vlc_configuration.h>

#include <stdarg.h>
#include <limits.h>
#include <locale.h>
#include <assert.h>

static_assert(LIBVLC_VERSION_MAJOR    == PACKAGE_VERSION_MAJOR, "Major VLC version mismatch");
static_assert(LIBVLC_VERSION_MINOR    == PACKAGE_VERSION_MINOR, "Minor VLC version mismatch");
static_assert(LIBVLC_VERSION_REVISION == PACKAGE_VERSION_REVISION, "VLC Revision version mismatch");
static_assert(LIBVLC_VERSION_EXTRA    == PACKAGE_VERSION_EXTRA, "VLC Extra version mismatch");
static_assert(LIBVLC_ABI_VERSION_MAJOR == LIBVLC_ABI_MAJOR, "Major LibVLC version mismatch");
static_assert(LIBVLC_ABI_VERSION_MINOR == LIBVLC_ABI_MINOR, "Minor LibVLC version mismatch");
static_assert(LIBVLC_ABI_VERSION_MICRO == LIBVLC_ABI_MICRO, "Micro LibVLC version mismatch");

int libvlc_abi_version(void)
{
    return LIBVLC_ABI_VERSION_INT;
}

static libvlc_instance_t *libvlc_new_inner(int argc, const char *const *argv)
{
    libvlc_threads_init ();

    libvlc_instance_t *p_new = malloc (sizeof (*p_new));
    if (unlikely(p_new == NULL))
        return NULL;

    const char *my_argv[argc + 2];
    my_argv[0] = "libvlc"; /* dummy arg0, skipped by getopt() et al */
    for( int i = 0; i < argc; i++ )
         my_argv[i + 1] = argv[i];
    my_argv[argc + 1] = NULL; /* C calling conventions require a NULL */

    libvlc_int_t *p_libvlc_int = libvlc_InternalCreate();
    if (unlikely (p_libvlc_int == NULL))
        goto error;

    const int ret = libvlc_InternalInit( p_libvlc_int, argc + 1, my_argv );
    if (ret != VLC_SUCCESS)
    {
        libvlc_InternalDestroy( p_libvlc_int );
        const char *error = (ret == VLC_EGENERIC) ? _( "Generic VLC error" )
                                                  : vlc_strerror_c( -ret );
        libvlc_printerr( "%s", error );
        goto error;
    }

    p_new->p_libvlc_int = p_libvlc_int;
    vlc_atomic_rc_init( &p_new->ref_count );
    p_new->p_callback_list = NULL;

    return p_new;

error:
    free (p_new);
    libvlc_threads_deinit ();
    return NULL;
}

struct interface_locale_state
{
    char *lang, *language;
#ifndef _WIN32
    char *messages;
#endif
};

static bool interface_locale_save(struct interface_locale_state *state)
{
    const char *lang = getenv("LANG"), *language = getenv("LANGUAGE");
    state->lang = lang ? strdup(lang) : NULL;
    state->language = language ? strdup(language) : NULL;
    bool valid = (!lang || state->lang) && (!language || state->language);
#ifndef _WIN32
    const char *messages = setlocale(LC_MESSAGES, NULL);
    state->messages = messages ? strdup(messages) : NULL;
    valid = valid && (!messages || state->messages);
#endif
    return valid;
}

static void interface_locale_restore(const struct interface_locale_state *state)
{
    if (state->lang) setenv("LANG", state->lang, 1); else unsetenv("LANG");
    if (state->language) setenv("LANGUAGE", state->language, 1); else unsetenv("LANGUAGE");
#ifndef _WIN32
    if (state->messages) {
        setlocale(LC_MESSAGES, "C");
        setlocale(LC_MESSAGES, state->messages);
    }
#endif
}

static void interface_locale_clear(struct interface_locale_state *state)
{
    free(state->lang);free(state->language);
#ifndef _WIN32
    free(state->messages);
#endif
}

static libvlc_instance_t *libvlc_new_with_settings(int argc, const char *const *argv, bool standalone)
{
    char **expanded = NULL;
    int count = config_ExpandStartupOptions(argc, argv, &expanded);
    if (count < 0)
        fprintf(stderr, "VLC could not read the saved startup options. Starting without them.\n");
    struct interface_locale_state previous = {0};
    if (standalone) {
        if (!interface_locale_save(&previous)) {
            interface_locale_clear(&previous);
            config_FreeStartupOptions(expanded);
            return NULL;
        }
        if (config_ApplyInterfaceLanguage(expanded ? count : argc,
                expanded ? (const char *const *)expanded : argv) != VLC_SUCCESS)
            fprintf(stderr, "VLC cannot use this interface language. VLC will use the system language.\n");
    }
    libvlc_instance_t *instance = libvlc_new_inner(expanded != NULL ? count : argc,
        expanded != NULL ? (const char *const *)expanded : argv);
    if (instance == NULL && expanded != NULL)
    {
        /* A removed plugin can invalidate a previously valid saved option.
         * Restore the original locale before retrying without those options. */
        fprintf(stderr, "VLC could not use the saved startup options. Starting without them.\n");
        if (standalone) {
            interface_locale_restore(&previous);
            config_ApplyInterfaceLanguage(argc, argv);
        }
        instance = libvlc_new_inner(argc, argv);
    }
    if (standalone) interface_locale_clear(&previous);
    config_FreeStartupOptions(expanded);
    return instance;
}

libvlc_instance_t *libvlc_new(int argc, const char *const *argv)
{
    return libvlc_new_with_settings(argc, argv, false);
}

/* Private launcher entry: an embedding client must never change its host's
 * language merely by creating a LibVLC instance. */
libvlc_instance_t *libvlc_new_standalone(int argc, const char *const *argv)
{
    return libvlc_new_with_settings(argc, argv, true);
}

libvlc_instance_t *libvlc_retain( libvlc_instance_t *p_instance )
{
    assert( p_instance != NULL );

    vlc_atomic_rc_inc( &p_instance->ref_count );
    return p_instance;
}

void libvlc_release( libvlc_instance_t *p_instance )
{
    if(vlc_atomic_rc_dec( &p_instance->ref_count ))
    {
        libvlc_Quit( p_instance->p_libvlc_int );

        libvlc_InternalCleanup( p_instance->p_libvlc_int );
        libvlc_InternalDestroy( p_instance->p_libvlc_int );
        free( p_instance );
        libvlc_threads_deinit ();
    }
}

void libvlc_set_user_agent (libvlc_instance_t *p_i,
                            const char *name, const char *http)
{
    libvlc_int_t *p_libvlc = p_i->p_libvlc_int;
    char *str;

    var_SetString (p_libvlc, "user-agent", name);
    if ((http != NULL)
     && (asprintf (&str, "%s LibVLC/"PACKAGE_VERSION, http) != -1))
    {
        var_SetString (p_libvlc, "http-user-agent", str);
        free (str);
    }
}

void libvlc_set_app_id(libvlc_instance_t *p_i, const char *id,
                       const char *version, const char *icon)
{
    libvlc_int_t *p_libvlc = p_i->p_libvlc_int;

    var_SetString(p_libvlc, "app-id", id ? id : "");
    var_SetString(p_libvlc, "app-version", version ? version : "");
    var_SetString(p_libvlc, "app-icon-name", icon ? icon : "");
}

const char * libvlc_get_version(void)
{
    return VERSION_MESSAGE;
}

const char * libvlc_get_compiler(void)
{
    return VLC_Compiler();
}

const char * libvlc_get_changeset(void)
{
    extern const char psz_vlc_changeset[];
    return psz_vlc_changeset;
}

void libvlc_free( void *ptr )
{
    free( ptr );
}

static libvlc_module_description_t *module_description_list_get(
                libvlc_instance_t *p_instance, const char *capability )
{
    libvlc_module_description_t *p_list = NULL,
                          *p_actual = NULL,
                          *p_previous = NULL;
    size_t count;
    module_t **module_list = module_list_get( &count );

    for (size_t i = 0; i < count; i++)
    {
        module_t *p_module = module_list[i];

        if ( !module_provides( p_module, capability ) )
            continue;

        p_actual = ( libvlc_module_description_t * ) malloc( sizeof( libvlc_module_description_t ) );
        if ( p_actual == NULL )
        {
            libvlc_printerr( "Not enough memory" );
            libvlc_module_description_list_release( p_list );
            module_list_free( module_list );
            return NULL;
        }

        if ( p_list == NULL )
            p_list = p_actual;

        const char* name = module_get_object( p_module );
        const char* shortname = module_GetShortName( p_module );
        const char* longname = module_GetLongName( p_module );
        const char* help = module_get_help( p_module );
        const char* help_html = module_get_help_html( p_module );
        p_actual->psz_name = name ? strdup( name ) : NULL;
        p_actual->psz_shortname = shortname ? strdup( shortname ) : NULL;
        p_actual->psz_longname = longname ? strdup( longname ) : NULL;
        p_actual->psz_help = help ? strdup( help ) : NULL;
        p_actual->psz_help_html = help_html ? strdup( help_html ) : NULL;

        p_actual->p_next = NULL;
        if ( p_previous )
            p_previous->p_next = p_actual;
        p_previous = p_actual;
    }

    module_list_free( module_list );
    VLC_UNUSED( p_instance );
    return p_list;
}

void libvlc_module_description_list_release( libvlc_module_description_t *p_list )
{
    libvlc_module_description_t *p_actual, *p_before;
    p_actual = p_list;

    while ( p_actual )
    {
        free( p_actual->psz_name );
        free( p_actual->psz_shortname );
        free( p_actual->psz_longname );
        free( p_actual->psz_help );
        free( p_actual->psz_help_html );
        p_before = p_actual;
        p_actual = p_before->p_next;
        free( p_before );
    }
}

libvlc_module_description_t *libvlc_audio_filter_list_get( libvlc_instance_t *p_instance )
{
    return module_description_list_get( p_instance, "audio filter" );
}

libvlc_module_description_t *libvlc_video_filter_list_get( libvlc_instance_t *p_instance )
{
    return module_description_list_get( p_instance, "video filter" );
}

libvlc_time_t libvlc_clock(void)
{
    return US_FROM_VLC_TICK(vlc_tick_now());
}

const char vlc_module_name[] = "libvlc";
