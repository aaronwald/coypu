#!/usr/bin/python

from websocket import create_connection
from json import dumps, loads
from pprint import pprint

ws = create_connection("wss://ws-feed.pro.coinbase.com")
print("Sending 'Hello, World'...")
params = {
    "type": "subscribe",
    "channels": [{"name": "ticker", "product_ids": ["ETH-USD"]}]
}
print (dumps(params))
ws.send(dumps(params))

x = range(0,10)
for y in x:
    result =  ws.recv()
    print("Received '%s'" % pprint(loads(result)))

ws.close()

