/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5. The full HDF5 copyright notice, including      *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose: Internal routines for managing plugins.
 *
 */


/****************/
/* Module Setup */
/****************/

#include "H5PLmodule.h"          /* This source code file is part of the H5PL module */


/***********/
/* Headers */
/***********/
#include "H5private.h"      /* Generic Functions            */
#include "H5Eprivate.h"     /* Error handling               */
#include "H5MMprivate.h"    /* Memory management            */
#include "H5PLpkg.h"        /* Plugin                       */
#include "H5Zprivate.h"     /* Filter pipeline              */


/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
hbool_t H5_PKG_INIT_VAR = FALSE;


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

/* Bitmask that controls whether classes of plugins
 * (e.g.: filters, VOL drivers) can be loaded.
 */
static unsigned int     H5PL_plugin_control_mask_g = H5PL_ALL_PLUGIN;

/* This flag will be set to FALSE if the HDF5_PLUGIN_PRELOAD
 * environment variable was set to H5PL_NO_PLUGIN at
 * package initialization.
 */
static hbool_t          H5PL_allow_plugins_g = TRUE;



/*-------------------------------------------------------------------------
 * Function:    H5PL__get_plugin_control_mask
 *
 * Purpose:     Gets the internal plugin control mask value.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5PL__get_plugin_control_mask(unsigned int *mask /*out*/)
{
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Check args - Just assert on package functions */
    HDassert(mask);

    /* Return the mask */
    *mask = H5PL_plugin_control_mask_g;

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5PL__get_plugin_control_mask() */


/*-------------------------------------------------------------------------
 * Function:    H5PL__set_plugin_control_mask
 *
 * Purpose:     Sets the internal plugin control mask value.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5PL__set_plugin_control_mask(unsigned int mask)
{
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Only allow setting this if plugins have not been disabled.
     * XXX: Note that we don't consider this an error, but instead
     *      silently ignore it. We may want to consider this behavior
     *      more carefully.
     */
    if (H5PL_allow_plugins_g)
        H5PL_plugin_control_mask_g = mask;

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5PL__set_plugin_control_mask() */


/*-------------------------------------------------------------------------
 * Function:    H5PL__init_package
 *
 * Purpose:     Initialize any package-specific data and call any init
 *              routines for the package.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5PL__init_package(void)
{
    char        *env_var = NULL;
    herr_t      ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    /* Check the environment variable to determine if the user wants
     * to ignore plugins. The special symbol H5PL_NO_PLUGIN (defined in
     * H5PLpublic.h) means we don't want to load plugins.
     */
    if (NULL != (env_var = HDgetenv("HDF5_PLUGIN_PRELOAD")))
        if (!HDstrcmp(env_var, H5PL_NO_PLUGIN)) {
            H5PL_plugin_control_mask_g = 0;
            H5PL_allow_plugins_g = FALSE;
        }

    /* Create the table of previously-loaded plugins */
    if (H5PL__create_plugin_cache() < 0)
        HGOTO_ERROR(H5E_PLUGIN, H5E_CANTINIT, FAIL, "can't create plugin cache")

    /* Create the table of search paths for dynamic libraries */
    if (H5PL__create_path_table() < 0)
        HGOTO_ERROR(H5E_PLUGIN, H5E_CANTINIT, FAIL, "can't create plugin search path table")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5PL__init_package() */


/*-------------------------------------------------------------------------
 * Function:    H5PL_term_package
 *
 * Purpose:     Terminate the H5PL interface: release all memory, reset all
 *              global variables to initial values. This only happens if all
 *              types have been destroyed from other interfaces.
 *
 * Return:      Success:    Positive if any action was taken that might
 *                          affect some other interface; zero otherwise
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
int
H5PL_term_package(void)
{
    hbool_t already_closed = FALSE;
    int     ret_value = 0;

    FUNC_ENTER_NOAPI_NOINIT

    if (H5_PKG_INIT_VAR) {

        /* Close the plugin cache.
         * We need to bump the return value if we did any real work here.
         */
        if (H5PL__close_plugin_cache(&already_closed) < 0)
            HGOTO_ERROR(H5E_PLUGIN, H5E_CANTFREE, (-1), "problem closing plugin cache")
        if (!already_closed)
            ret_value++;

        /* Close the search path table and free the paths */
        if (H5PL__close_path_table() < 0)
            HGOTO_ERROR(H5E_PLUGIN, H5E_CANTFREE, (-1), "problem closing search path table")

        /* Mark the interface as uninitialized */
        if (0 == ret_value)
            H5_PKG_INIT_VAR = FALSE;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5PL_term_package() */


/*-------------------------------------------------------------------------
 * Function:    H5PL_load
 *
 * Purpose:     Given the plugin type and identifier, this function searches
 *              for and, if found, loads a dynamic plugin library.
 *
 *              The function searches first in the cached plugins and then
 *              in the paths listed in the path table.
 *
 * Return:      Success:    A pointer to the plugin info
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
const void *
H5PL_load(H5PL_type_t type, int id)
{
    H5PL_search_params_t    search_params;
    hbool_t                 found = FALSE;          /* Whether the plugin was found */
    const void             *plugin_info = NULL;
    const void             *ret_value = NULL;

    FUNC_ENTER_NOAPI(NULL)

    /* Check if plugins can be loaded for this plugin type */
    switch (type) {
        case H5PL_TYPE_FILTER:
            if ((H5PL_plugin_control_mask_g & H5PL_FILTER_PLUGIN) == 0)
                HGOTO_ERROR(H5E_PLUGIN, H5E_CANTLOAD, NULL, "required dynamically loaded plugin filter '%d' is not available", id)
            break;

        case H5PL_TYPE_ERROR:
        case H5PL_TYPE_NONE:
        default:
            HGOTO_ERROR(H5E_PLUGIN, H5E_CANTLOAD, NULL, "required dynamically loaded plugin '%d' is not valid", id)
    }

    /* Set up the search parameters */
    search_params.type = type;
    search_params.id = id;

    /* Search in the table of already loaded plugin libraries */
    if(H5PL__find_plugin_in_cache(&search_params, &found, &plugin_info) < 0)
        HGOTO_ERROR(H5E_PLUGIN, H5E_CANTGET, NULL, "search in plugin cache  failed")

    /* If not found, try iterating through the path table to find an appropriate plugin */
    if (!found)
        if (H5PL__find_plugin_in_path_table(&search_params, &found, &plugin_info) < 0)
            HGOTO_ERROR(H5E_PLUGIN, H5E_CANTGET, NULL, "search in path table failed")

    /* Set the return value we found the plugin */
    if (found)
        ret_value = plugin_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5PL_load() */


/*-------------------------------------------------------------------------
 * Function:    H5PL__open
 *
 * Purpose:     Opens a plugin.
 *
 *              The success parameter will be set to TRUE and the plugin_info
 *              parameter will be filled in on success. Otherwise, they
 *              will be FALSE and NULL, respectively.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
/* NOTE: We turn off -Wpedantic in gcc to quiet a warning about converting
 *       object pointers to function pointers, which is undefined in ANSI C.
 *       This is basically unavoidable due to the nature of dlsym() and *is*
 *       defined in POSIX, so it's fine.
 *
 *       This pragma only needs to surround the assignment of the
 *       get_plugin_info function pointer, but early (4.4.7, at least) gcc
 *       only allows diagnostic pragmas to be toggled outside of functions.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
herr_t
H5PL__open(const char *path, H5PL_type_t type, int id, hbool_t *success, const void **plugin_info)
{
    H5PL_HANDLE             handle = NULL;
    H5PL_get_plugin_info_t  get_plugin_info = NULL;
    htri_t                  ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    /* Check args - Just assert on package functions */
    HDassert(path);
    HDassert(success);
    HDassert(plugin_info);

    /* Initialize out parameters */
    *success = FALSE;
    *plugin_info = NULL;

    /* There are different reasons why a library can't be open, e.g. wrong architecture.
     * If we can't open the library, just return.
     */
    if (NULL == (handle = H5PL_OPEN_DLIB(path))) {
        H5PL_CLR_ERROR; /* clear error */
        HGOTO_DONE(SUCCEED);
    }

    /* Return a handle for the function H5PLget_plugin_info in the dynamic library.
     * The plugin library is suppose to define this function.
     */
    if (NULL != (get_plugin_info = (H5PL_get_plugin_info_t)H5PL_GET_LIB_FUNC(handle, "H5PLget_plugin_info"))) {

        const H5Z_class2_t *info;

        /* Get the plugin info */
        if (NULL == (info = (const H5Z_class2_t *)(*get_plugin_info)()))
            HGOTO_ERROR(H5E_PLUGIN, H5E_CANTGET, FAIL, "can't get plugin info")

        /* Check if the filter IDs match */
        if (info->id == id) {
            H5Z_class2_t *plugin_copy = NULL;

            /* Store the plugin in the cache */
            if (H5PL__add_plugin(type, id, handle))
                HGOTO_ERROR(H5E_PLUGIN, H5E_CANTINSERT, FAIL, "unable to add new plugin to plugin cache")

            /* allocate local copy of plugin info */
            if (NULL == (plugin_copy = (H5Z_class2_t *)H5MM_calloc(sizeof(H5Z_class2_t))))
                HGOTO_ERROR(H5E_PLUGIN, H5E_CANTALLOC, FAIL, "can't allocate memory for plugin info")
            /* Set the plugin info to return */
            *plugin_info = (const void *)info;

            plugin_copy->version = info->version;
            plugin_copy->id = info->id;
            plugin_copy->encoder_present = info->encoder_present;
            plugin_copy->decoder_present = info->decoder_present;
            plugin_copy->can_apply = info->can_apply;
            plugin_copy->set_local = info->set_local;
            plugin_copy->filter = info->filter;
            /* copy the user's string into the property */
            if(NULL == (plugin_copy->name = (char *)H5MM_xstrdup(info->name)))
                HGOTO_ERROR(H5E_PLUGIN, H5E_NOSPACE, FAIL, "can't allocate memory for plugin info name")

            /* Set output parameters */
            *success = TRUE;
        }
    }

done:
    if (!success && handle)
        if (*plugin_info)
            *plugin_info = (H5Z_class2_t *)H5MM_xfree(*plugin_info);
        if (H5PL__close(handle) < 0)
            HDONE_ERROR(H5E_PLUGIN, H5E_CLOSEERROR, FAIL, "can't close dynamic library")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5PL__open() */
#pragma GCC diagnostic pop


/*-------------------------------------------------------------------------
 * Function:    H5PL__close
 *
 * Purpose:     Closes the handle for dynamic library
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5PL__close(H5PL_HANDLE handle)
{
    FUNC_ENTER_PACKAGE_NOERR

    H5PL_CLOSE_LIB(handle);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5PL__close() */

