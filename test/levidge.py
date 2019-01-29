#!/usr/bin/python

import ssl
import websocket
#from websocket import create_connection
from json import dumps, loads
from pprint import pprint

ws = websocket.WebSocket(sslopt={"cert_reqs": ssl.CERT_NONE})
ws.connect("wss://levidge.com")

params = {
    "type": "subscribe",
    "channels": [{"name": "level2", "product_ids": ["ETH-USD"]}]
}
print (dumps(params))
ws.send(dumps(params))

x = range(0,100000)
for y in x:
    result =  ws.recv()
    if result:
        print("Received '%s'" % pprint(loads(result)))

ws.close()

