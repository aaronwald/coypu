#!/usr/bin/python

import ssl
import websocket
#from websocket import create_connection
from json import dumps, loads
from pprint import pprint

ws = websocket.WebSocket(sslopt={"cert_reqs": ssl.CERT_NONE})
ws.connect("wss://ws-sandbox.kraken.com")
print("Sending 'Hello, World'...")

pairs = ["ADA/CAD", "ADA/ETH", "ADA/EUR", "ADA/USD", "ADA/XBT", "BCH/EUR", "BCH/USD", "BCH/XBT", "BSV/EUR", "BSV/USD", "BSV/XBT", "DASH/EUR", "DASH/USD", "DASH/XBT", "EOS/ETH", "EOS/EUR", "EOS/USD", "EOS/XBT", "GNO/ETH", "GNO/EUR", "GNO/USD", "GNO/XBT", "QTUM/CAD", "QTUM/ETH", "QTUM/EUR", "QTUM/USD", "QTUM/XBT", "USDT/USD", "ETC/ETH", "ETC/XBT", "ETC/EUR", "ETC/USD", "ETH/XBT", "ETH/CAD", "ETH/EUR", "ETH/GBP", "ETH/JPY", "ETH/USD", "LTC/XBT", "LTC/EUR", "LTC/USD", "MLN/ETH", "MLN/XBT", "REP/ETH", "REP/XBT", "REP/EUR", "REP/USD", "STR/EUR", "STR/USD", "XBT/CAD", "XBT/EUR", "XBT/GBP", "XBT/JPY", "XBT/USD", "BTC/CAD", "BTC/EUR", "BTC/GBP", "BTC/JPY", "BTC/USD", "XDG/XBT", "XLM/XBT", "DOGE/XBT", "STR/XBT", "XLM/EUR", "XLM/USD", "XMR/XBT", "XMR/EUR", "XMR/USD", "XRP/XBT", "XRP/CAD", "XRP/EUR", "XRP/JPY", "XRP/USD", "ZEC/XBT", "ZEC/EUR", "ZEC/JPY", "ZEC/USD", "XTZ/CAD", "XTZ/ETH", "XTZ/EUR", "XTZ/USD", "XTZ/XBT"]
pairs1 = ["BTC/USD"]

params = {
    "event": "subscribe",
    "pair": pairs1,
    "subscription": { "name" : "*"}
    }

print (dumps(params))
ws.send(dumps(params))

x = range(0,100000)
for y in x:
    result =  ws.recv()
    if result:
        print(pprint(loads(result)))

ws.close()

e
