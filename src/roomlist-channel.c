/*
 * gabble-roomlist-channel.c - Source for GabbleRoomlistChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include "config.h"
#include "roomlist-channel.h"

#include <string.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#define DEBUG_FLAG GABBLE_DEBUG_ROOMLIST

#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "namespaces.h"
#include "util.h"

static void channel_iface_init (gpointer, gpointer);
static void roomlist_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleRoomlistChannel, gabble_roomlist_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_ROOM_LIST,
      roomlist_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL)
    );

static const gchar *gabble_roomlist_channel_interfaces[] = {
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_REQUESTED,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_CONFERENCE_SERVER,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  LAST_PROPERTY
};

/* private structure */

struct _GabbleRoomlistChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  gchar *conference_server;

  gboolean closed;
  gboolean listing;

  gpointer disco_pipeline;
  TpHandleSet *signalled_rooms;

  GPtrArray *pending_room_signals;
  guint timer_source_id;

  gboolean dispose_has_run;
};

#define ROOM_SIGNAL_INTERVAL 300

static gboolean emit_room_signal (gpointer data);

static void
gabble_roomlist_channel_init (GabbleRoomlistChannel *self)
{
  GabbleRoomlistChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannelPrivate);

  self->priv = priv;

  priv->pending_room_signals = g_ptr_array_new ();
}


static GObject *
gabble_roomlist_channel_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRoomlistChannelPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROOMLIST_CHANNEL (obj)->priv;

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_roomlist_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleRoomlistChannel *chan = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv =
    chan->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_TARGET_ID:
      g_value_set_static_string (value, "");
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_INTERFACES:
      g_value_set_boxed (value, gabble_roomlist_channel_interfaces);
      break;
    case PROP_CONFERENCE_SERVER:
      g_value_set_string (value, priv->conference_server);
      break;
    case PROP_INITIATOR_HANDLE:
      /* Room listing is always initiated by the local user */
      g_value_set_uint (value, conn->self_handle);
      break;
    case PROP_INITIATOR_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (conn,
              TP_HANDLE_TYPE_CONTACT);

          g_value_set_string (value,
              tp_handle_inspect (repo, conn->self_handle));
        }
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, TRUE);
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, priv->closed);
      break;
    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (object,
              TP_IFACE_CHANNEL, "TargetHandle",
              TP_IFACE_CHANNEL, "TargetHandleType",
              TP_IFACE_CHANNEL, "ChannelType",
              TP_IFACE_CHANNEL, "TargetID",
              TP_IFACE_CHANNEL, "InitiatorHandle",
              TP_IFACE_CHANNEL, "InitiatorID",
              TP_IFACE_CHANNEL, "Requested",
              TP_IFACE_CHANNEL, "Interfaces",
              TP_IFACE_CHANNEL_TYPE_ROOM_LIST, "Server",
              NULL));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_roomlist_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleRoomlistChannel *chan = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv =
    chan->priv;
  TpBaseConnection *conn;
  TpHandleRepoIface *room_handles;
  TpHandleSet *new_signalled_rooms;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE_TYPE:
    case PROP_HANDLE:
    case PROP_CHANNEL_TYPE:
      /* these properties are writable in the interface, but not actually
       * meaningfully changeable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      conn = (TpBaseConnection *) priv->conn;

      room_handles = tp_base_connection_get_handles (conn,
          TP_HANDLE_TYPE_ROOM);

      new_signalled_rooms = tp_handle_set_new (room_handles);
      if (priv->signalled_rooms != NULL)
        {
          const TpIntSet *add;
          TpIntSet *tmp;
          add = tp_handle_set_peek (priv->signalled_rooms);
          tmp = tp_handle_set_update (new_signalled_rooms, add);
          tp_handle_set_destroy (priv->signalled_rooms);
          tp_intset_destroy (tmp);
        }
      priv->signalled_rooms = new_signalled_rooms;
      break;
    case PROP_CONFERENCE_SERVER:
      g_free (priv->conference_server);
      priv->conference_server = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_roomlist_channel_dispose (GObject *object);
static void gabble_roomlist_channel_finalize (GObject *object);

static void
gabble_roomlist_channel_class_init (GabbleRoomlistChannelClass *gabble_roomlist_channel_class)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { "Requested", "requested", NULL },
      { "InitiatorHandle", "initiator-handle", NULL },
      { "InitiatorID", "initiator-id", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl roomlist_props[] = {
      { "Server", "conference-server", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        roomlist_props,
      },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roomlist_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roomlist_channel_class,
      sizeof (GabbleRoomlistChannelPrivate));

  object_class->constructor = gabble_roomlist_channel_constructor;

  object_class->get_property = gabble_roomlist_channel_get_property;
  object_class->set_property = gabble_roomlist_channel_set_property;

  object_class->dispose = gabble_roomlist_channel_dispose;
  object_class->finalize = gabble_roomlist_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE,
      "handle");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "Currently empty, because this channel always has handle 0.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact who initiated the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator's bare JID",
      "The string obtained by inspecting the initiator-handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this room list channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("conference-server",
      "Name of conference server to use",
      "Name of the XMPP conference server on which to list rooms",
      "",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONFERENCE_SERVER,
      param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  gabble_roomlist_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleRoomlistChannelClass, dbus_props_class));
}

static void stop_listing (GabbleRoomlistChannel *self);

static void
gabble_roomlist_channel_dispose (GObject *object)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv =
    self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  stop_listing (self);

  if (!priv->closed)
    {
      priv->closed = TRUE;
      tp_svc_channel_emit_closed ((TpSvcChannel *) object);
    }

  g_assert (priv->pending_room_signals != NULL);
  g_assert (priv->pending_room_signals->len == 0);
  g_ptr_array_free (priv->pending_room_signals, TRUE);
  priv->pending_room_signals = NULL;

  if (G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->dispose (object);
}

static void
gabble_roomlist_channel_finalize (GObject *object)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv =
    self->priv;

  /* free any data held directly by the object here */

  g_free (priv->object_path);
  g_free (priv->conference_server);

  if (priv->signalled_rooms != NULL)
    tp_handle_set_destroy (priv->signalled_rooms);

  G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->finalize (object);
}

