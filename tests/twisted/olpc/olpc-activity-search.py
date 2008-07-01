"""
test OLPC search activity
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq, sync_stream

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

NS_PUBSUB = "http://jabber.org/protocol/pubsub"
NS_DISCO_INFO = "http://jabber.org/protocol/disco#info"
NS_DISCO_ITEMS = "http://jabber.org/protocol/disco#items"

NS_AMP = "http://jabber.org/protocol/amp"
NS_STANZA = "urn:ietf:params:xml:ns:xmpp-stanzas"

def check_view(view, conn, activities, buddies):
    act = view.GetActivities()
    assert sorted(act) == sorted(activities)

    handles = view.GetBuddies()
    assert sorted(conn.InspectHandles(1, handles)) == sorted(buddies)

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    # announce Gadget service
    reply = make_result_iq(stream, disco_event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    item = query.addElement((None, 'item'))
    item['jid'] = 'gadget.localhost'
    stream.send(reply)

    # wait for Gadget disco#info query
    event = q.expect('stream-iq', to='gadget.localhost', query_ns=NS_DISCO_INFO)
    reply = make_result_iq(stream, event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    identity = query.addElement((None, 'identity'))
    identity['category'] = 'collaboration'
    identity['type'] = 'gadget'
    identity['name'] = 'OLPC Gadget'
    feature = query.addElement((None, 'feature'))
    feature['var'] = NS_OLPC_BUDDY
    feature = query.addElement((None, 'feature'))
    feature['var'] = NS_OLPC_ACTIVITY
    stream.send(reply)

    activity_prop_iface = dbus.Interface(conn,
            'org.laptop.Telepathy.ActivityProperties')
    buddy_prop_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    sync_stream(q, stream)

    # request 3 random activities (view 0)
    call_async(q, gadget_iface, 'RequestRandomActivities', 3)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=NS_OLPC_ACTIVITY),
        EventPattern('dbus-return', method='RequestRandomActivities'))

    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '0'
    random = xpath.queryForNodes('/iq/view/random', iq_event.stanza)
    assert len(random) == 1
    assert random[0]['max'] == '3'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room1@conference.localhost'
    activity['id'] = 'activity1'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#005FE4,#00A0FF')
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'lucien@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBCC,#CCBBAA')
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'jean@localhost'
    stream.send(reply)

    ## Current views ##
    # view 0: activity 1 (with: Lucien, Jean)

    view_path = return_event.value[0]
    view0 = bus.get_object(conn.bus_name, view_path)
    view0_iface = dbus.Interface(view0, 'org.laptop.Telepathy.View')

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    room1_handle, props = event.args
    assert conn.InspectHandles(2, [room1_handle])[0] == 'room1@conference.localhost'
    assert props == {'color': '#005FE4,#00A0FF'}

    # participants are added to view
    event = q.expect('dbus-signal', signal='BuddiesChanged')
    members_handles, removed = event.args
    assert sorted(conn.InspectHandles(1, members_handles)) == \
            sorted(['lucien@localhost', 'jean@localhost'])

    event = q.expect('dbus-signal', signal='ActivitiesChanged')
    added, removed = event.args
    assert len(added) == 1
    id, room1_handle = added[0]
    assert id == 'activity1'
    assert sorted(conn.InspectHandles(2, [room1_handle])) == \
            ['room1@conference.localhost']

    # check activities and buddies in view
    check_view(view0_iface, conn, added, ['lucien@localhost',
        'jean@localhost'])

    # we can now get activity properties
    props = activity_prop_iface.GetProperties(room1_handle)
    assert props == {'color': '#005FE4,#00A0FF'}

    # and we can get participant's properties too
    props = buddy_prop_iface.GetProperties(members_handles[0])
    assert props == {'color': '#AABBCC,#CCBBAA'}

    # and their activities
    call_async (q, buddy_prop_iface, 'GetActivities', members_handles[0])

    event = q.expect('stream-iq', to='lucien@localhost', query_name='pubsub',
            query_ns=NS_PUBSUB)
    iq = event.stanza
    # return an error, we can't query pubsub node
    reply = IQ(stream, "error")
    reply['id'] = iq['id']
    reply['from'] = iq['to']
    pubsub = reply.addElement((NS_PUBSUB, 'pubsub'))
    items = pubsub.addElement((None, 'items'))
    items['node'] = 'http://laptop.org/xmpp/activities'
    error = reply.addElement((None, 'error'))
    error['type'] = 'auth'
    error.addElement((NS_STANZA, 'not-authorized'))
    error.addElement(("%s#errors" % NS_PUBSUB, 'presence-subscription-required'))
    stream.send(reply)

    event = q.expect('dbus-return', method='GetActivities')
    activities = event.value[0]
    assert activities == [('activity1', room1_handle)]

    # activity search by properties (view 1)
    props = {'color': '#AABBCC,#001122'}
    call_async(q, gadget_iface, 'SearchActivitiesByProperties', props)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=NS_OLPC_ACTIVITY),
        EventPattern('dbus-return', method='SearchActivitiesByProperties'))

    properties = xpath.queryForNodes('/iq/view/activity/properties/property',
            iq_event.stanza)
    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '1'
    assert len(properties) == 1
    property = properties[0]
    assert property['type'] == 'str'
    assert property['name'] == 'color'
    assert property.children == ['#AABBCC,#001122']

    # reply to request
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room2@conference.localhost'
    activity['id'] = 'activity2'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBCC,#001122')
    stream.send(reply)

    ## Current views ##
    # view 0: activity 1 (with: Lucien, Jean)
    # view 1: activity 2

    view_path = return_event.value[0]
    view1 = bus.get_object(conn.bus_name, view_path)
    view1_iface = dbus.Interface(view1, 'org.laptop.Telepathy.View')

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    room2_handle, props = event.args
    assert conn.InspectHandles(2, [room2_handle])[0] == 'room2@conference.localhost'
    assert props == {'color': '#AABBCC,#001122'}

    event = q.expect('dbus-signal', signal='ActivitiesChanged')
    added, removed = event.args
    assert removed == []
    assert added == [('activity2', room2_handle)]

    act = view1.GetActivities()
    assert sorted(act) == sorted(added)

    assert view1_iface.GetBuddies() == []

    # activity search by participants (view 2)
    participants = conn.RequestHandles(1, ["alice@localhost", "bob@localhost"])
    call_async(q, gadget_iface, 'SearchActivitiesByParticipants', participants)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost',
            query_ns=NS_OLPC_ACTIVITY),
        EventPattern('dbus-return', method='SearchActivitiesByParticipants'))

    buddies = xpath.queryForNodes('/iq/view/activity/buddy', iq_event.stanza)
    view = iq_event.stanza.firstChildElement()
    assert view.name == 'view'
    assert view['id'] == '2'
    assert len(buddies) == 2
    assert (buddies[0]['jid'], buddies[1]['jid']) == ('alice@localhost',
            'bob@localhost')

    # reply to request
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    view = xpath.queryForNodes('/iq/view', reply)[0]
    activity = view.addElement((None, "activity"))
    activity['room'] = 'room3@conference.localhost'
    activity['id'] = 'activity3'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBCC,#001122')
    stream.send(reply)

    ## Current views ##
    # view 0: activity 1 (with: Lucien, Jean)
    # view 1: activity 2
    # view 2: activity 3

    view_path = return_event.value[0]
    view2 = bus.get_object(conn.bus_name, view_path)
    view2_iface = dbus.Interface(view2, 'org.laptop.Telepathy.View')

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    room3_handle, props = event.args
    assert conn.InspectHandles(2, [room3_handle])[0] == 'room3@conference.localhost'
    assert props == {'color': '#AABBCC,#001122'}

    event = q.expect('dbus-signal', signal='ActivitiesChanged')
    added, removed = event.args
    assert removed == []
    assert added == [('activity3', room3_handle)]

    act = view2.GetActivities()
    assert sorted(act) == sorted(added)

    # add activity 4 to view 0
    message = domish.Element((None, 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'
    added = message.addElement((NS_OLPC_ACTIVITY, 'added'))
    added['id'] = '0'
    activity = added.addElement((None, 'activity'))
    activity['id'] = 'activity4'
    activity['room'] = 'room4@conference.localhost'
    properties = activity.addElement((NS_OLPC_ACTIVITY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#DDEEDD,#EEDDEE')
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'fernand@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    buddy = activity.addElement((None, 'buddy'))
    buddy['jid'] = 'jean@localhost'
    property.addContent('#AABBAA,#BBAABB')
    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    ## Current views ##
    # view 0: activity 1 (with: Lucien, Jean), activity 4 (with: Fernand, Jean)
    # view 1: activity 2
    # view 2: activity 3
    # participants are added to view

    event = q.expect('dbus-signal', signal='BuddiesChanged')
    members_handles, removed = event.args
    assert sorted(conn.InspectHandles(1, members_handles)) == \
            sorted(['fernand@localhost', 'jean@localhost'])

    # activity is added too
    event = q.expect('dbus-signal', signal='ActivitiesChanged')
    added, removed = event.args
    assert len(added) == 1
    id, room4_handle = added[0]
    assert id == 'activity4'
    assert sorted(conn.InspectHandles(2, [room4_handle])) == \
            ['room4@conference.localhost']

    # check activities and buddies in view
    check_view(view0_iface, conn, [
        ('activity1', room1_handle),('activity4', room4_handle)],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost'])

    # Gadget informs us about an activity properties change
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'

    change = message.addElement((NS_OLPC_ACTIVITY, 'change'))
    change['activity'] = 'activity1'
    change['room'] = 'room1@conference.localhost'
    change['id'] = '0'
    properties = change.addElement((NS_OLPC_ACTIVITY_PROPS, 'properties'))
    property = properties.addElement((None, 'property'))
    property['type'] = 'str'
    property['name'] = 'tags'
    property.addContent('game')
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBAA,#BBAABB')

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    event = q.expect('dbus-signal', signal='ActivityPropertiesChanged')
    room = event.args[0]
    properties = event.args[1]

    assert properties == {'tags': 'game', 'color': '#AABBAA,#BBAABB'}

    # we now get the new activity properties
    props = activity_prop_iface.GetProperties(room)
    assert props == {'tags': 'game', 'color': '#AABBAA,#BBAABB'}

    # Marcel joined activity 1
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'

    activity = message.addElement((NS_OLPC_ACTIVITY, 'activity'))
    activity['room'] = 'room1@conference.localhost'
    activity['id'] = '0'
    joined = activity.addElement((None, 'joined'))
    joined['jid'] = 'marcel@localhost'
    properties = joined.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#CCCCCC,#DDDDDD')

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    ## Current views ##
    # view 0: activity 1 (with: Lucien, Jean, Marcel), activity 4 (with
    # Fernand, Jean)
    # view 1: activity 2
    # view 2: activity 3

    view_event, buddy_info_event, activities_changed_event = q.expect_many(
            EventPattern('dbus-signal', signal='BuddiesChanged'),
            EventPattern('dbus-signal', signal='PropertiesChanged'),
            EventPattern('dbus-signal', signal='ActivitiesChanged'))

    added, removed = view_event.args
    assert conn.InspectHandles(1, added) == ['marcel@localhost']

    contact, properties = buddy_info_event.args
    assert contact == added[0]
    assert properties == {'color': '#CCCCCC,#DDDDDD'}

    # check activities and buddies in view
    check_view(view0_iface, conn, [
        ('activity1', room1_handle),('activity4', room4_handle)],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost',
            'marcel@localhost'])

    contact, activities = activities_changed_event.args
    assert contact == added[0]
    assert activities == [('activity1', room1_handle)]

    # Marcel left activity 1
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'

    activity = message.addElement((NS_OLPC_ACTIVITY, 'activity'))
    activity['room'] = 'room1@conference.localhost'
    activity['id'] = '0'
    left = activity.addElement((None, 'left'))
    left['jid'] = 'marcel@localhost'

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    ## Current views ##
    # view 0: activity 1 (with: Lucien, Jean), activity 4 (with Fernand, Jean)
    # view 1: activity 2
    # view 2: activity 3

    view_event, activities_changed_event = q.expect_many(
            EventPattern('dbus-signal', signal='BuddiesChanged'),
            EventPattern('dbus-signal', signal='ActivitiesChanged'))

    # check activities and buddies in view
    check_view(view0_iface, conn, [
        ('activity1', room1_handle),('activity4', room4_handle)],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost'])

    contact, activities = activities_changed_event.args
    assert conn.InspectHandles(1, [contact]) == ['marcel@localhost']
    assert activities == []

    # Jean left activity 1
    message = domish.Element(('jabber:client', 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'

    activity = message.addElement((NS_OLPC_ACTIVITY, 'activity'))
    activity['room'] = 'room1@conference.localhost'
    activity['id'] = '0'
    left = activity.addElement((None, 'left'))
    left['jid'] = 'jean@localhost'

    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    ## Current views ##
    # view 0: activity 1 (with: Lucien), activity 4 (with Fernand, Jean)
    # view 1: activity 2
    # view 2: activity 3

    activities_changed_event = q.expect('dbus-signal',
            signal='ActivitiesChanged')
    contact, activities = activities_changed_event.args
    assert conn.InspectHandles(1, [contact]) == ['jean@localhost']

    # Jean wasn't removed from the view as he is still in activity 4
    check_view(view0_iface, conn, [
        ('activity1', room1_handle),('activity4', room4_handle)],
        ['fernand@localhost', 'lucien@localhost', 'jean@localhost'])

    # remove activity 1 from view 0
    message = domish.Element((None, 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'
    removed = message.addElement((NS_OLPC_ACTIVITY, 'removed'))
    removed['id'] = '0'
    activity = removed.addElement((None, 'activity'))
    activity['id'] = 'activity1'
    activity['room'] = 'room1@conference.localhost'
    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    ## Current views ##
    # view 0: activity 4 (with Jean, Fernand)
    # view 1: activity 2
    # view 2: activity 3

    buddies_changed_event, _, buddies_activities_changed_event = \
            q.expect_many(
            EventPattern('dbus-signal', signal='BuddiesChanged'),
    # activity is removed from the view
            EventPattern('dbus-signal', signal='ActivitiesChanged',
                interface='org.laptop.Telepathy.View',
                args=[[], [('activity1', room1_handle)]]),
            EventPattern('dbus-signal', signal='ActivitiesChanged',
                interface='org.laptop.Telepathy.BuddyInfo'))

    # participants are removed from the view
    added, removed = buddies_changed_event.args
    assert sorted(conn.InspectHandles(1, removed)) == \
            sorted(['lucien@localhost'])

    contact, activities = buddies_activities_changed_event.args
    assert contact == removed[0]
    assert activities == []

    # check activities and buddies in view
    check_view(view0_iface, conn, [
        ('activity4', room4_handle)],
        ['fernand@localhost', 'jean@localhost'])

    # close view 0
    call_async(q, view0_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '0'

    # close view 1
    call_async(q, view1_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '1'

    # close view 2
    call_async(q, view2_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '2'


if __name__ == '__main__':
    exec_test(test)
