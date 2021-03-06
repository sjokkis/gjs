/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <config.h>

#include <util/log.h>

#include "value.h"
#include "closure.h"
#include "arg.h"
#include "param.h"
#include "object.h"
#include "boxed.h"
#include "union.h"
#include <gjs/gjs.h>

#include <girepository.h>

static JSBool gjs_value_from_g_value_internal(JSContext    *context,
                                              jsval        *value_p,
                                              const GValue *gvalue,
                                              gboolean      no_copy);

static void
closure_marshal(GClosure        *closure,
                GValue          *return_value,
                guint            n_param_values,
                const GValue    *param_values,
                gpointer         invocation_hint,
                gpointer         marshal_data)
{
    JSContext *context;
    int argc;
    jsval *argv;
    jsval rval;
    int i;
    GSignalQuery signal_query = { 0, };

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Marshal closure %p",
                      closure);

    context = gjs_closure_get_context(closure);
    if (context == NULL) {
        /* We were destroyed; become a no-op */
        return;
    }

    JS_BeginRequest(context);

    argc = n_param_values;
    argv = g_newa(jsval, n_param_values);
    rval = JSVAL_VOID;

    gjs_set_values(context, argv, argc, JSVAL_VOID);
    gjs_root_value_locations(context, argv, argc);
    JS_AddRoot(context, &rval);

    if (marshal_data) {
        /* we are used for a signal handler */
        guint signal_id;

        signal_id = GPOINTER_TO_UINT(marshal_data);

        g_signal_query(signal_id, &signal_query);

        if (!signal_query.signal_id) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Signal handler being called on invalid signal");
            goto cleanup;
        }

        if (signal_query.n_params + 1 != n_param_values) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Signal handler being called with wrong number of parameters");
            goto cleanup;
        }
    }

    for (i = 0; i < argc; ++i) {
        const GValue *gval = &param_values[i];
        gboolean no_copy;

        no_copy = FALSE;

        if (i >= 1 && signal_query.signal_id) {
            no_copy = (signal_query.param_types[i - 1] & G_SIGNAL_TYPE_STATIC_SCOPE) != 0;
        }

        if (!gjs_value_from_g_value_internal(context, &argv[i], gval, no_copy)) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Unable to convert arg %d in order to invoke closure",
                      i);
            gjs_log_exception(context, NULL);
            goto cleanup;
        }
    }

    gjs_closure_invoke(closure, argc, argv, &rval);

    if (return_value != NULL) {
        if (rval == JSVAL_VOID) {
            /* something went wrong invoking, error should be set already */
            goto cleanup;
        }

        if (!gjs_value_to_g_value(context, rval, return_value)) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Unable to convert return value when invoking closure");
            gjs_log_exception(context, NULL);
            goto cleanup;
        }
    }

 cleanup:
    gjs_unroot_value_locations(context, argv, argc);
    JS_RemoveRoot(context, &rval);
    JS_EndRequest(context);
}

GClosure*
gjs_closure_new_for_signal(JSContext  *context,
                           JSObject   *callable,
                           const char *description,
                           guint       signal_id)
{
    GClosure *closure;

    closure = gjs_closure_new(context, callable, description);

    g_closure_set_meta_marshal(closure, GUINT_TO_POINTER(signal_id), closure_marshal);

    return closure;
}

GClosure*
gjs_closure_new_marshaled (JSContext    *context,
                           JSObject     *callable,
                           const char   *description)
{
    GClosure *closure;

    closure = gjs_closure_new(context, callable, description);

    g_closure_set_marshal(closure, closure_marshal);

    return closure;
}

static GType
gjs_value_guess_g_type(jsval value)
{
    if (JSVAL_IS_NULL(value))
        return G_TYPE_POINTER;

    if (JSVAL_IS_STRING(value))
        return G_TYPE_STRING;

    if (JSVAL_IS_INT(value))
        return G_TYPE_INT;

    if (JSVAL_IS_DOUBLE(value))
        return G_TYPE_DOUBLE;

    if (JSVAL_IS_BOOLEAN(value))
        return G_TYPE_BOOLEAN;

    if (JSVAL_IS_OBJECT(value)) {
        return G_TYPE_OBJECT;
    }

    return G_TYPE_INVALID;
}

