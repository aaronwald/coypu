#!/usr/bin/env python

from websocket import create_connection
import json

if __name__ == "__main__":
    ws = create_connection("ws://localhost:8080/websocket")
    ws.send(json.dumps({"cmd": "mark", "seqno": 0}))

    while True:
        result =  ws.recv()
        print result

    ws.close()
