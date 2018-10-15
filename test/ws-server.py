#!/usr/bin/env python

# WS server example

import asyncio
import websockets

async def hello(websocket, path):
    print("wait")
#    name = await websocket.recv()
#    print(f"< {name}")
    name = "foozzz"

    greeting = f"Hello {name}!"

    await websocket.send(greeting)
    print(f"> {greeting}")

start_server = websockets.serve(hello, 'localhost', 8765)

asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()

