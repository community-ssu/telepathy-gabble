/*
 * gabble-muc-channel.h - Header for GabbleMucChannel
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#ifndef __GABBLE_MUC_CHANNEL_H__
#define __GABBLE_MUC_CHANNEL_H__

#include <glib-object.h>

#include <loudmouth/loudmouth.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/message-mixin.h>
#include <telepathy-glib/properties-mixin.h>

#include "types.h"

G_BEGIN_DECLS

typedef enum {
    MUC_STATE_CREATED = 0,
    MUC_STATE_INITIATED,
    MUC_STATE_AUTH,
    MUC_STATE_JOINED,
    MUC_STATE_ENDED,
} GabbleMucState;

typedef struct _GabbleMucChannelClass GabbleMucChannelClass;
typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelClass {
    GObjectClass parent_class;

    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleMucChannel {
    GObject parent;

    TpGroupMixin group;
    TpPropertiesMixin properties;
    TpMessageMixin message_mixin;

    GabbleMucChannelPrivate *priv;
};

GType gabble_muc_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MUC_CHANNEL \
  (gabble_muc_channel_get_type ())
#define GABBLE_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MUC_CHANNEL, \
                              GabbleMucChannel))
#define GABBLE_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MUC_CHANNEL,\
                           GabbleMucChannelClass))
#define GABBLE_IS_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MUC_CHANNEL))
#define GABBLE_IS_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MUC_CHANNEL))
#define GABBLE_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MUC_CHANNEL,\
                              GabbleMucChannelClass))

gboolean _gabble_muc_channel_is_ready (GabbleMucChannel *chan);
void _gabble_muc_channel_presence_error (GabbleMucChannel *chan,
    const gchar *jid, LmMessageNode *pres_node);
void _gabble_muc_channel_member_presence_updated (GabbleMucChannel *chan,
    TpHandle handle, LmMessage *message, LmMessageNode *x_node,
    LmMessageNode *item_node);
void _gabble_muc_channel_handle_subject (GabbleMucChannel *chan,
    TpChannelTextMessageType msg_type, TpHandleType handle_type,
    TpHandle sender, time_t timestamp, const gchar *subject, LmMessage *msg);
void _gabble_muc_channel_receive (GabbleMucChannel *chan,
    TpChannelTextMessageType msg_type, TpHandleType handle_type,
    TpHandle sender, time_t timestamp, const gchar *id, const gchar *text,
    LmMessage *msg, TpChannelTextSendError send_error,
    TpDeliveryStatus delivery_status);

void _gabble_muc_channel_state_receive (GabbleMucChannel *chan,
    guint state, guint from_handle);

gboolean gabble_muc_channel_send_presence (GabbleMucChannel *chan,
    GError **error);

gboolean gabble_muc_channel_send_invite (GabbleMucChannel *self,
    const gchar *jid, const gchar *message, GError **error);

G_END_DECLS

#endif /* #ifndef __GABBLE_MUC_CHANNEL_H__*/
