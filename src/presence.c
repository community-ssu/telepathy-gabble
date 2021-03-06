/*
 * gabble-presence.c - Gabble's per-contact presence structure
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
#include "presence.h"

#include <string.h>
#include <telepathy-glib/channel-manager.h>

#include "presence-cache.h"
#include "namespaces.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "debug.h"

G_DEFINE_TYPE (GabblePresence, gabble_presence, G_TYPE_OBJECT);

#define GABBLE_PRESENCE_PRIV(account) ((account)->priv)

typedef struct _Resource Resource;

struct _Resource {
    gchar *name;
    GabblePresenceCapabilities caps;
    GHashTable *per_channel_manager_caps;
    guint caps_serial;
    GabblePresenceId status;
    gchar *status_message;
    gint8 priority;
    time_t last_activity;
};

struct _GabblePresencePrivate {
    gchar *no_resource_status_message;
    GSList *resources;
    guint olpc_views;
};

static Resource *
_resource_new (gchar *name)
{
  Resource *new = g_slice_new0 (Resource);
  new->name = name;
  new->caps = PRESENCE_CAP_NONE;
  new->per_channel_manager_caps = NULL;
  new->status = GABBLE_PRESENCE_OFFLINE;
  new->status_message = NULL;
  new->priority = 0;
  new->caps_serial = 0;
  new->last_activity = 0;

  return new;
}

static void
_resource_free (Resource *resource)
{
  g_free (resource->name);
  g_free (resource->status_message);
  if (resource->per_channel_manager_caps != NULL)
    {
      gabble_presence_cache_free_cache_entry
        (resource->per_channel_manager_caps);
      resource->per_channel_manager_caps = NULL;
    }

  g_slice_free (Resource, resource);
}

static void
gabble_presence_finalize (GObject *object)
{
  GSList *i;
  GabblePresence *presence = GABBLE_PRESENCE (object);
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  for (i = priv->resources; NULL != i; i = i->next)
    _resource_free (i->data);

  if (presence->per_channel_manager_caps != NULL)
    {
      gabble_presence_cache_free_cache_entry
        (presence->per_channel_manager_caps);
      presence->per_channel_manager_caps = NULL;
    }

  g_slist_free (priv->resources);
  g_free (presence->nickname);
  g_free (presence->avatar_sha1);
  g_free (priv->no_resource_status_message);
}

static void
gabble_presence_class_init (GabblePresenceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (GabblePresencePrivate));
  object_class->finalize = gabble_presence_finalize;
}

static void
gabble_presence_init (GabblePresence *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PRESENCE, GabblePresencePrivate);
  ((GabblePresencePrivate *) self->priv)->resources = NULL;
}

GabblePresence *
gabble_presence_new (void)
{
  return g_object_new (GABBLE_TYPE_PRESENCE, NULL);
}

static gboolean
resource_better_than (Resource *a, Resource *b)
{
    if (a->priority < 0)
        return FALSE;

    if (NULL == b)
        return TRUE;

    if (a->status < b->status)
        return FALSE;
    else if (a->status > b->status)
        return TRUE;

    if (a->last_activity < b->last_activity)
        return FALSE;
    else if (a->last_activity > b->last_activity)
        return TRUE;

    return (a->priority > b->priority);
}

const gchar *
gabble_presence_pick_resource_by_caps (
    GabblePresence *presence,
    GabblePresenceCapabilities caps)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;
  Resource *chosen = NULL;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (((res->caps & caps) == caps) &&
          (resource_better_than (res, chosen)))
              chosen = res;
    }

  if (chosen)
    return chosen->name;
  else
    return NULL;
}

gboolean
gabble_presence_resource_has_caps (GabblePresence *presence,
                                   const gchar *resource,
                                   GabblePresenceCapabilities caps)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (!tp_strdiff (res->name, resource) && (res->caps & caps) == caps)
        return TRUE;
    }

  return FALSE;
}

void
gabble_presence_set_capabilities (GabblePresence *presence,
                                  const gchar *resource,
                                  GabblePresenceCapabilities caps,
                                  GHashTable *per_channel_manager_caps,
                                  guint serial)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;

  if (resource == NULL && priv->resources != NULL)
    {
      /* This is consistent with the handling of presence: if we get presence
       * from a bare JID, we throw away all the resources, and if we get
       * presence from a resource, any presence we stored from a bare JID is
       * overridden by the aggregated presence.
       */
      DEBUG ("Ignoring caps for NULL resource since we have presence for "
        "some resources");
      return;
    }

  presence->caps = 0;
  if (presence->per_channel_manager_caps != NULL)
    {
      gabble_presence_cache_free_cache_entry
        (presence->per_channel_manager_caps);
      presence->per_channel_manager_caps = NULL;
    }
  presence->per_channel_manager_caps = g_hash_table_new (NULL, NULL);

  if (resource == NULL)
    {
      DEBUG ("adding caps %u to bare jid", caps);
      presence->caps = caps;
      gabble_presence_cache_update_cache_entry (
          presence->per_channel_manager_caps, per_channel_manager_caps);
      return;
    }

  DEBUG ("about to add caps %u to resource %s with serial %u", caps, resource,
    serial);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *tmp = (Resource *) i->data;

      if (0 == strcmp (tmp->name, resource))
        {
          DEBUG ("found resource %s", resource);

          if (serial > tmp->caps_serial)
            {
              DEBUG ("new serial %u, old %u, clearing caps", serial,
                tmp->caps_serial);
              tmp->caps = 0;
              tmp->caps_serial = serial;
            }

          if (serial >= tmp->caps_serial)
            {
              DEBUG ("adding caps %u to resource %s", caps, resource);
              tmp->caps |= caps;
              DEBUG ("resource %s caps now %u", resource, tmp->caps);

              if (tmp->per_channel_manager_caps != NULL)
                {
                  gabble_presence_cache_free_cache_entry
                      (tmp->per_channel_manager_caps);
                  tmp->per_channel_manager_caps = NULL;
                }
              if (per_channel_manager_caps != NULL)
                gabble_presence_cache_copy_cache_entry
                    (&tmp->per_channel_manager_caps, per_channel_manager_caps);
            }
        }

      presence->caps |= tmp->caps;

      if (tmp->per_channel_manager_caps != NULL)
        gabble_presence_cache_update_cache_entry
            (presence->per_channel_manager_caps,
             tmp->per_channel_manager_caps);
    }

  DEBUG ("total caps now %u", presence->caps);
}

