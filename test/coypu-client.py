#!/usr/bin/env python3
import argparse
import asyncio
import curses
from websockets import connect
import json
import sys
import logging

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.FileHandler("client.log"))


async def coypu(display, host, port):
    async with connect("ws://%s:%d/websocket" % (host, port), ping_interval=600, ping_timeout=10) as cws:
        await cws.send(json.dumps({"cmd": "mark"}))
        while True:
            msg = await cws.recv()
            try:
                display.render(msg)
                # await ?
            except Exception as e:
                print(e)
    
class Display:
    def __init__ (self, loop):
        self.loop = loop

    def __enter__(self):
        self.stdscr = curses.initscr()
        self.blotter_pad = curses.newpad(999,120)
        curses.start_color()
        curses.noecho()
        curses.cbreak()
        curses.curs_set(0)
        self.blotter_pad.keypad(1)
        self.blotter_pad.nodelay(1)

        curses.init_pair(1, curses.COLOR_RED, curses.COLOR_BLACK)
        curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)
        curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_BLACK)
        curses.init_pair(4, curses.COLOR_RED, curses.COLOR_WHITE)
        self.height, self.width = self.stdscr.getmaxyx()

        self.last_y = 0
        self.products = {}
        self.row_product_map = {}
        self.seq_no = 0
        self.selected_row = 0
        
        return self

    def __exit__(self, *args):
        curses.nocbreak()
        self.blotter_pad.keypad(0)
        curses.echo()
        curses.endwin()

    def init_product (self, product, source):
        product = "%s.%s" % (product, source)
        product = product.replace("-", "/", 1)

        if product not in self.products:
            self.products[product] = { 'y': self.last_y, 'last_bid': 0.0, 'last_ask':0.0, 'vol': '-', 'last':0.0, 'prev':0.0 }
            self.products[product]['last_color'] = curses.color_pair(3)
            self.row_product_map[self.last_y] = product
            self.last_y = self.last_y + 1

        return product, self.last_y

    def draw_line (self, product, force_clear=False):
        y = self.products[product]['y']
        height,width = self.blotter_pad.getmaxyx()
        if y >= height-1:
            return
        bid_qty = self.products[product]["last_bid_qty"]
        ask_qty = self.products[product]["last_ask_qty"]
        bid_px = self.products[product]["last_bid"]
        ask_px = self.products[product]["last_ask"]
        last = self.products[product]["prev"]

        atts = 0
        if force_clear==False and y == self.selected_row:
            atts = curses.color_pair(4) | curses.A_REVERSE
    
        self.blotter_pad.addstr(y, 0, product, atts)
        self.blotter_pad.clrtoeol()
    
        f = "{:12.4f}".format(bid_qty)
        self.blotter_pad.addstr(y, 15, f)
    
        if bid_px > ask_px:
            f = "{:14.7f}".format(bid_px)
            self.blotter_pad.addstr(y, 28, f, curses.color_pair(4))
            
            f = "x {:14.7f}".format(ask_px)
            self.blotter_pad.addstr(y, 43, f, curses.color_pair(4))
        else: 
            f = "{:14.7f}".format(bid_px)
            self.blotter_pad.addstr(y, 28, f)
            
            f = "x {:14.7f}".format(ask_px)
            self.blotter_pad.addstr(y, 43, f)
        
        
        f = "{:12.4f}".format(ask_qty)
        self.blotter_pad.addstr(y, 60, f)
        
        f = "{:14.8f}".format(last)
        self.blotter_pad.addstr(y, 100, f, self.products[product]['last_color'])

        f = "({:14.8f})".format(ask_px-bid_px)
        self.blotter_pad.addstr(y, 80, f)

    def redraw_screen (self):
        for x in self.products:
            self.draw_line(x)

    def sort_lines (self, reversed):
        row_product_map = {}
    
        new_y = 0
        for x in sorted(self.products, reverse=reversed):
            self.products[x]['y'] = new_y
            row_product_map[new_y] = x
            new_y +=1
            self.draw_line(x)

        return row_product_map

    def blotter_refresh(self):
        if self.selected_row >= self.height-1:
            self.blotter_pad.refresh(self.selected_row-self.height,0,0,0,self.height-1,120)
        else:
            self.blotter_pad.refresh(0,0,0,0,self.height-1,120)
        
    def render (self, doc):
        self.seq_no = self.seq_no + 1
        try:
            self.blotter_pad.addstr(self.height-1,0, "{:d}".format(self.seq_no))
        except Exception as e:
            pass
        

        l = doc.split(' ')
        if l[0] == "Trade":
            product, last_y = self.init_product(product=l[1], source=l[6])
                        
            self.products[product]['vol'] = l[2]
            self.products[product]['last'] = float(l[3])
            self.draw_line(product)
            self.blotter_refresh()
            
        elif l[0] == "Tick":
            product, last_y = self.init_product(product=l[1], source=l[6])
            
            last = self.products[product]['last']
            prev = self.products[product]['prev']

            self.products[product]['prev'] = last
            self.products[product]['last_bid'] = float(int(l[3]))/100000000.0
            self.products[product]['last_ask'] = float(int(l[4]))/100000000.0
            self.products[product]['last_bid_qty'] = float(int(l[2]))/100000000.0
            self.products[product]['last_ask_qty'] = float(int(l[5]))/100000000.0
            
            if last < prev:
                self.products[product]['last_color'] = curses.color_pair(1)
            elif last > prev:
                self.products[product]['last_color'] = curses.color_pair(2)

            self.draw_line(product)
            self.blotter_refresh()


    async def get_ch(self):
        while True:
            char = await self.loop.run_in_executor(None, self.blotter_pad.getch)
            try:
                if char == ord('q'):
                    loop.stop()
                elif char == ord('h'):
                    self.row_product_map = self.sort_lines(False)
                elif char == ord('H'):
                    self.row_product_map = self.sort_lines(True)
                elif char == curses.KEY_DOWN:
                    if self.selected_row+1 in self.row_product_map:
                        self.draw_line(self.row_product_map[self.selected_row], True)
                        self.selected_row += 1
                        self.draw_line(self.row_product_map[self.selected_row])

                        self.blotter_refresh()
                elif char == curses.KEY_UP:
                    if self.selected_row > 0:
                        self.draw_line(self.row_product_map[self.selected_row], True)
                        self.selected_row -= 1
                        self.draw_line(self.row_product_map[self.selected_row])
                        self.blotter_refresh()
                elif char == curses.KEY_PPAGE:
                    self.draw_line(self.row_product_map[self.selected_row], True)
                    self.selected_row = 0
                    self.draw_line(self.row_product_map[self.selected_row])                
                    self.blotter_refresh()
                elif char == curses.KEY_NPAGE:
                    self.draw_line(self.row_product_map[self.selected_row], True)
                    self.selected_row = self.last_y-1
                    if self.selected_row in self.row_product_map:
                        self.draw_line(self.row_product_map[self.selected_row])
                    self.blotter_refresh()
                elif char == curses.KEY_RESIZE:
                    oldheight = self.height-1
                    self.height,self.width = self.stdscr.getmaxyx()

                    self.blotter_pad.clear()
                    self.redraw_screen()
                    self.blotter_refresh()
            except Exception as e:
                print(e)

                    
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", help="increase output verbosity", action="store_true")
    parser.add_argument("-p", "--port", help="coypu webservice port", default=8080, type=int)
    parser.add_argument("-w", "--host", help="coypu host", default='localhost', type=str)
    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)
    else:
        logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.INFO)
        
    loop = asyncio.get_event_loop()

    with Display(loop) as display:
        task1 = loop.create_task(display.get_ch())
        task2 = loop.create_task(coypu(display, args.host, args.port))
        loop.run_forever()

        # cleans up display for some reason
        task1.cancel()
        try:
            loop.run_until_complete(task1)
        except:
            pass
