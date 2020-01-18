/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/****************/
/* Module Setup */
/****************/

#include "H5Rmodule.h"          /* This source code file is part of the H5R module */


/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                        */
#include "H5ACprivate.h"        /* Metadata cache                           */
#include "H5CXprivate.h"        /* API Contexts                             */
#include "H5Dprivate.h"         /* Datasets                                 */
#include "H5Eprivate.h"         /* Error handling                           */
#include "H5Gprivate.h"         /* Groups                                   */
#include "H5HGprivate.h"        /* Global Heaps                             */
#include "H5Iprivate.h"         /* IDs                                      */
#include "H5MMprivate.h"        /* Memory management                        */
#include "H5Oprivate.h"         /* Object headers                           */
#include "H5Rpkg.h"             /* References                               */
#include "H5Sprivate.h"         /* Dataspaces                               */
#include "H5Tprivate.h"         /* Datatypes                                */

/****************/
/* Local Macros */
/****************/

#define H5R_MAX_STRING_LEN  (1 << 16)   /* Max encoded string length    */

/* Encode macro */
#define H5R_ENCODE(func, val, buf, buf_size, actual, m) do {\
    size_t __nalloc = buf_size;                             \
    if(func(val, buf, &__nalloc) < 0)                       \
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, m) \
    if(buf && buf_size >= __nalloc) {                       \
        buf += __nalloc;                                    \
        buf_size -= __nalloc;                               \
    }                                                       \
    actual += __nalloc;                                     \
} while(0)

#define H5R_ENCODE_VAR(func, var, size, buf, buf_size, actual, m) do {  \
    size_t __nalloc = buf_size;                                         \
    if(func(var, size, buf, &__nalloc) < 0)                             \
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, m)             \
    if(buf && buf_size >= __nalloc) {                                   \
        p += __nalloc;                                                  \
        buf_size -= __nalloc;                                           \
    }                                                                   \
    actual += __nalloc;                                                 \
} while(0)

/* Decode macro */
#define H5R_DECODE(func, val, buf, buf_size, actual, m) do {\
    size_t __nbytes = buf_size;                             \
    if(func(buf, &__nbytes, val) < 0)                       \
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, m) \
    buf += __nbytes;                                        \
    buf_size -= __nbytes;                                   \
    actual += __nbytes;                                     \
} while(0)

#define H5R_DECODE_VAR(func, var, size, buf, buf_size, actual, m) do {  \
    size_t __nbytes = buf_size;                                         \
    if(func(buf, &__nbytes, var, size) < 0)                             \
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, m)             \
    p += __nbytes;                                                      \
    buf_size -= __nbytes;                                               \
    actual += __nbytes;                                                 \
} while(0)

/* Debug */
//#define H5R_DEBUG
#ifdef H5R_DEBUG
#define H5R_LOG_DEBUG(...) do {                                 \
      HDfprintf(stdout, " # %s(): ", __func__);                 \
      HDfprintf(stdout, __VA_ARGS__);                           \
      HDfprintf(stdout, "\n");                                  \
      HDfflush(stdout);                                         \
  } while (0)
static const char *
H5R__print_token(const H5VL_token_t token) {
    static char string[64];
    HDsnprintf(string, 64, "%zu", *(haddr_t *)token);
    return string;
}
#else
#define H5R_LOG_DEBUG(...) do { } while (0)
#endif

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Local Prototypes */
/********************/

static herr_t H5R__encode_obj_token(const H5VL_token_t *obj_token, size_t token_size, unsigned char *buf, size_t *nalloc);
static herr_t H5R__decode_obj_token(const unsigned char *buf, size_t *nbytes, H5VL_token_t *obj_token, uint8_t *token_size);
static herr_t H5R__encode_region(H5S_t *space, unsigned char *buf, size_t *nalloc);
static herr_t H5R__decode_region(const unsigned char *buf, size_t *nbytes, H5S_t **space_ptr);
static herr_t H5R__encode_string(const char *string, unsigned char *buf, size_t *nalloc);
static herr_t H5R__decode_string(const unsigned char *buf, size_t *nbytes, char **string_ptr);

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

/* Flag indicating "top" of interface has been initialized */
static hbool_t H5R_top_package_initialize_s = FALSE;


/*--------------------------------------------------------------------------
NAME
   H5R__init_package -- Initialize interface-specific information
USAGE
    herr_t H5R__init_package()

RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.

--------------------------------------------------------------------------*/
herr_t
H5R__init_package(void)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Mark "top" of interface as initialized */
    H5R_top_package_initialize_s = TRUE;

    /* Sanity check, if assert fails, H5R_REF_BUF_SIZE must be increased */
    HDcompile_assert(sizeof(H5R_ref_priv_t) <= H5R_REF_BUF_SIZE);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5R__init_package() */


/*--------------------------------------------------------------------------
 NAME
    H5R_top_term_package
 PURPOSE
    Terminate various H5R objects
 USAGE
    void H5R_top_term_package()
 RETURNS
    void
 DESCRIPTION
    Release IDs for the atom group, deferring full interface shutdown
    until later (in H5R_term_package).
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
     Can't report errors...
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
int
H5R_top_term_package(void)
{
    int	n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Mark closed if initialized */
    if(H5R_top_package_initialize_s)
        if(0 == n)
            H5R_top_package_initialize_s = FALSE;

    FUNC_LEAVE_NOAPI(n)
} /* end H5R_top_term_package() */


/*--------------------------------------------------------------------------
 NAME
    H5R_term_package
 PURPOSE
    Terminate various H5R objects
 USAGE
    void H5R_term_package()
 RETURNS
    void
 DESCRIPTION
    Release the atom group and any other resources allocated.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
     Can't report errors...

     Finishes shutting down the interface, after H5R_top_term_package()
     is called
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
int
H5R_term_package(void)
{
    int	n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(H5_PKG_INIT_VAR) {
        /* Sanity checks */
        HDassert(FALSE == H5R_top_package_initialize_s);

        /* Mark closed */
        if(0 == n)
            H5_PKG_INIT_VAR = FALSE;
    }

    FUNC_LEAVE_NOAPI(n)
} /* end H5R_term_package() */


