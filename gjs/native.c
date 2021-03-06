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

#include <gmodule.h>

#include <util/log.h>

#include "native.h"
#include "jsapi-util.h"

typedef struct {
    GjsDefineModuleFunc func;
    GjsNativeFlags flags;
} GjsNativeModule;

static GHashTable *modules = NULL;

static void
native_module_free(void *data)
{
    g_slice_free(GjsNativeModule, data);
}

void
gjs_register_native_module (const char            *module_id,
                            GjsDefineModuleFunc  func,
                            GjsNativeFlags       flags)
{
    GjsNativeModule *module;

    if (modules == NULL) {
        modules = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        g_free, native_module_free);
    }

    if (g_hash_table_lookup(modules, module_id) != NULL) {
        g_warning("A second native module tried to register the same id '%s'",
                  module_id);
        return;
    }

    module = g_slice_new(GjsNativeModule);
    module->func = func;
    module->flags = flags;

    g_hash_table_replace(modules,
                         g_strdup(module_id),
                         module);

    gjs_debug(GJS_DEBUG_NATIVE,
              "Registered native JS module '%s'",
              module_id);
}

static JSObject*
module_get_parent(JSContext *context,
                  JSObject  *module_obj)
{
    jsval value;

    if (gjs_object_get_property(context, module_obj, "__parentModule__", &value) &&
        value != JSVAL_NULL &&
        JSVAL_IS_OBJECT(value)) {
        return JSVAL_TO_OBJECT(value);
    } else {
        return NULL;
    }
}

JSBool
gjs_import_native_module(JSContext        *context,
                         JSObject         *module_obj,
                         const char       *filename,
                         GjsNativeFlags *flags_p)
{
    GModule *gmodule;
    GString *module_id;
    JSObject *parent;
    GjsNativeModule *native_module;

    if (flags_p)
        *flags_p = 0;

    /* Vital to load in global scope so any dependent libs
     * are loaded into the main app. We don't want a second
     * private copy of GTK or something.
     */
    gmodule = g_module_open(filename, 0);
    if (gmodule == NULL) {
        gjs_throw(context,
                     "Failed to load '%s': %s",
                     filename, g_module_error());
        return JS_FALSE;
    }

    /* dlopen() as a side effect should have registered us as
     * a native module. We just have to reverse-engineer
     * the module id from module_obj.
     */
    module_id = g_string_new(NULL);
    parent = module_obj;
    while (parent != NULL) {
        jsval value;

        if (gjs_object_get_property(context, parent, "__moduleName__", &value) &&
            JSVAL_IS_STRING(value)) {
            const char *name;
            name = gjs_string_get_ascii(value);

            if (module_id->len > 0)
                g_string_prepend(module_id, ".");

            g_string_prepend(module_id, name);
        }

        /* Move up to parent */
        parent = module_get_parent(context, parent);
    }

    gjs_debug(GJS_DEBUG_NATIVE,
              "Defining native module '%s'",
              module_id->str);

    if (modules != NULL)
        native_module = g_hash_table_lookup(modules, module_id->str);
    else
        native_module = NULL;

    if (native_module == NULL) {
        gjs_throw(context,
                  "No native module '%s' has registered itself",
                  module_id->str);
        g_string_free(module_id, TRUE);
        g_module_close(gmodule);
        return JS_FALSE;
    }

    g_string_free(module_id, TRUE);

    if (flags_p)
        *flags_p = native_module->flags;

    /* make the module resident, which makes the close() a no-op
     * (basically we leak the module permanently)
     */
    g_module_make_resident(gmodule);
    g_module_close(gmodule);

    if (native_module->flags & GJS_NATIVE_SUPPLIES_MODULE_OBJ) {

        /* In this case we just throw away "module_obj" eventually,
         * since the native module defines itself in the parent of
         * module_obj directly.
         */
        parent = module_get_parent(context, module_obj);
        return (* native_module->func) (context, parent);
    } else {
        return (* native_module->func) (context, module_obj);
    }
}