static Resource *
_find_resource (GabblePresence *presence, const gchar *resource)
{
  GSList *i;
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (0 == strcmp (res->name, resource))
        return res;
    }

  return NULL;
}

static void
aggregate_resources (GabblePresence *presence)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;
  guint8 prio;

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  presence->caps = 0;
  presence->status = GABBLE_PRESENCE_OFFLINE;

  prio = -128;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *r = (Resource *) i->data;

      presence->caps |= r->caps;

      /* trump existing status & message if it's more present
       * or has the same presence and a higher priority */
      if (r->status > presence->status ||
          (r->status == presence->status && r->priority > prio))
        {
          presence->status = r->status;
          presence->status_message = r->status_message;
          prio = r->priority;
        }
    }

  if (presence->status <= GABBLE_PRESENCE_HIDDEN && priv->olpc_views > 0)
    {
      /* Contact is in at least one view and we didn't receive a better
       * presence from him so announce it as available */
      presence->status = GABBLE_PRESENCE_AVAILABLE;
      g_free (presence->status_message);
      presence->status_message = NULL;
    }
}

gboolean
gabble_presence_update (GabblePresence *presence,
                        const gchar *resource,
                        GabblePresenceId status,
                        const gchar *status_message,
                        gint8 priority)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  Resource *res;
  GabblePresenceId old_status;
  gchar *old_status_message;
  GSList *i;
  gboolean ret = FALSE;

  /* save our current state */
  old_status = presence->status;
  old_status_message = g_strdup (presence->status_message);

  if (NULL == resource)
    {
      /* presence from a JID with no resource: free all resources and set
       * presence directly */

      for (i = priv->resources; i; i = i->next)
        _resource_free (i->data);

      g_slist_free (priv->resources);
      priv->resources = NULL;

      if (tp_strdiff (priv->no_resource_status_message, status_message))
        {
          g_free (priv->no_resource_status_message);
          priv->no_resource_status_message = g_strdup (status_message);
        }

      presence->status = status;
      presence->status_message = priv->no_resource_status_message;
      goto OUT;
    }

  res = _find_resource (presence, resource);

  /* remove, create or update a Resource as appropriate */
  if (status <= GABBLE_PRESENCE_LAST_UNAVAILABLE)
    {
      if (NULL != res)
        {
          priv->resources = g_slist_remove (priv->resources, res);
          _resource_free (res);
          res = NULL;

          /* recalculate aggregate capability mask */
          if (presence->per_channel_manager_caps != NULL)
            {
              gabble_presence_cache_free_cache_entry
                (presence->per_channel_manager_caps);
              presence->per_channel_manager_caps = NULL;
            }
          presence->per_channel_manager_caps = g_hash_table_new (NULL, NULL);
          presence->caps = 0;

          for (i = priv->resources; i; i = i->next)
            {
              Resource *r = (Resource *) i->data;

              presence->caps |= r->caps;

              if (r->per_channel_manager_caps != NULL)
                gabble_presence_cache_update_cache_entry
                    (presence->per_channel_manager_caps,
                     r->per_channel_manager_caps);
            }
        }
    }
  else
    {
      if (NULL == res)
        {
          res = _resource_new (g_strdup (resource));
          priv->resources = g_slist_append (priv->resources, res);
        }

      res->status = status;

      if (tp_strdiff (res->status_message, status_message))
        {
          g_free (res->status_message);
          res->status_message = g_strdup (status_message);
        }

      res->priority = priority;

      if (res->status >= GABBLE_PRESENCE_AVAILABLE)
          res->last_activity = time (NULL);
    }

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  presence->caps = 0;
  presence->status = GABBLE_PRESENCE_OFFLINE;

  /* use the status message from any offline Resource we're
   * keeping around just because it has a message on it */
  presence->status_message = res ? res->status_message : NULL;

  aggregate_resources (presence);