/*-------------------------------------------------------------------------
 * Function:    H5R__create_object
 *
 * Purpose: Creates an object reference.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__create_object(const H5VL_token_t *obj_token, size_t token_size,
    H5R_ref_priv_t *ref)
{
    size_t encode_size;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(ref);

    /* Create new reference */
    H5MM_memcpy(&ref->ref.obj.token, obj_token, token_size);
    ref->ref.obj.filename = NULL;
    ref->loc_id = H5I_INVALID_HID;
    ref->type = (uint8_t)H5R_OBJECT2;
    ref->token_size = (uint8_t)token_size;

    /* Cache encoding size (assume no external reference) */
    if(H5R__encode(NULL, ref, NULL, &encode_size, 0) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, "unable to determine encoding size")
    ref->encode_size = (uint32_t)encode_size;

    H5R_LOG_DEBUG("Created object reference, %d, filename=%s, obj_addr=%s, encode size=%u",
        (int)sizeof(H5R_ref_priv_t), ref->ref.obj.filename, H5R__print_token(ref->ref.obj.token),
        ref->encode_size);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__create_object() */


/*-------------------------------------------------------------------------
 * Function:    H5R__create_region
 *
 * Purpose: Creates a region reference.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__create_region(const H5VL_token_t *obj_token, size_t token_size,
    H5S_t *space, H5R_ref_priv_t *ref)
{
    size_t encode_size;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(space);
    HDassert(ref);

    /* Create new reference */
    H5MM_memcpy(&ref->ref.obj.token, obj_token, token_size);
    ref->ref.obj.filename = NULL;
    if(NULL == (ref->ref.reg.space = H5S_copy(space, FALSE, TRUE)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "unable to copy dataspace")

    ref->loc_id = H5I_INVALID_HID;
    ref->type = (uint8_t)H5R_DATASET_REGION2;
    ref->token_size = (uint8_t)token_size;

    /* Cache encoding size (assume no external reference) */
    if(H5R__encode(NULL, ref, NULL, &encode_size, 0) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, "unable to determine encoding size")
    ref->encode_size = (uint32_t)encode_size;

    H5R_LOG_DEBUG("Created region reference, %d, filename=%s, obj_addr=%s, encode size=%u",
        (int)sizeof(H5R_ref_priv_t), ref->ref.obj.filename, H5R__print_token(ref->ref.obj.token),
        ref->encode_size);

done:
    if(ret_value < 0)
        if(ref->ref.reg.space) {
            H5S_close(ref->ref.reg.space);
            ref->ref.reg.space = NULL;
        } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5R__create_region */


/*-------------------------------------------------------------------------
 * Function:    H5R__create_attr
 *
 * Purpose: Creates an attribute reference.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__create_attr(const H5VL_token_t *obj_token, size_t token_size,
    const char *attr_name, H5R_ref_priv_t *ref)
{
    size_t encode_size;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(attr_name);
    HDassert(ref);

    /* Make sure that attribute name is not longer than supported encode size */
    if(HDstrlen(attr_name) > H5R_MAX_STRING_LEN)
        HGOTO_ERROR(H5E_REFERENCE, H5E_ARGS, FAIL, "attribute name too long (%d > %d)", (int)HDstrlen(attr_name), H5R_MAX_STRING_LEN)

    /* Create new reference */
    H5MM_memcpy(&ref->ref.obj.token, obj_token, token_size);
    ref->ref.obj.filename = NULL;
    if(NULL == (ref->ref.attr.name = HDstrdup(attr_name)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "Cannot copy attribute name")

    ref->loc_id = H5I_INVALID_HID;
    ref->type = (uint8_t)H5R_ATTR;
    ref->token_size = (uint8_t)token_size;

    /* Cache encoding size (assume no external reference) */
    if(H5R__encode(NULL, ref, NULL, &encode_size, 0) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, "unable to determine encoding size")
    ref->encode_size = (uint32_t)encode_size;

    H5R_LOG_DEBUG("Created attribute reference, %d, filename=%s, obj_addr=%s, attr name=%s, encode size=%u",
        (int)sizeof(H5R_ref_priv_t), ref->ref.obj.filename, H5R__print_token(ref->ref.obj.token),
        ref->ref.attr.name, ref->encode_size);

done:
    if(ret_value < 0) {
        H5MM_xfree(ref->ref.attr.name);
        ref->ref.attr.name = NULL;
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5R__create_attr */


/*-------------------------------------------------------------------------
 * Function:    H5R__destroy
 *
 * Purpose: Destroy reference.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__destroy(H5R_ref_priv_t *ref)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(ref != NULL);

    H5MM_xfree(ref->ref.obj.filename);
    ref->ref.obj.filename = NULL;

    switch(ref->type) {
        case H5R_OBJECT2:
            break;

        case H5R_DATASET_REGION2:
            if(H5S_close(ref->ref.reg.space) < 0)
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTFREE, FAIL, "Cannot close dataspace")
            ref->ref.reg.space = NULL;
            break;

        case H5R_ATTR:
            H5MM_xfree(ref->ref.attr.name);
            ref->ref.attr.name = NULL;
            break;

        case H5R_OBJECT1:
        case H5R_DATASET_REGION1:
            break;
        case H5R_BADTYPE:
        case H5R_MAXTYPE:
            HDassert("invalid reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (invalid reference type)")

        default:
            HDassert("unknown reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (unknown reference type)")
    } /* end switch */

    /* Decrement refcount of attached loc_id */
    if(ref->type && (ref->loc_id != H5I_INVALID_HID)) {
        if(ref->app_ref) {
            if(H5I_dec_app_ref(ref->loc_id) < 0)
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDEC, FAIL, "decrementing location ID failed")
        } else {
            if(H5I_dec_ref(ref->loc_id) < 0)
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDEC, FAIL, "decrementing location ID failed")
        }
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__destroy() */


/*-------------------------------------------------------------------------
 * Function:    H5R__set_loc_id
 *
 * Purpose: Attach location ID to reference and increment location refcount.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__set_loc_id(H5R_ref_priv_t *ref, hid_t id, hbool_t inc_ref, hbool_t app_ref)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(ref != NULL);
    HDassert(id != H5I_INVALID_HID);

    /* If a location ID was previously assigned, decrement refcount and
     * assign new one */
    if((ref->loc_id != H5I_INVALID_HID)) {
        if(ref->app_ref) {
            if(H5I_dec_app_ref(ref->loc_id) < 0)
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDEC, FAIL, "decrementing location ID failed")
        } else {
            if(H5I_dec_ref(ref->loc_id) < 0)
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDEC, FAIL, "decrementing location ID failed")
        }
    }
    ref->loc_id = id;

    /* Prevent location ID from being freed until reference is destroyed,
     * set app_ref if necessary as references are exposed to users and are
     * expected to be destroyed, this allows the loc_id to be cleanly released
     * on shutdown if users fail to call H5Rdestroy(). */
    if(inc_ref && H5I_inc_ref(ref->loc_id, app_ref) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTINC, FAIL, "incrementing location ID failed")
    ref->app_ref = app_ref;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__set_loc_id() */


