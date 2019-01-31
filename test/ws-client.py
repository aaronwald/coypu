#!/usr/bin/env python3

from queue import Queue
import websocket
from threading import Thread

import curses
import sys
import json

stopped = False
ws_queue = Queue()

def on_message(ws, message):
    ws_queue.put(message)

def on_error(ws, error):
    print(error)

def on_close(ws):
    print("### closed ###")

def on_open(ws):
    doc = json.dumps({"cmd": "mark"})
    ws.send(doc)

def do_websocket(ws):
    ws.run_forever()

def draw_line (stdscr, products, product, selected_row):
    y = products[product]['y']
    
    bid_qty = products[product]["last_bid_qty"]
    ask_qty = products[product]["last_ask_qty"]
    bid_px = products[product]["last_bid"]
    ask_px = products[product]["last_ask"]
    last = products[product]["prev"]

    atts = 0
    if y == selected_row:
        atts = curses.color_pair(4) | curses.A_REVERSE
    
    stdscr.addstr(y, 0, product, atts)
    stdscr.clrtoeol()
    
    f = "{:12.4f}".format(bid_qty)
    stdscr.addstr(y, 15, f)
    
    if bid_px > ask_px:
        f = "{:14.7f}".format(bid_px)
        stdscr.addstr(y, 28, f, curses.color_pair(4))
        
        f = "x {:14.7f}".format(ask_px)
        stdscr.addstr(y, 43, f, curses.color_pair(4))
    else: 
        f = "{:14.7f}".format(bid_px)
        stdscr.addstr(y, 28, f)
        
        f = "x {:14.7f}".format(ask_px)
        stdscr.addstr(y, 43, f)
        
        
    f = "{:12.4f}".format(ask_qty)
    stdscr.addstr(y, 60, f)
    
    f = "{:14.8f}".format(last)
    stdscr.addstr(y, 80, f, products[product]['last_color'])

def sort_lines (stdscr, products, reversed, selected_row):
    row_product_map = {}
    
    new_y = 0
    for x in sorted(products, reverse=reversed):
        products[x]['y'] = new_y
        row_product_map[new_y] = x
        new_y +=1
        draw_line(stdscr, products, x, selected_row)

    return row_product_map

def make_product (pair, source, last_y, products, row_product_map):
    product = "%s.%s" % (pair, source)
    product = product.replace("-", "/", 1)

    if product not in products:
        products[product] = { 'y': last_y, 'last_bid': 0.0, 'last_ask':0.0,
                              'vol': '-', 'last':0.0, 'prev':0.0, 'last_color': curses.color_pair(3)}
        
        row_product_map[last_y] = product
        last_y = last_y + 1

    return product,last_y

if __name__ == "__main__":
    websocket.enableTrace(False)
    
    ws = websocket.WebSocketApp("ws://localhost:8080/websocket",
                                on_message = on_message,
                                on_error = on_error,
                                on_close = on_close,
                                on_open = on_open)

    thread = Thread(target=do_websocket, args=[ws])
    thread.start()
 
    stdscr = curses.initscr()
    curses.start_color()
    curses.noecho()
    curses.cbreak()
    curses.curs_set(0)
    stdscr.keypad(1)
    stdscr.refresh()
    stdscr.nodelay(1)

    curses.init_pair(1, curses.COLOR_RED, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_BLACK)
    curses.init_pair(4, curses.COLOR_RED, curses.COLOR_WHITE)
    height,width = stdscr.getmaxyx()

    row_product_map = {}
    last_y = 0
    products = {}
    seq_no = 0
    select_row = 0
    
    while 1:
        c = stdscr.getch()
        
        if c == ord('q'):
            break
        elif c == ord('h'):
            row_product_map = sort_lines(stdscr, products, False, select_row)
        elif c == ord('H'):
            row_product_map = sort_lines(stdscr, products, True, select_row)
        elif c == curses.KEY_DOWN:
            if select_row+1 in row_product_map:
                draw_line(stdscr, products, row_product_map[select_row], -1)
                select_row += 1
                draw_line(stdscr, products, row_product_map[select_row], select_row)
        elif c == curses.KEY_UP:
            if select_row > 0:
                draw_line(stdscr, products, row_product_map[select_row], -1)
                select_row -= 1
                draw_line(stdscr, products, row_product_map[select_row], select_row)
        elif c == curses.KEY_PPAGE:
            draw_line(stdscr, products, row_product_map[select_row], -1)
            select_row = 0
            draw_line(stdscr, products, row_product_map[select_row], select_row)
        elif c == curses.KEY_NPAGE:
            draw_line(stdscr, products, row_product_map[select_row], -1)
            select_row = last_y-1
            if select_row in row_product_map:
                draw_line(stdscr, products, row_product_map[select_row], select_row)
        elif c == curses.KEY_RESIZE:
            oldheight = height-1
            height,width = stdscr.getmaxyx()
            if oldheight < height:
                stdscr.addstr(oldheight,0,"")
                stdscr.clrtoeol()
            
        if not ws_queue.empty():
            doc = ws_queue.get(False)
            seq_no = seq_no + 1

            stdscr.addstr(height-1,0, "{:d}".format(seq_no))

            if doc:
                try:
                    l = doc.split(' ')
                    if l[0] == "Trade":
                        product, last_y = make_product(pair=l[1], source=l[6], last_y=last_y, products=products, row_product_map=row_product_map)
                        
                        products[product]['vol'] = l[2]
                        products[product]['last'] = float(l[3])

                    if l[0] == "Tick":
                        product, last_y = make_product(pair=l[1], source=l[6], last_y=last_y, products=products, row_product_map=row_product_map)
                        
                        last = products[product]['last']
                        prev = products[product]['prev']

                        products[product]['prev'] = last
                        products[product]['last_bid'] = float(int(l[3]))/100000000.0
                        products[product]['last_ask'] = float(int(l[4]))/100000000.0
                        products[product]['last_bid_qty'] = float(int(l[2]))/100000000.0
                        products[product]['last_ask_qty'] = float(int(l[5]))/100000000.0

                        if last < prev:
                            products[product]['last_color'] = curses.color_pair(1)
                        elif last > prev:
                            products[product]['last_color'] = curses.color_pair(2)

                        draw_line(stdscr, products, product, select_row)
                        
                except Exception as e:
                    print >> sys.stderr, e
                    stdscr.addstr(20, 0, "exception" + str(e))

    stopped = True


    curses.nocbreak()
    stdscr.keypad(0)
    curses.echo()
    curses.endwin()
    print("Shutting down....")
    ws.keep_running = False