OUT:
  /* detect changes */
  if (presence->status != old_status ||
      tp_strdiff (presence->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}

LmMessage *
gabble_presence_as_message (GabblePresence *presence,
                            const gchar *to)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  LmMessage *message;
  LmMessageNode *node, *subnode;
  LmMessageSubType subtype;
  Resource *res = priv->resources->data; /* pick first resource */

  g_assert (NULL != res);

  if (presence->status == GABBLE_PRESENCE_OFFLINE)
    subtype = LM_MESSAGE_SUB_TYPE_UNAVAILABLE;
  else
    subtype = LM_MESSAGE_SUB_TYPE_AVAILABLE;

  message = lm_message_new_with_sub_type (to, LM_MESSAGE_TYPE_PRESENCE,
              subtype);
  node = lm_message_get_node (message);

  switch (presence->status)
    {
    case GABBLE_PRESENCE_AVAILABLE:
    case GABBLE_PRESENCE_OFFLINE:
    case GABBLE_PRESENCE_HIDDEN:
      break;
    case GABBLE_PRESENCE_AWAY:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_AWAY);
      break;
    case GABBLE_PRESENCE_CHAT:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_CHAT);
      break;
    case GABBLE_PRESENCE_DND:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_DND);
      break;
    case GABBLE_PRESENCE_XA:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_XA);
      break;
    default:
      g_critical ("%s: Unexpected Telepathy presence type", G_STRFUNC);
      break;
    }

  if (presence->status_message)
    lm_message_node_add_child (node, "status", presence->status_message);

  if (res->priority)
    {
      gchar *priority = g_strdup_printf ("%d", res->priority);
      lm_message_node_add_child (node, "priority", priority);
      g_free (priority);
    }

  subnode = lm_message_node_add_child (node, "x", "");
  lm_message_node_set_attribute (subnode, "xmlns", NS_VCARD_TEMP_UPDATE);
  /* NULL means we make no particular assertion about the avatar. */
  if (presence->avatar_sha1 != NULL)
    {
      lm_message_node_add_child (subnode, "photo", presence->avatar_sha1);
    }

  return message;
}

gchar *
gabble_presence_dump (GabblePresence *presence)
{
  GSList *i;
  GString *ret = g_string_new ("");
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  g_string_append_printf (ret,
    "nickname: %s\n"
    "accumulated status: %d\n"
    "accumulated status msg: %s\n"
    "accumulated capabilities: %d\n"
    "kept while unavailable: %d\n"
    "resources:\n", presence->nickname, presence->status,
    presence->status_message, presence->caps,
    presence->keep_unavailable);

  for (i = priv->resources; i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      g_string_append_printf (ret,
        "  %s\n"
        "    capabilities: %d\n"
        "    status: %d\n"
        "    status msg: %s\n"
        "    priority: %d\n", res->name, res->caps, res->status,
        res->status_message, res->priority);
    }

  if (priv->resources == NULL)
    g_string_append_printf (ret, "  (none)\n");

  return g_string_free (ret, FALSE);
}

gboolean
gabble_presence_added_to_view (GabblePresence *self)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (self);
  GabblePresenceId old_status;
  gchar *old_status_message;
  gboolean ret = FALSE;

  /* save our current state */
  old_status = self->status;
  old_status_message = g_strdup (self->status_message);

  priv->olpc_views++;
  aggregate_resources (self);

  /* detect changes */
  if (self->status != old_status ||
      tp_strdiff (self->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}

gboolean
gabble_presence_removed_from_view (GabblePresence *self)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (self);
  GabblePresenceId old_status;
  gchar *old_status_message;
  gboolean ret = FALSE;

  /* save our current state */
  old_status = self->status;
  old_status_message = g_strdup (self->status_message);

  priv->olpc_views--;
  aggregate_resources (self);

  /* detect changes */
  if (self->status != old_status ||
      tp_strdiff (self->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}
