/*
 * vcard-manager.h - vCard lookup helper for Gabble connections
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GABBLE_VCARD_MANAGER_H__
#define __GABBLE_VCARD_MANAGER_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleVCardManagerPrivate GabbleVCardManagerPrivate;
typedef struct _GabbleVCardManagerClass GabbleVCardManagerClass;
typedef struct _GabbleVCardManagerRequest GabbleVCardManagerRequest;
typedef struct _GabbleVCardManagerEditRequest GabbleVCardManagerEditRequest;

/**
 * GabbleVCardManagerError:
 * @GABBLE_VCARD_MANAGER_ERROR_CANCELLED: The vCard request was cancelled
 * @GABBLE_VCARD_MANAGER_ERROR_TIMEOUT: The vCard request timed out
 * @GABBLE_VCARD_MANAGER_ERROR_UNKNOWN: An unknown error occured
 */
typedef enum
{
  GABBLE_VCARD_MANAGER_ERROR_CANCELLED,
  GABBLE_VCARD_MANAGER_ERROR_TIMEOUT,
  GABBLE_VCARD_MANAGER_ERROR_UNKNOWN
} GabbleVCardManagerError;

GQuark gabble_vcard_manager_error_quark (void);
#define GABBLE_VCARD_MANAGER_ERROR gabble_vcard_manager_error_quark ()

GType gabble_vcard_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_VCARD_MANAGER \
  (gabble_vcard_manager_get_type ())
#define GABBLE_VCARD_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_VCARD_MANAGER, \
                              GabbleVCardManager))
#define GABBLE_VCARD_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_VCARD_MANAGER, \
                           GabbleVCardManagerClass))
#define GABBLE_IS_VCARD_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_VCARD_MANAGER))
#define GABBLE_IS_VCARD_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_VCARD_MANAGER))
#define GABBLE_VCARD_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_VCARD_MANAGER, \
                              GabbleVCardManagerClass))

struct _GabbleVCardManagerClass {
    GObjectClass parent_class;
};

struct _GabbleVCardManager {
    GObject parent;
    GabbleVCardManagerPrivate *priv;
};

typedef void (*GabbleVCardManagerCb)(GabbleVCardManager *self,
                                    GabbleVCardManagerRequest *request,
                                    TpHandle handle,
                                    LmMessageNode *vcard,
                                    GError *error,
                                    gpointer user_data);

GabbleVCardManager *gabble_vcard_manager_new (GabbleConnection *);

GQuark gabble_vcard_manager_cache_quark (void);

GabbleVCardManagerRequest *gabble_vcard_manager_request (GabbleVCardManager *,
                                                       TpHandle,
                                                       guint timeout,
                                                       GabbleVCardManagerCb,
                                                       gpointer user_data,
                                                       GObject *object);

void gabble_vcard_manager_cancel_request (GabbleVCardManager *manager,
                                          GabbleVCardManagerRequest *request);

const gchar *gabble_vcard_manager_get_cached_alias (GabbleVCardManager *,
                                                   TpHandle);
gboolean gabble_vcard_manager_has_cached_alias (GabbleVCardManager *manager,
                                                TpHandle handle);

gboolean gabble_vcard_manager_get_cached (GabbleVCardManager *,
                                          TpHandle,
                                          LmMessageNode **);
void gabble_vcard_manager_invalidate_cache (GabbleVCardManager *, TpHandle);

typedef void (*GabbleVCardManagerEditCb)(GabbleVCardManager *self,
                                         GabbleVCardManagerEditRequest *request,
                                         LmMessageNode *vcard,
                                         GError *error,
                                         gpointer user_data);

GabbleVCardManagerEditRequest *gabble_vcard_manager_edit (GabbleVCardManager *,
                                                          guint timeout,
                                                          GabbleVCardManagerEditCb,
                                                          gpointer user_data,
                                                          GObject *object,
                                                          size_t n_pairs,
                                                          ...);


void gabble_vcard_manager_remove_edit_request (GabbleVCardManagerEditRequest *);

gchar *vcard_get_avatar_sha1 (LmMessageNode *vcard);

/* For unit tests only */
void gabble_vcard_manager_set_suspend_reply_timeout (guint timeout);
void gabble_vcard_manager_set_default_request_timeout (guint timeout);

G_END_DECLS

#endif