GabbleRoomlistChannel *
_gabble_roomlist_channel_new (GabbleConnection *conn,
                              const gchar *object_path,
                              const gchar *conference_server)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (conference_server != NULL, NULL);

  return GABBLE_ROOMLIST_CHANNEL (
      g_object_new (GABBLE_TYPE_ROOMLIST_CHANNEL,
                    "connection", conn,
                    "object-path", object_path,
                    "conference-server", conference_server, NULL));
}

static gboolean
emit_room_signal (gpointer data)
{
  GabbleRoomlistChannel *chan = data;
  GabbleRoomlistChannelPrivate *priv =
    chan->priv;
  GType room_info_type = TP_STRUCT_TYPE_ROOM_INFO;

  if (!priv->listing)
      return FALSE;

  if (priv->pending_room_signals->len == 0)
      return TRUE;

  tp_svc_channel_type_room_list_emit_got_rooms (
      (TpSvcChannelTypeRoomList *) chan, priv->pending_room_signals);

  while (priv->pending_room_signals->len != 0)
    {
      gpointer boxed = g_ptr_array_index (priv->pending_room_signals, 0);
      g_boxed_free (room_info_type, boxed);
      g_ptr_array_remove_index_fast (priv->pending_room_signals, 0);
    }

  return TRUE;
}