static JSBool
gjs_value_to_g_value_internal(JSContext    *context,
                              jsval         value,
                              GValue       *gvalue,
                              gboolean      no_copy)
{
    GType gtype;

    gtype = G_VALUE_TYPE(gvalue);

    if (gtype == 0) {
        gtype = gjs_value_guess_g_type(value);

        if (gtype == G_TYPE_INVALID) {
            gjs_throw(context, "Could not guess unspecified GValue type");
            return JS_FALSE;
        }

        gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                          "Guessed GValue type %s from JS Value",
                          g_type_name(gtype));

        g_value_init(gvalue, gtype);
    }

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Converting jsval to gtype %s",
                      g_type_name(gtype));


    if (gtype == G_TYPE_STRING) {
        /* Don't use ValueToString since we don't want to just toString()
         * everything automatically
         */
        if (JSVAL_IS_NULL(value)) {
            g_value_set_string(gvalue, NULL);
        } else if (JSVAL_IS_STRING(value)) {
            gchar *utf8_string;

            if (!gjs_string_to_utf8(context, value, &utf8_string))
                return JS_FALSE;

            g_value_set_string(gvalue, utf8_string);
            g_free(utf8_string);
        } else {
            gjs_throw(context,
                      "Wrong type %s; string expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_CHAR) {
        gint32 i;
        if (JS_ValueToInt32(context, value, &i) && i >= SCHAR_MIN && i <= SCHAR_MAX) {
            g_value_set_char(gvalue, (signed char)i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; char expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_UCHAR) {
        guint16 i;
        if (JS_ValueToUint16(context, value, &i) && i <= UCHAR_MAX) {
            g_value_set_uchar(gvalue, (unsigned char)i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; unsigned char expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_INT) {
        gint32 i;
        if (JS_ValueToInt32(context, value, &i)) {
            g_value_set_int(gvalue, i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; integer expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_DOUBLE) {
        gdouble d;
        if (JS_ValueToNumber(context, value, &d)) {
            g_value_set_double(gvalue, d);
        } else {
            gjs_throw(context,
                      "Wrong type %s; double expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_FLOAT) {
        gdouble d;
        if (JS_ValueToNumber(context, value, &d)) {
            g_value_set_float(gvalue, d);
        } else {
            gjs_throw(context,
                      "Wrong type %s; float expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_UINT) {
        guint32 i;
        if (JS_ValueToECMAUint32(context, value, &i)) {
            g_value_set_uint(gvalue, i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; unsigned integer expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (gtype == G_TYPE_BOOLEAN) {
        JSBool b;

        /* JS_ValueToBoolean() pretty much always succeeds,
         * which is maybe surprising sometimes, but could
         * be handy also...
         */
        if (JS_ValueToBoolean(context, value, &b)) {
            g_value_set_boolean(gvalue, b);
        } else {
            gjs_throw(context,
                      "Wrong type %s; boolean expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        GObject *gobj;

        gobj = NULL;
        if (JSVAL_IS_NULL(value)) {
            /* nothing to do */
        } else if (JSVAL_IS_OBJECT(value)) {
            JSObject *obj;
            obj = JSVAL_TO_OBJECT(value);
            gobj = gjs_g_object_from_object(context, obj);
        } else {
            gjs_throw(context,
                      "Wrong type %s; object %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return JS_FALSE;
        }

        g_value_set_object(gvalue, gobj);
    } else if (gtype == G_TYPE_STRV) {
        if (JSVAL_IS_NULL(value)) {
            /* do nothing */
        } else if (gjs_object_has_property(context,
                                           JSVAL_TO_OBJECT(value),
                                           "length")) {
            jsval length_value;
            guint32 length;

            if (!gjs_object_require_property(context,
                                             JSVAL_TO_OBJECT(value), NULL,
                                             "length",
                                             &length_value) ||
                !JS_ValueToECMAUint32(context, length_value, &length)) {
                gjs_throw(context,
                          "Wrong type %s; strv expected",
                          gjs_get_type_name(value));
                return JS_FALSE;
            } else {
                void *result;
                char **strv;

                if (!gjs_array_to_strv (context,
                                        value,
                                        length, &result))
                    return JS_FALSE;
                /* cast to strv in a separate step to avoid type-punning */
                strv = result;
                g_value_take_boxed (gvalue, strv);
            }
        } else {
            gjs_throw(context,
                      "Wrong type %s; strv expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
        void *gboxed;

        gboxed = NULL;
        if (JSVAL_IS_NULL(value)) {
            /* nothing to do */
        } else if (JSVAL_IS_OBJECT(value)) {
            JSObject *obj;
            obj = JSVAL_TO_OBJECT(value);
            gboxed = gjs_c_struct_from_boxed(context, obj);
        } else {
            gjs_throw(context,
                      "Wrong type %s; boxed type %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return JS_FALSE;
        }

        if (no_copy)
            g_value_set_static_boxed(gvalue, gboxed);
        else
            g_value_set_boxed(gvalue, gboxed);
    } else if (g_type_is_a(gtype, G_TYPE_ENUM)) {
        if (JSVAL_IS_INT(value)) {
            GEnumValue *v;

            v = g_enum_get_value(G_ENUM_CLASS(g_type_class_peek(gtype)),
                                 JSVAL_TO_INT(value));
            if (v == NULL) {
                gjs_throw(context,
                          "%d is not a valid value for enumeration %s",
                          JSVAL_TO_INT(value), g_type_name(gtype));
                return JS_FALSE;
            }

            g_value_set_enum(gvalue, v->value);
        } else {
            gjs_throw(context,
                         "Wrong type %s; enum %s expected",
                         gjs_get_type_name(value),
                         g_type_name(gtype));
            return JS_FALSE;
        }
    } else if (g_type_is_a(gtype, G_TYPE_FLAGS)) {
        if (JSVAL_IS_INT(value)) {
            if (!_gjs_flags_value_is_valid(context, gtype, JSVAL_TO_INT(value)))
                return JS_FALSE;

            g_value_set_flags(gvalue, value);
        } else {
            gjs_throw(context,
                      "Wrong type %s; flags %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return JS_FALSE;
        }
    } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
        void *gparam;

        gparam = NULL;
        if (JSVAL_IS_NULL(value)) {
            /* nothing to do */
        } else if (JSVAL_IS_OBJECT(value)) {
            JSObject *obj;
            obj = JSVAL_TO_OBJECT(value);
            gparam = gjs_g_param_from_param(context, obj);
        } else {
            gjs_throw(context,
                      "Wrong type %s; param type %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return JS_FALSE;
        }

        g_value_set_param(gvalue, gparam);
    } else if (g_type_is_a(gtype, G_TYPE_POINTER)) {
        if (JSVAL_IS_NULL(value)) {
            /* Nothing to do */
        } else {
            gjs_throw(context,
                      "Cannot convert non-null JS value to G_POINTER");
            return JS_FALSE;
        }
    } else if (JSVAL_IS_NUMBER(value) &&
               g_value_type_transformable(G_TYPE_INT, gtype)) {
        /* Only do this crazy gvalue transform stuff after we've
         * exhausted everything else. Adding this for
         * e.g. ClutterUnit.
         */
        gint32 i;
        if (JS_ValueToInt32(context, value, &i)) {
            GValue int_value = { 0, };
            g_value_init(&int_value, G_TYPE_INT);
            g_value_set_int(&int_value, i);
            g_value_transform(&int_value, gvalue);
        } else {
            gjs_throw(context,
                      "Wrong type %s; integer expected",
                      gjs_get_type_name(value));
            return JS_FALSE;
        }
    } else {
        gjs_debug(GJS_DEBUG_GCLOSURE, "jsval is number %d gtype fundamental %d transformable to int %d from int %d",
                  JSVAL_IS_NUMBER(value),
                  G_TYPE_IS_FUNDAMENTAL(gtype),
                  g_value_type_transformable(gtype, G_TYPE_INT),
                  g_value_type_transformable(G_TYPE_INT, gtype));

        gjs_throw(context,
                  "Don't know how to convert JavaScript object to GType %s",
                  g_type_name(gtype));
        return JS_FALSE;
    }

    return JS_TRUE;
}

JSBool
gjs_value_to_g_value(JSContext    *context,
                     jsval         value,
                     GValue       *gvalue)
{
    return gjs_value_to_g_value_internal(context, value, gvalue, FALSE);
}

JSBool
gjs_value_to_g_value_no_copy(JSContext    *context,
                             jsval         value,
                             GValue       *gvalue)
{
    return gjs_value_to_g_value_internal(context, value, gvalue, TRUE);
}

static JSBool
gjs_value_from_g_value_internal(JSContext    *context,
                                jsval        *value_p,
                                const GValue *gvalue,
                                gboolean      no_copy)
{
    GType gtype;

    gtype = G_VALUE_TYPE(gvalue);

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Converting gtype %s to jsval",
                      g_type_name(gtype));

    if (gtype == G_TYPE_STRING) {
        const char *v;
        v = g_value_get_string(gvalue);
        if (v == NULL) {
            gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                              "Converting NULL string to JSVAL_NULL");
            *value_p = JSVAL_NULL;
        } else {
            if (!gjs_string_from_utf8(context, v, -1, value_p))
                return JS_FALSE;
        }
    } else if (gtype == G_TYPE_CHAR) {
        char v;
        v = g_value_get_char(gvalue);
        *value_p = INT_TO_JSVAL(v);
    } else if (gtype == G_TYPE_UCHAR) {
        unsigned char v;
        v = g_value_get_uchar(gvalue);
        *value_p = INT_TO_JSVAL(v);
    } else if (gtype == G_TYPE_INT) {
        int v;
        v = g_value_get_int(gvalue);
        return JS_NewNumberValue(context, v, value_p);
    } else if (gtype == G_TYPE_UINT) {
        uint v;
        v = g_value_get_uint(gvalue);
        return JS_NewNumberValue(context, v, value_p);
    } else if (gtype == G_TYPE_DOUBLE) {
        double d;
        d = g_value_get_double(gvalue);
        return JS_NewNumberValue(context, d, value_p);
    } else if (gtype == G_TYPE_FLOAT) {
        double d;
        d = g_value_get_float(gvalue);
        return JS_NewNumberValue(context, d, value_p);
    } else if (gtype == G_TYPE_BOOLEAN) {
        gboolean v;
        v = g_value_get_boolean(gvalue);
        *value_p = BOOLEAN_TO_JSVAL(v);
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        GObject *gobj;
        JSObject *obj;

        gobj = g_value_get_object(gvalue);

        obj = gjs_object_from_g_object(context, gobj);
        *value_p = OBJECT_TO_JSVAL(obj);
    } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
        GjsBoxedCreationFlags boxed_flags;
        GIBaseInfo *info;
        void *gboxed;
        JSObject *obj;

        gboxed = g_value_get_boxed(gvalue);
        boxed_flags = GJS_BOXED_CREATION_NONE;

        /* The only way to differentiate unions and structs is from
         * their g-i info as both GBoxed */
        info = g_irepository_find_by_gtype(g_irepository_get_default(),
                                           gtype);
        if (info == NULL) {
            gjs_throw(context,
                      "No introspection information found for %s",
                      g_type_name(gtype));
            return JS_FALSE;
        }

        switch (g_base_info_get_type(info)) {
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_STRUCT:
            if (no_copy)
                boxed_flags |= GJS_BOXED_CREATION_NO_COPY;
            obj = gjs_boxed_from_c_struct(context, (GIStructInfo *)info, gboxed, boxed_flags);
            break;
        case GI_INFO_TYPE_UNION:
            obj = gjs_union_from_c_union(context, (GIUnionInfo *)info, gboxed);
            break;
        default:
            gjs_throw(context,
                      "Unexpected introspection type %d for %s",
                      g_base_info_get_type(info),
                      g_type_name(gtype));
            return JS_FALSE;
        }
        *value_p = OBJECT_TO_JSVAL(obj);
    } else if (g_type_is_a(gtype, G_TYPE_ENUM)) {
        int v;
        v = g_value_get_enum(gvalue);
        *value_p = INT_TO_JSVAL(v);
    } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
        GParamSpec *gparam;
        JSObject *obj;

        gparam = g_value_get_param(gvalue);

        obj = gjs_param_from_g_param(context, gparam);
        *value_p = OBJECT_TO_JSVAL(obj);
    } else if (g_type_is_a(gtype, G_TYPE_POINTER)) {
        gpointer pointer;

        pointer = g_value_get_pointer(gvalue);

        if (pointer == NULL) {
            *value_p = JSVAL_NULL;
        } else {
            gjs_throw(context,
                      "Can't convert non-null pointer to JS value");
            return JS_FALSE;
        }
    } else if (g_value_type_transformable(gtype, G_TYPE_DOUBLE)) {
        GValue double_value = { 0, };
        double v;
        g_value_init(&double_value, G_TYPE_DOUBLE);
        g_value_transform(gvalue, &double_value);
        v = g_value_get_double(&double_value);
        return JS_NewNumberValue(context, v, value_p);
    } else if (g_value_type_transformable(gtype, G_TYPE_INT)) {
        GValue int_value = { 0, };
        int v;
        g_value_init(&int_value, G_TYPE_INT);
        g_value_transform(gvalue, &int_value);
        v = g_value_get_int(&int_value);
        return JS_NewNumberValue(context, v, value_p);
    } else {
        gjs_throw(context,
                  "Don't know how to convert GType %s to JavaScript object",
                  g_type_name(gtype));
        return JS_FALSE;
    }

    return JS_TRUE;
}

JSBool
gjs_value_from_g_value(JSContext    *context,
                       jsval        *value_p,
                       const GValue *gvalue)
{
    return gjs_value_from_g_value_internal(context, value_p, gvalue, FALSE);
}
