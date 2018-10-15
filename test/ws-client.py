#!/usr/bin/python

from Queue import Queue
from websocket import create_connection
from threading import Thread
import curses

stopped = False
ws_queue = Queue()

def do_websocket():
    ws = create_connection("ws://localhost:8080/websocket")
    ws.send("Hello, World")

    while not stopped:
        result =  ws.recv()
        if result:
            ws_queue.put(result)

    ws.close()


if __name__ == "__main__":
    thread = Thread(target = do_websocket, args = [])
    thread.start()

    stdscr = curses.initscr()
    curses.noecho()
    curses.cbreak()
    stdscr.keypad(1)
    stdscr.refresh()
    stdscr.nodelay(1)

    count = 1
    while 1:
        c = stdscr.getch()
        
        if c == ord('q'):
            break

        if not ws_queue.empty():
            doc = ws_queue.get(False)
            if doc:
                stdscr.addstr(0, 0, doc)
                count = count + 1

    stopped = True

    curses.nocbreak()
    stdscr.keypad(0)
    curses.echo()
    curses.endwin()
    print("Shutting down....")