static void
room_info_cb (gpointer pipeline, GabbleDiscoItem *item, gpointer user_data)
{
  GabbleRoomlistChannel *chan = user_data;
  GabbleRoomlistChannelPrivate *priv;
  TpHandleRepoIface *room_handles;
  const char *jid, *category, *type, *var, *name;
  TpHandle handle;
  GHashTable *keys;
  GValue room = {0,};
  GValue *tmp;
  gpointer k, v;
  GType room_info_type = TP_STRUCT_TYPE_ROOM_INFO;

  #define INSERT_KEY(hash, name, type, type2, value) \
    do {\
      tmp = g_slice_new0 (GValue); \
      g_value_init (tmp, (type)); \
      g_value_set_##type2 (tmp, (value)); \
      g_hash_table_insert (hash, (name), tmp); \
    } while (0)

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (chan));
  priv = chan->priv;
  room_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);

  jid = item->jid;
  name = item->name;
  category = item->category;
  type = item->type;

  if (0 != strcmp (category, "conference") ||
      0 != strcmp (type, "text"))
    return;

  if (!g_hash_table_lookup_extended (item->features,
        "http://jabber.org/protocol/muc", &k, &v))
    {
      /* not muc */
      return;
    }

  handle = tp_handle_ensure (room_handles, jid, NULL, NULL);
  if (handle == 0)
    {
      DEBUG ("ignoring listed room with invalid JID '%s'", jid);
      return;
    }

  DEBUG ("got room identity, name=%s, category=%s, type=%s", name,
      category, type);

  keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                (GDestroyNotify) tp_g_value_slice_free);

  INSERT_KEY (keys, "handle-name", G_TYPE_STRING, string,
      tp_handle_inspect (room_handles, handle));
  INSERT_KEY (keys, "name", G_TYPE_STRING, string, name);

  if (g_hash_table_lookup_extended (item->features, "muc_membersonly", &k, &v))
    INSERT_KEY (keys, "invite-only", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_open", &k, &v))
    INSERT_KEY (keys, "invite-only", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features,
        "muc_passwordprotected", &k, &v))
    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_unsecure", &k, &v))
    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_unsecured", &k, &v))
    INSERT_KEY (keys, "password", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_hidden", &k, &v))
    INSERT_KEY (keys, "hidden", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_public", &k, &v))
    INSERT_KEY (keys, "hidden", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_membersonly", &k, &v))
    INSERT_KEY (keys, "members-only", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_open", &k, &v))
    INSERT_KEY (keys, "members-only", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_moderated", &k, &v))
    INSERT_KEY (keys, "moderated", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_unmoderated", &k, &v))
    INSERT_KEY (keys, "moderated", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features,
        "muc_nonanonymous", &k, &v))
    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_anonymous", &k, &v))
    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features,
        "muc_semianonymous", &k, &v))
    INSERT_KEY (keys, "anonymous", G_TYPE_BOOLEAN, boolean, FALSE);
  if (g_hash_table_lookup_extended (item->features, "muc_persistent", &k, &v))
    INSERT_KEY (keys, "persistent", G_TYPE_BOOLEAN, boolean, TRUE);
  if (g_hash_table_lookup_extended (item->features, "muc_temporary", &k, &v))
    INSERT_KEY (keys, "persistent", G_TYPE_BOOLEAN, boolean, FALSE);

  var = g_hash_table_lookup (item->features, "muc#roominfo_description");
  if (var != NULL)
    INSERT_KEY (keys, "description", G_TYPE_STRING, string, var);

  var = g_hash_table_lookup (item->features, "muc#roominfo_occupants");
  if (var != NULL)
    INSERT_KEY (keys, "members", G_TYPE_UINT, uint,
                (guint) g_ascii_strtoull (var, NULL, 10));

  var = g_hash_table_lookup (item->features, "muc#roominfo_lang");
  if (var != NULL)
    INSERT_KEY (keys, "language", G_TYPE_STRING, string, var);

  /* transfer the room handle ref to signalled_rooms */
  tp_handle_set_add (priv->signalled_rooms, handle);
  tp_handle_unref (room_handles, handle);

  g_value_init (&room, room_info_type);
  g_value_take_boxed (&room,
      dbus_g_type_specialized_construct (room_info_type));

  dbus_g_type_struct_set (&room,
      0, handle,
      1, "org.freedesktop.Telepathy.Channel.Type.Text",
      2, keys,
      G_MAXUINT);

  DEBUG ("adding new room signal data to pending: %s", jid);
  g_ptr_array_add (priv->pending_room_signals, g_value_get_boxed (&room));
  g_hash_table_destroy (keys);
}

static void
rooms_end_cb (gpointer data, gpointer user_data)
{
  GabbleRoomlistChannel *chan = user_data;
  GabbleRoomlistChannelPrivate *priv =
    chan->priv;

  emit_room_signal (chan);

  priv->listing = FALSE;
  tp_svc_channel_type_room_list_emit_listing_rooms (
      (TpSvcChannelTypeRoomList *) chan, FALSE);

  g_source_remove (priv->timer_source_id);
  priv->timer_source_id = 0;
}

static void
stop_listing (GabbleRoomlistChannel *self)
{
  GabbleRoomlistChannelPrivate *priv =
    self->priv;

  if (priv->listing)
    {
      emit_room_signal (self);
      priv->listing = FALSE;
      tp_svc_channel_type_room_list_emit_listing_rooms (
          (TpSvcChannelTypeRoomList *) self, FALSE);
    }

  if (priv->disco_pipeline != NULL)
    {
      gabble_disco_pipeline_destroy (priv->disco_pipeline);
      priv->disco_pipeline = NULL;
    }

  if (priv->timer_source_id)
    {
      g_source_remove (priv->timer_source_id);
      priv->timer_source_id = 0;
    }

  g_assert (priv->pending_room_signals->len == 0);
}


/************************* D-Bus Method definitions **************************/

/**
 * gabble_roomlist_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roomlist_channel_close (TpSvcChannel *iface,
                               DBusGMethodInvocation *context)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (iface);
  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  DEBUG ("called on %p", self);

  g_object_run_dispose (G_OBJECT (self));

  tp_svc_channel_return_from_close (context);
}


/**
 * gabble_roomlist_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roomlist_channel_get_channel_type (TpSvcChannel *iface,
                                          DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
}


/**
 * gabble_roomlist_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roomlist_channel_get_handle (TpSvcChannel *self,
                                    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, 0, 0);
}


/**
 * gabble_roomlist_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_roomlist_channel_get_interfaces (TpSvcChannel *iface,
                                        DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      gabble_roomlist_channel_interfaces);
}


/**
 * gabble_roomlist_channel_get_listing_rooms
 *
 * Implements D-Bus method GetListingRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_roomlist_channel_get_listing_rooms (TpSvcChannelTypeRoomList *iface,
                                           DBusGMethodInvocation *context)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (iface);
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  priv = self->priv;
  tp_svc_channel_type_room_list_return_from_get_listing_rooms (
      context, priv->listing);
}


/**
 * gabble_roomlist_channel_list_rooms
 *
 * Implements D-Bus method ListRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_roomlist_channel_list_rooms (TpSvcChannelTypeRoomList *iface,
                                    DBusGMethodInvocation *context)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (iface);
  GabbleRoomlistChannelPrivate *priv;

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  priv = self->priv;

  priv->listing = TRUE;
  tp_svc_channel_type_room_list_emit_listing_rooms (iface, TRUE);

  if (priv->disco_pipeline == NULL)
    priv->disco_pipeline = gabble_disco_pipeline_init (priv->conn->disco,
        room_info_cb, rooms_end_cb, self);

  gabble_disco_pipeline_run (priv->disco_pipeline, priv->conference_server);

  priv->timer_source_id = g_timeout_add (ROOM_SIGNAL_INTERVAL,
      emit_room_signal, self);

  tp_svc_channel_type_room_list_return_from_list_rooms (context);
}

/**
 * gabble_roomlist_channel_stop_listing
 *
 * Implements D-Bus method StopListing
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 */
static void
gabble_roomlist_channel_stop_listing (TpSvcChannelTypeRoomList *iface,
                                      DBusGMethodInvocation *context)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (iface);

  g_assert (GABBLE_IS_ROOMLIST_CHANNEL (self));

  stop_listing (self);

  tp_svc_channel_type_room_list_return_from_stop_listing (context);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_roomlist_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
roomlist_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeRoomListClass *klass =
    (TpSvcChannelTypeRoomListClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_room_list_implement_##x (\
    klass, gabble_roomlist_channel_##x)
  IMPLEMENT(get_listing_rooms);
  IMPLEMENT(list_rooms);
  IMPLEMENT(stop_listing);
#undef IMPLEMENT
}
