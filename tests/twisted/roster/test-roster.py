"""
Test basic roster functionality.
"""

import dbus

from gabbletest import exec_test, expect_list_channel
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'

    stream.send(event.stanza)

    # FIXME: this is somewhat fragile - it's asserting the exact order that
    # things currently happen in roster.c. In reality the order is not
    # significant
    expect_list_channel(q, bus, conn, 'publish',
        ['amy@foo.com', 'bob@foo.com'])
    expect_list_channel(q, bus, conn, 'subscribe',
        ['amy@foo.com', 'che@foo.com'])
    expect_list_channel(q, bus, conn, 'stored',
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])

if __name__ == '__main__':
    exec_test(test)