/*-------------------------------------------------------------------------
 * Function:    H5R__get_loc_id
 *
 * Purpose: Retrieve location ID attached to existing reference.
 *
 * Return:  Valid ID on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5R__get_loc_id(const H5R_ref_priv_t *ref)
{
    hid_t ret_value = H5I_INVALID_HID;  /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    HDassert(ref != NULL);

    ret_value = ref->loc_id;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__get_loc_id() */


/*-------------------------------------------------------------------------
 * Function:    H5R__reopen_file
 *
 * Purpose: Re-open referenced file using file access property list.
 *
 * Return:  Valid ID on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5R__reopen_file(H5R_ref_priv_t *ref, hid_t fapl_id)
{
    H5P_genplist_t *plist;              /* Property list for FAPL */
    void *new_file = NULL;              /* File object opened */
    H5VL_connector_prop_t connector_prop;  /* Property for VOL connector ID & info     */
    H5VL_object_t *vol_obj = NULL;      /* VOL object for file */
    hbool_t supported;                  /* Whether 'post open' operation is supported by VOL connector */
    hid_t ret_value = H5I_INVALID_HID;

    FUNC_ENTER_PACKAGE

    /* TODO add search path */

    /* Verify access property list and set up collective metadata if appropriate */
    if(H5CX_set_apl(&fapl_id, H5P_CLS_FACC, H5I_INVALID_HID, TRUE) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTSET, H5I_INVALID_HID, "can't set access property list info")

    /* Get the VOL info from the fapl */
    if(NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "not a file access property list")
    if(H5P_peek(plist, H5F_ACS_VOL_CONN_NAME, &connector_prop) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTGET, H5I_INVALID_HID, "can't get VOL connector info")

     /* Stash a copy of the "top-level" connector property, before any pass-through
     *  connectors modify or unwrap it.
     */
    if(H5CX_set_vol_connector_prop(&connector_prop) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTSET, H5I_INVALID_HID, "can't set VOL connector info in API context")

    /* Open the file */
    /* (Must open file read-write to allow for object modifications) */
    if(NULL == (new_file = H5VL_file_open(&connector_prop, H5R_REF_FILENAME(ref), H5F_ACC_RDWR, fapl_id, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTOPENFILE, H5I_INVALID_HID, "unable to open file")

    /* Get an ID for the file */
    if((ret_value = H5VL_register_using_vol_id(H5I_FILE, new_file, connector_prop.connector_id, TRUE)) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to atomize file handle")

    /* Get the file object */
    if(NULL == (vol_obj = H5VL_vol_object(ret_value)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTGET, H5I_INVALID_HID, "invalid object identifier")

    /* Make the 'post open' callback */
    supported = FALSE;
    if(H5VL_introspect_opt_query(vol_obj, H5VL_SUBCLS_FILE, H5VL_NATIVE_FILE_POST_OPEN, &supported) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTGET, H5I_INVALID_HID, "can't check for 'post open' operation")
    if(supported)
        if(H5VL_file_optional(vol_obj, H5VL_NATIVE_FILE_POST_OPEN, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTINIT, H5I_INVALID_HID, "unable to make file 'post open' callback")

    /* Attach loc_id to reference */
    if(H5R__set_loc_id((H5R_ref_priv_t *)ref, ret_value, FALSE, TRUE) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTSET, H5I_INVALID_HID, "unable to attach location id to reference")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__reopen_file() */


/*-------------------------------------------------------------------------
 * Function:    H5R__get_type
 *
 * Purpose: Given a reference to some object, return the type of that reference.
 *
 * Return:  Type of the reference
 *
 *-------------------------------------------------------------------------
 */
H5R_type_t
H5R__get_type(const H5R_ref_priv_t *ref)
{
    H5R_type_t ret_value = H5R_BADTYPE;

    FUNC_ENTER_PACKAGE_NOERR

    HDassert(ref != NULL);
    ret_value = (H5R_type_t)ref->type;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__get_type() */


/*-------------------------------------------------------------------------
 * Function:    H5R__equal
 *
 * Purpose: Compare two references
 *
 * Return:  TRUE if equal, FALSE if unequal, FAIL if error
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5R__equal(const H5R_ref_priv_t *ref1, const H5R_ref_priv_t *ref2)
{
    htri_t ret_value = TRUE;

    FUNC_ENTER_PACKAGE

    HDassert(ref1 != NULL);
    HDassert(ref2 != NULL);

    /* Compare reference types */
    if(ref1->type != ref2->type)
        HGOTO_DONE(FALSE);

    /* Compare object addresses */
    if(ref1->token_size != ref2->token_size)
        HGOTO_DONE(FALSE);
    if(0 != HDmemcmp(&ref1->ref.obj.token, &ref2->ref.obj.token, ref1->token_size))
        HGOTO_DONE(FALSE);

    /* Compare filenames */
    if((ref1->ref.obj.filename && (NULL == ref2->ref.obj.filename))
        || ((NULL == ref1->ref.obj.filename) && ref2->ref.obj.filename))
        HGOTO_DONE(FALSE);
    if(ref1->ref.obj.filename && ref1->ref.obj.filename
        && (0 != HDstrcmp(ref1->ref.obj.filename, ref2->ref.obj.filename)))
        HGOTO_DONE(FALSE);

    switch(ref1->type) {
        case H5R_OBJECT2:
            break;
        case H5R_DATASET_REGION2:
            if((ret_value = H5S_extent_equal(ref1->ref.reg.space, ref2->ref.reg.space)) < 0)
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOMPARE, FAIL, "cannot compare dataspace extents")
            break;
        case H5R_ATTR:
            HDassert(ref1->ref.attr.name && ref2->ref.attr.name);
            if(0 != HDstrcmp(ref1->ref.attr.name, ref2->ref.attr.name))
                HGOTO_DONE(FALSE);
            break;
        case H5R_OBJECT1:
        case H5R_DATASET_REGION1:
        case H5R_BADTYPE:
        case H5R_MAXTYPE:
            HDassert("invalid reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (invalid reference type)")
        default:
            HDassert("unknown reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (unknown reference type)")
    } /* end switch */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__equal() */


/*-------------------------------------------------------------------------
 * Function:    H5R__copy
 *
 * Purpose: Copy a reference
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__copy(const H5R_ref_priv_t *src_ref, H5R_ref_priv_t *dst_ref)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert((src_ref != NULL) && (dst_ref != NULL));

    H5MM_memcpy(&dst_ref->ref.obj.token, &src_ref->ref.obj.token, src_ref->token_size);
    dst_ref->encode_size = src_ref->encode_size;
    dst_ref->type = src_ref->type;
    dst_ref->token_size = src_ref->token_size;

    switch(src_ref->type) {
        case H5R_OBJECT2:
            break;
        case H5R_DATASET_REGION2:
            if(NULL == (dst_ref->ref.reg.space = H5S_copy(src_ref->ref.reg.space, FALSE, TRUE)))
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "unable to copy dataspace")
            break;
        case H5R_ATTR:
            if(NULL == (dst_ref->ref.attr.name = HDstrdup(src_ref->ref.attr.name)))
                HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "Cannot copy attribute name")
            break;
        case H5R_OBJECT1:
        case H5R_DATASET_REGION1:
            HDassert("invalid reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (invalid reference type)")
        case H5R_BADTYPE:
        case H5R_MAXTYPE:
        default:
            HDassert("unknown reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (unknown reference type)")
    } /* end switch */

    /* We only need to keep a copy of the filename if we don't have the loc_id */
    if(src_ref->loc_id == H5I_INVALID_HID) {
        HDassert(src_ref->ref.obj.filename);

        if(NULL == (dst_ref->ref.obj.filename = HDstrdup(src_ref->ref.obj.filename)))
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "Cannot copy filename")
        dst_ref->loc_id = H5I_INVALID_HID;
    } else {
        dst_ref->ref.obj.filename = NULL;

        /* Set location ID and hold reference to it */
        if(H5R__set_loc_id(dst_ref, src_ref->loc_id, TRUE, TRUE) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTSET, FAIL, "cannot set reference location ID")
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__copy() */


/*-------------------------------------------------------------------------
 * Function:    H5R__get_obj_token
 *
 * Purpose: Given a reference to some object, get the encoded object addr.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__get_obj_token(const H5R_ref_priv_t *ref, H5VL_token_t *obj_token,
    size_t *token_size)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(ref != NULL);
    HDassert(ref->token_size <= H5VL_MAX_TOKEN_SIZE);

    if(obj_token) {
        if(0 == ref->token_size)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "NULL token size")
        H5MM_memcpy(obj_token, &ref->ref.obj.token, ref->token_size);
    }
    if(token_size)
        *token_size = ref->token_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__get_obj_token() */


/*-------------------------------------------------------------------------
 * Function:    H5R__set_obj_token
 *
 * Purpose: Given a reference to some object, set the encoded object addr.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__set_obj_token(H5R_ref_priv_t *ref, const H5VL_token_t *obj_token,
    size_t token_size)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    HDassert(ref != NULL);
    HDassert(obj_token);
    HDassert(token_size);
    HDassert(token_size <= H5VL_MAX_TOKEN_SIZE);

    H5MM_memcpy(&ref->ref.obj.token, obj_token, ref->token_size);
    ref->token_size = (uint8_t)token_size;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__set_obj_token() */


/*-------------------------------------------------------------------------
 * Function:    H5R__get_region
 *
 * Purpose: Given a reference to some object, creates a copy of the dataset
 * pointed to's dataspace and defines a selection in the copy which is the
 * region pointed to.
 *
 * Return:  Pointer to the dataspace on success/NULL on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__get_region(const H5R_ref_priv_t *ref, H5S_t *space)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    HDassert(ref != NULL);
    HDassert(ref->type == H5R_DATASET_REGION2);
    HDassert(space);

    /* Copy reference selection to destination */
    if(H5S_select_copy(space, ref->ref.reg.space, FALSE) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "unable to copy selection")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__get_region() */


/*-------------------------------------------------------------------------
 * Function:    H5R__get_file_name
 *
 * Purpose: Given a reference to some object, determine a file name of the
 * object located into.
 *
 * Return:  Non-negative length of the path on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
ssize_t
H5R__get_file_name(const H5R_ref_priv_t *ref, char *buf, size_t size)
{
    size_t copy_len;
    ssize_t ret_value = -1;     /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check args */
    HDassert(ref != NULL);

    /* Return if that reference has no filename set */
    if(!ref->ref.obj.filename)
        HGOTO_ERROR(H5E_REFERENCE, H5E_ARGS, (-1), "no filename available for that reference")

    /* Get the file name length */
    copy_len = HDstrlen(ref->ref.obj.filename);
    HDassert(copy_len <= H5R_MAX_STRING_LEN);

    /* Copy the file name */
    if(buf) {
        copy_len = MIN(copy_len, size - 1);
        H5MM_memcpy(buf, ref->ref.obj.filename, copy_len);
        buf[copy_len] = '\0';
    }
    ret_value = (ssize_t)(copy_len + 1);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__get_file_name() */


/*-------------------------------------------------------------------------
 * Function:    H5R__get_attr_name
 *
 * Purpose: Given a reference to some attribute, determine its name.
 *
 * Return:  Non-negative length of the path on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
ssize_t
H5R__get_attr_name(const H5R_ref_priv_t *ref, char *buf, size_t size)
{
    ssize_t ret_value = -1;     /* Return value */
    size_t attr_name_len;       /* Length of the attribute name */

    FUNC_ENTER_PACKAGE_NOERR

    /* Check args */
    HDassert(ref != NULL);
    HDassert(ref->type == H5R_ATTR);

    /* Get the attribute name length */
    attr_name_len = HDstrlen(ref->ref.attr.name);
    HDassert(attr_name_len <= H5R_MAX_STRING_LEN);

    /* Get the attribute name */
    if(buf) {
        size_t copy_len = MIN(attr_name_len, size - 1);
        H5MM_memcpy(buf, ref->ref.attr.name, copy_len);
        buf[copy_len] = '\0';
    }

    ret_value = (ssize_t)(attr_name_len + 1);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__get_attr_name() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode
 *
 * Purpose: Private function for H5Rencode.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__encode(const char *filename, const H5R_ref_priv_t *ref, unsigned char *buf,
    size_t *nalloc, unsigned flags)
{
    uint8_t *p = (uint8_t *)buf;
    size_t buf_size = 0, encode_size = 0;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(ref);
    HDassert(nalloc);

    /**
     * Encoding format:
     * | Reference type (8 bits) | Flags (8 bits) | Token (token size)
     *    |                         |
     *    |                         |----> H5R_IS_EXTERNAL: File info
     *    |
     *    |----> H5R_DATASET_REGION2: Serialized selection
     *    |
     *    |----> H5R_ATTR: Attribute name len + name
     *
     */

    /* Don't encode if buffer size isn't big enough or buffer is empty */
    if(buf && *nalloc >= H5R_ENCODE_HEADER_SIZE) {
        /* Encode the type of the reference */
        *p++ = (uint8_t)ref->type;

        /* Encode the flags */
        *p++ = (uint8_t)flags;

        buf_size = *nalloc - H5R_ENCODE_HEADER_SIZE;
    } /* end if */
    encode_size += H5R_ENCODE_HEADER_SIZE;

    /* Encode object token */
    H5R_ENCODE_VAR(H5R__encode_obj_token, &ref->ref.obj.token, ref->token_size,
        p, buf_size, encode_size, "Cannot encode object address");

    /**
     * TODO Encode VOL info
     * When we have a better way of storing blobs, we should add
     * support for referencing files in external VOLs.
     * There are currently multiple limitations:
     *   - avoid duplicating VOL info on each reference
     *   - must query terminal VOL connector to avoid passthrough confusion
     */
    if(flags & H5R_IS_EXTERNAL)
        /* Encode file name */
        H5R_ENCODE(H5R__encode_string, filename, p, buf_size, encode_size,
            "Cannot encode filename");

    switch(ref->type) {
        case H5R_OBJECT2:
            break;

        case H5R_DATASET_REGION2:
            /* Encode dataspace */
            H5R_ENCODE(H5R__encode_region, ref->ref.reg.space, p, buf_size,
                encode_size, "Cannot encode region");
            break;

        case H5R_ATTR:
            /* Encode attribute name */
            H5R_ENCODE(H5R__encode_string, ref->ref.attr.name, p, buf_size,
                encode_size, "Cannot encode attribute name");
            break;

        case H5R_OBJECT1:
        case H5R_DATASET_REGION1:
        case H5R_BADTYPE:
        case H5R_MAXTYPE:
            HDassert("invalid reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (invalid reference type)")

        default:
            HDassert("unknown reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (unknown reference type)")
    } /* end switch */

    *nalloc = encode_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__encode() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode
 *
 * Purpose: Private function for H5Rdecode.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__decode(const unsigned char *buf, size_t *nbytes, H5R_ref_priv_t *ref)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t buf_size = 0, decode_size = 0;
    uint8_t flags;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(buf);
    HDassert(nbytes);
    HDassert(ref);
    buf_size = *nbytes;

    /* Don't decode if buffer size isn't big enough */
    if(buf_size < H5R_ENCODE_HEADER_SIZE)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Set new reference */
    ref->type = (H5R_type_t)*p++;
    if(ref->type <= H5R_BADTYPE || ref->type >= H5R_MAXTYPE)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid reference type")

    /* Set flags */
    flags = *p++;

    buf_size -= H5R_ENCODE_HEADER_SIZE;
    decode_size += H5R_ENCODE_HEADER_SIZE;

    /* Decode object token */
    H5R_DECODE_VAR(H5R__decode_obj_token, &ref->ref.obj.token, &ref->token_size,
        p, buf_size, decode_size, "Cannot decode object address");

    /* We do not need to store the filename if the reference is internal */
    if(flags & H5R_IS_EXTERNAL) {
        /* Decode file name */
        H5R_DECODE(H5R__decode_string, &ref->ref.obj.filename, p, buf_size,
            decode_size, "Cannot decode filename");
    } else
        ref->ref.obj.filename = NULL;

    switch(ref->type) {
        case H5R_OBJECT2:
            break;
        case H5R_DATASET_REGION2:
            /* Decode dataspace */
            H5R_DECODE(H5R__decode_region, &ref->ref.reg.space, p, buf_size,
                decode_size, "Cannot decode region");
            break;
        case H5R_ATTR:
            /* Decode attribute name */
            H5R_DECODE(H5R__decode_string, &ref->ref.attr.name, p, buf_size,
                decode_size, "Cannot decode attribute name");
            break;
        case H5R_OBJECT1:
        case H5R_DATASET_REGION1:
        case H5R_BADTYPE:
        case H5R_MAXTYPE:
            HDassert("invalid reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (invalid reference type)")
        default:
            HDassert("unknown reference type" && 0);
            HGOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "internal error (unknown reference type)")
    } /* end switch */

    /* Set loc ID to invalid */
    ref->loc_id = H5I_INVALID_HID;

    /* Set encoding size */
    ref->encode_size = (uint32_t)decode_size;

    H5R_LOG_DEBUG("Decoded reference, filename=%s, obj_addr=%s, encode size=%u",
        ref->ref.obj.filename, H5R__print_token(ref->ref.obj.token),
        ref->encode_size);

    *nbytes = decode_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode_obj_token
 *
 * Purpose: Encode an object address.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5R__encode_obj_token(const H5VL_token_t *obj_token, size_t token_size,
    unsigned char *buf, size_t *nalloc)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC_NOERR

    HDassert(nalloc);

    /* Don't encode if buffer size isn't big enough or buffer is empty */
    if(buf && *nalloc >= token_size) {
        uint8_t *p = (uint8_t *)buf;

        /* Encode token size */
        *p++ = (uint8_t)(token_size & 0xff);

        /* Encode token */
        H5MM_memcpy(p, obj_token, token_size);
    }
    *nalloc = token_size + H5_SIZEOF_UINT8_T;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__encode_obj_token() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_obj_token
 *
 * Purpose: Decode an object address.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5R__decode_obj_token(const unsigned char *buf, size_t *nbytes,
    H5VL_token_t *obj_token, uint8_t *token_size)
{
    const uint8_t *p = (const uint8_t *)buf;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(buf);
    HDassert(nbytes);
    HDassert(obj_token);
    HDassert(token_size);

    /* Don't decode if buffer size isn't big enough */
    if(*nbytes < H5_SIZEOF_UINT8_T)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Get token size */
    *token_size = *p++;
    if(*token_size > sizeof(H5VL_token_t))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Invalid token size (%u)", *token_size)

    /* Decode token */
    H5MM_memcpy(obj_token, p, *token_size);

    *nbytes = (size_t)*token_size + H5_SIZEOF_UINT8_T;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode_obj_token() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode_region
 *
 * Purpose: Encode a selection.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5R__encode_region(H5S_t *space, unsigned char *buf, size_t *nalloc)
{
    uint8_t *p = NULL; /* Pointer to data to store */
    hssize_t buf_size = 0;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(space);
    HDassert(nalloc);

    /* Get the amount of space required to serialize the selection */
    if((buf_size = H5S_SELECT_SERIAL_SIZE(space)) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, "Cannot determine amount of space needed for serializing selection")

    /* Don't encode if buffer size isn't big enough or buffer is empty */
    if(buf && *nalloc >= ((size_t)buf_size + 2 * H5_SIZEOF_UINT32_T)) {
        int rank;
        p = (uint8_t *)buf;

        /* Encode the size for safety check */
        UINT32ENCODE(p, (uint32_t)buf_size);

        /* Encode the extent rank */
        if((rank = H5S_get_simple_extent_ndims(space)) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTGET, FAIL, "can't get extent rank for selection")
        UINT32ENCODE(p, (uint32_t)rank);

        /* Serialize the selection */
        if(H5S_SELECT_SERIALIZE(space, (unsigned char **)&p) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTENCODE, FAIL, "can't serialize selection")
    } /* end if */
    *nalloc = (size_t)buf_size + 2 * H5_SIZEOF_UINT32_T;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__encode_region() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_region
 *
 * Purpose: Decode a selection.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5R__decode_region(const unsigned char *buf, size_t *nbytes, H5S_t **space_ptr)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t buf_size = 0;
    unsigned rank;
    H5S_t *space;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(buf);
    HDassert(nbytes);
    HDassert(space_ptr);

    /* Don't decode if buffer size isn't big enough */
    if(*nbytes < (2 * H5_SIZEOF_UINT32_T))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Decode the selection size */
    UINT32DECODE(p, buf_size);
    buf_size += H5_SIZEOF_UINT32_T;

    /* Decode the extent rank */
    UINT32DECODE(p, rank);
    buf_size += H5_SIZEOF_UINT32_T;

    /* Don't decode if buffer size isn't big enough */
    if(*nbytes < buf_size)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Deserialize the selection (dataspaces need the extent rank information) */
    if(NULL == (space = H5S_create(H5S_SIMPLE)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")
    if(H5S_set_extent_simple(space, rank, NULL, NULL) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTSET, FAIL, "can't set extent rank for selection")
    if(H5S_SELECT_DESERIALIZE(&space, &p) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "can't deserialize selection")

    *nbytes = buf_size;
    *space_ptr = space;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode_region() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode_string
 *
 * Purpose: Encode a string.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5R__encode_string(const char *string, unsigned char *buf, size_t *nalloc)
{
    size_t string_len, buf_size;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(string);
    HDassert(nalloc);

    /* Get the amount of space required to serialize the string */
    string_len = HDstrlen(string);
    if(string_len > H5R_MAX_STRING_LEN)
        HGOTO_ERROR(H5E_REFERENCE, H5E_ARGS, FAIL, "string too long")

    /* Compute buffer size, allow for the attribute name length and object address */
    buf_size = string_len + sizeof(uint16_t);

    if(buf && *nalloc >= buf_size) {
        uint8_t *p = (uint8_t *)buf;
        /* Serialize information for string length into the buffer */
        UINT16ENCODE(p, string_len);
        /* Copy the string into the buffer */
        H5MM_memcpy(p, string, string_len);
    }
    *nalloc = buf_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__encode_string() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_string
 *
 * Purpose: Decode a string.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5R__decode_string(const unsigned char *buf, size_t *nbytes, char **string_ptr)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t string_len;
    char *string = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(buf);
    HDassert(nbytes);
    HDassert(string_ptr);

    /* Don't decode if buffer size isn't big enough */
    if(*nbytes < sizeof(uint16_t))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Get the string length */
    UINT16DECODE(p, string_len);
    HDassert(string_len <= H5R_MAX_STRING_LEN);

    /* Allocate the string */
    if(NULL == (string = (char *)H5MM_malloc(string_len + 1)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTALLOC, FAIL, "Cannot allocate string")

     /* Copy the string */
     H5MM_memcpy(string, p, string_len);
     string[string_len] = '\0';

     *string_ptr = string;
     *nbytes = sizeof(uint16_t) + string_len;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode_string() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode_heap
 *
 * Purpose: Encode data and insert into heap (native only).
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__encode_heap(H5F_t *f, unsigned char *buf, size_t *nalloc,
    const unsigned char *data, size_t data_size)
{
    size_t buf_size;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(f);
    HDassert(nalloc);

    buf_size = H5HG_HEAP_ID_SIZE(f);
    if(buf && *nalloc >= buf_size) {
        H5HG_t hobjid;
        uint8_t *p = (uint8_t *)buf;

        /* Write the reference information to disk (allocates space also) */
        if(H5HG_insert(f, data_size, data, &hobjid) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_WRITEERROR, FAIL, "Unable to write reference information")

        /* Encode the heap information */
        H5F_addr_encode(f, &p, hobjid.addr);
        UINT32ENCODE(p, hobjid.idx);
    } /* end if */
    *nalloc = buf_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__encode_heap() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_heap
 *
 * Purpose: Decode data inserted into heap (native only).
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__decode_heap(H5F_t *f, const unsigned char *buf, size_t *nbytes,
    unsigned char **data_ptr, size_t *data_size)
{
    const uint8_t *p = (const uint8_t *)buf;
    H5HG_t hobjid;
    size_t buf_size;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(f);
    HDassert(buf);
    HDassert(nbytes);
    HDassert(data_ptr);

    buf_size = H5HG_HEAP_ID_SIZE(f);
    /* Don't decode if buffer size isn't big enough */
    if(*nbytes < buf_size)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Get the heap information */
    H5F_addr_decode(f, &p, &(hobjid.addr));
    if(!H5F_addr_defined(hobjid.addr) || hobjid.addr == 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "Undefined reference pointer")
    UINT32DECODE(p, hobjid.idx);

    /* Read the information from disk */
    if(NULL == (*data_ptr = (unsigned char *)H5HG_read(f, &hobjid, (void *)*data_ptr, data_size)))
        HGOTO_ERROR(H5E_REFERENCE, H5E_READERROR, FAIL, "Unable to read reference data")

    *nbytes = buf_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode_heap() */


/*-------------------------------------------------------------------------
 * Function:    H5R__free_heap
 *
 * Purpose: Remove data previously inserted into heap (native only).
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__free_heap(H5F_t *f, const unsigned char *buf, size_t nbytes)
{
    H5HG_t hobjid;
    const uint8_t *p = (const uint8_t *)buf;
    size_t buf_size;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(f);
    HDassert(buf);

    buf_size = H5HG_HEAP_ID_SIZE(f);
    /* Don't decode if buffer size isn't big enough */
    if(nbytes < buf_size)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    /* Get the heap information */
    H5F_addr_decode(f, &p, &(hobjid.addr));
    if(!H5F_addr_defined(hobjid.addr) || hobjid.addr == 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "Undefined reference pointer")
    UINT32DECODE(p, hobjid.idx);

    /* Free heap object */
    if(hobjid.addr > 0) {
        /* Free heap object */
        if(H5HG_remove(f, &hobjid) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_WRITEERROR, FAIL, "Unable to remove heap object")
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__free_heap() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_token_compat
 *
 * Purpose: Decode an object token. (native only)
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__decode_token_compat(H5VL_object_t *vol_obj, H5I_type_t type, H5R_type_t ref_type,
    const unsigned char *buf, H5VL_token_t *obj_token)
{
    hid_t file_id = H5I_INVALID_HID;    /* File ID for region reference */
    H5VL_object_t *vol_obj_file = NULL;
    H5VL_file_cont_info_t cont_info = {H5VL_CONTAINER_INFO_VERSION, 0, 0, 0};
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

#ifndef NDEBUG
    {
        hbool_t is_native = FALSE;  /* Whether the src file is using the native VOL connector */

        /* Check if using native VOL connector */
        if(H5VL_object_is_native(vol_obj, &is_native) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTGET, FAIL, "can't query if file uses native VOL connector")

        /* Must use native VOL connector for this operation */
        HDassert(is_native);
    }
#endif /* NDEBUG */

    /* Get the file for the object */
    if((file_id = H5F_get_file_id(vol_obj, type, FALSE)) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file or file object")

    /* Retrieve VOL object */
    if(NULL == (vol_obj_file = H5VL_vol_object(file_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Get container info */
    if(H5VL_file_get((const H5VL_object_t *)vol_obj_file, H5VL_FILE_GET_CONT_INFO, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL, &cont_info) < 0)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTGET, FAIL, "unable to get container info")

    if(ref_type == H5R_OBJECT1) {
        size_t buf_size = H5R_OBJ_REF_BUF_SIZE;

        /* Get object address */
        if(H5R__decode_token_obj_compat(buf, &buf_size, obj_token, cont_info.token_size) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "unable to get object token")
    } /* end if */
    else {
        size_t buf_size = H5R_DSET_REG_REF_BUF_SIZE;
        H5F_t *f = NULL;

        /* Retrieve file from VOL object */
        if(NULL == (f = (H5F_t *)H5VL_object_data((const H5VL_object_t *)vol_obj_file)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid VOL object")

        /* Get object address */
        if(H5R__decode_token_region_compat(f, buf, &buf_size, obj_token, cont_info.token_size, NULL) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "unable to get object address")
    } /* end else */

done:
    if(file_id != H5I_INVALID_HID && H5I_dec_ref(file_id) < 0)
        HDONE_ERROR(H5E_REFERENCE, H5E_CANTDEC, FAIL, "unable to decrement refcount on file")
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode_token_compat() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode_token_obj_compat
 *
 * Purpose: Encode an object token. (native only)
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__encode_token_obj_compat(const H5VL_token_t *obj_token, size_t token_size,
    unsigned char *buf, size_t *nalloc)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE_NOERR

    HDassert(obj_token);
    HDassert(token_size);
    HDassert(nalloc);

    /* Don't encode if buffer size isn't big enough or buffer is empty */
    if(buf && *nalloc >= token_size)
        H5MM_memcpy(buf, obj_token, token_size);

    *nalloc = token_size;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__encode_token_obj_compat() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_token_obj_compat
 *
 * Purpose: Decode an object token. (native only)
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__decode_token_obj_compat(const unsigned char *buf, size_t *nbytes,
    H5VL_token_t *obj_token, size_t token_size)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(buf);
    HDassert(nbytes);
    HDassert(obj_token);
    HDassert(token_size);

    /* Don't decode if buffer size isn't big enough */
    if(*nbytes < token_size)
        HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "Buffer size is too small")

    H5MM_memcpy(obj_token, buf, token_size);

    *nbytes = token_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5R__decode_token_obj_compat() */


/*-------------------------------------------------------------------------
 * Function:    H5R__encode_token_region_compat
 *
 * Purpose: Encode dataset selection and insert data into heap (native only).
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__encode_token_region_compat(H5F_t *f, const H5VL_token_t *obj_token,
    size_t token_size, H5S_t *space, unsigned char *buf, size_t *nalloc)
{
    size_t buf_size;
    unsigned char *data = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(f);
    HDassert(obj_token);
    HDassert(token_size);
    HDassert(space);
    HDassert(nalloc);

    /* Get required buffer size */
    if(H5R__encode_heap(f, NULL, &buf_size, NULL, (size_t)0) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    if(buf && *nalloc >= buf_size) {
        ssize_t data_size;
        uint8_t *p;

        /* Pass the correct encoding version for the selection depending on the
         * file libver bounds, this is later retrieved in H5S hyper encode */
        H5CX_set_libver_bounds(f);

        /* Zero the heap ID out, may leak heap space if user is re-using
         * reference and doesn't have garbage collection turned on
         */
        HDmemset(buf, 0, buf_size);

        /* Get the amount of space required to serialize the selection */
        if((data_size = H5S_SELECT_SERIAL_SIZE(space)) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTINIT, FAIL, "Invalid amount of space for serializing selection")

        /* Increase buffer size to allow for the dataset token */
        data_size += (hssize_t)token_size;

        /* Allocate the space to store the serialized information */
        H5_CHECK_OVERFLOW(data_size, hssize_t, size_t);
        if(NULL == (data = (uint8_t *)H5MM_malloc((size_t)data_size)))
            HGOTO_ERROR(H5E_REFERENCE, H5E_NOSPACE, FAIL, "memory allocation failed")

        /* Serialize information for dataset OID into heap buffer */
        p = (uint8_t *)data;
        H5MM_memcpy(p, obj_token, token_size);
        p += token_size;

        /* Serialize the selection into heap buffer */
        if(H5S_SELECT_SERIALIZE(space, &p) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTCOPY, FAIL, "Unable to serialize selection")

        /* Write to heap */
        if(H5R__encode_heap(f, buf, nalloc, data, (size_t)data_size) < 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")
    }
    *nalloc = buf_size;

done:
    H5MM_free(data);
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5R__encode_token_region_compat() */


/*-------------------------------------------------------------------------
 * Function:    H5R__decode_token_region_compat
 *
 * Purpose: Decode dataset selection from data inserted into heap (native only).
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5R__decode_token_region_compat(H5F_t *f, const unsigned char *buf,
    size_t *nbytes, H5VL_token_t *obj_token, size_t token_size,
    H5S_t **space_ptr)
{
    unsigned char *data = NULL;
    H5VL_token_t token = { 0 };
    size_t data_size;
    const uint8_t *p;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    HDassert(f);
    HDassert(buf);
    HDassert(nbytes);
    HDassert(token_size);

    /* Read from heap */
    if(H5R__decode_heap(f, buf, nbytes, &data, &data_size) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Get object address */
    p = (const uint8_t *)data;
    H5MM_memcpy(&token, p, token_size);
    p += token_size;

    if(space_ptr) {
        H5O_loc_t oloc; /* Object location */
        H5S_t *space = NULL;
        const uint8_t *q = (const uint8_t *)&token;

        /* Initialize the object location */
        H5O_loc_reset(&oloc);
        oloc.file = f;
        H5F_addr_decode(f, &q, &oloc.addr);

        /* Open and copy the dataset's dataspace */
        if(NULL == (space = H5S_read(&oloc)))
            HGOTO_ERROR(H5E_REFERENCE, H5E_NOTFOUND, FAIL, "not found")

        /* Unserialize the selection */
        if(H5S_SELECT_DESERIALIZE(&space, &p) < 0)
            HGOTO_ERROR(H5E_REFERENCE, H5E_CANTDECODE, FAIL, "can't deserialize selection")

        *space_ptr = space;
    }
    if(obj_token)
        H5MM_memcpy(obj_token, &token, token_size);

done:
    H5MM_free(data);
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5R__decode_token_region_compat() */
