#!/usr/bin/env python3
import argparse
import asyncio
import struct 
import socket
import curses, curses.panel
from websockets import connect
import json
import sys
import logging
import coincache_pb2 as cc

async def coypu(display, host, port, offset):
    async with connect("ws://%s:%d/websocket" % (host, port), ping_interval=600, ping_timeout=10) as cws:
        if offset == -1:
            await cws.send(json.dumps({"cmd": "mark"}))
        else:
            await cws.send(json.dumps({"cmd": "mark", "offset" : 0}))
        while True:
            msg = await cws.recv()
            try:
                display.render(msg)
                # await ?
            except Exception as e:
                logging.getLogger('websockets').exception("coypu", e)
    
class Display:
    def __init__ (self, loop, snap_host, snap_port):
        self.loop = loop
        self.snap_host = snap_host
        self.snap_port = snap_port
        self.COL_PRODUCT = 0
        self.COL_BID_QTY = 1
        self.COL_BID_PX = 2
        self.COL_ASK_PX = 3
        self.COL_ASK_QTY = 4
        self.COL_SPREAD = 5
        self.COL_LAST = 6
        self.COL_UPDATES = 7
        self.COL_TOT_QTY = 8

    def __enter__(self):
        self.stdscr = curses.initscr()
        self.blotter_pad = curses.newpad(999,140)
        curses.start_color()
        curses.noecho()
        curses.cbreak()
        curses.curs_set(0)
        self.blotter_pad.keypad(1)
        self.blotter_pad.nodelay(True)

        curses.init_pair(1, curses.COLOR_RED  , curses.COLOR_BLACK)
        curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)
        curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_BLACK)
        curses.init_pair(4, curses.COLOR_RED  , curses.COLOR_WHITE)
        curses.init_pair(5, curses.COLOR_CYAN , curses.COLOR_BLACK) # header
        self.height, self.width = self.stdscr.getmaxyx()

        self.last_y = 1
        self.products = {}
        self.row_product_map = {}
        self.seq_no = 0
        self.selected_row = 1
        self.selected_col = 0
        self.show_book = False
        self.draw_header()
       
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
            self.products[product] = { 'y': self.last_y, 'last_bid_qty' : 0.0, 'last_ask_qty' : 0.0,
                                       'selected' : False, 'updates' : 0, 'tot_qty' : 0.0,
                                       'last_bid': 0.0, 'last_ask':0.0, 'vol': '-', 'last':0.0, 'prev':0.0 }
            self.products[product]['last_color'] = curses.color_pair(3)
            self.row_product_map[self.last_y] = product
            self.last_y = self.last_y + 1

        return product, self.last_y

    def compute_atts(self, sc, check, do_bold, force_clear, y):
        atts = curses.A_DIM
        batts = curses.A_BOLD
        if force_clear==False and y == self.selected_row:
            atts = curses.A_REVERSE
        elif do_bold:
            atts = batts

        return atts if sc == check else batts if do_bold else 0

    def draw_header(self):
        header_row = 0

        win = self.blotter_pad
        cols = [
            [  1, 11, "Ticker"],
            [ 15, 12, "Bid Qty"],
            [ 28, 14, "Bid Size"],
            [ 45, 14, "Ask Size"],
            [ 60, 12, "Ask Qty"],
            [ 73, 16, "Spread"],
            [ 90, 14, "Last"],
            [105,  8, "Updates"],
            [114, 14, "Volume"]
        ]

        for c in cols:
            x = "{:>" + str(c[1]) + "s}"

            win.addstr(header_row, c[0], x.format(c[2]), curses.color_pair(5) | curses.A_BOLD | curses.A_UNDERLINE)
    
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
        updates = self.products[product]["updates"]
        tot_qty = self.products[product]["tot_qty"]

        sc = self.selected_col
        do_bold = self.products[product]['selected']
        ca = lambda col : self.compute_atts(sc, col, do_bold, force_clear, y)
        
        self.blotter_pad.addstr(y, 0, '>' if do_bold else ' ', curses.A_BOLD if do_bold else 0)
        self.blotter_pad.clrtoeol()
        self.blotter_pad.addstr(y, 1, "{:>11s}".format(product), ca(self.COL_PRODUCT))
    
        f = "{:12.4f}".format(bid_qty)
        self.blotter_pad.addstr(y, 15, f, ca(self.COL_BID_QTY))

        self.blotter_pad.addstr(y, 43, 'x')
        
        if bid_px > ask_px:
            f = "{:14.7f}".format(bid_px)
            self.blotter_pad.addstr(y, 28, f, curses.color_pair(4))
            
            f = "{:14.7f}".format(ask_px)
            self.blotter_pad.addstr(y, 45, f, curses.color_pair(4))
        else:
            f = "{:14.7f}".format(bid_px)
            self.blotter_pad.addstr(y, 28, f, ca(self.COL_BID_PX))
            
            f = "{:14.7f}".format(ask_px)
            self.blotter_pad.addstr(y, 45, f, ca(self.COL_ASK_PX))
        
        f = "{:12.4f}".format(ask_qty)
        self.blotter_pad.addstr(y, 60, f, ca(self.COL_ASK_QTY))

        f = "({:14.8f})".format(ask_px-bid_px)
        self.blotter_pad.addstr(y, 73, f, ca(self.COL_SPREAD))

        f = "{:14.7f}".format(last)
        self.blotter_pad.addstr(y, 90, f, self.products[product]['last_color'] | ca(self.COL_LAST))

        f = "{:>8d}".format(updates)
        self.blotter_pad.addstr(y, 105, f, ca(self.COL_UPDATES))
        
        f = "{:14.4f}".format(tot_qty)
        self.blotter_pad.addstr(y, 114, f, ca(self.COL_TOT_QTY))
        
    def redraw_screen (self):
        self.draw_header()
        for x in self.products:
            self.draw_line(x)

    def sort_lines (self, reversed):
        row_product_map = {}
    
        new_y = 1
        for x in sorted(self.products, reverse=reversed):
            self.products[x]['y'] = new_y
            row_product_map[new_y] = x
            new_y +=1
            self.draw_line(x)

        return row_product_map

    def blotter_refresh(self):
        if self.selected_row >= self.height-1:
            self.blotter_pad.refresh(self.selected_row-self.height+1,0,0,0,self.height-1,self.width-1)
        else:
            self.blotter_pad.refresh(0,0,0,0,self.height-1,self.width-1)

    def render (self, msg):
        self.seq_no = self.seq_no + 1
        cm = cc.CoypuMessage()
        cm.ParseFromString(msg);

        if cm.type == cc.CoypuMessage.TICK:
            product, last_y = self.init_product(product=cm.tick.key, source=cm.tick.source)
            if cm.tick.source == 0:
                log = logging.getLogger('websockets')
                log.info(product)
                log.info(cm.tick.key)
                log.info(cm.tick.source)
                log.info(cm)

            
            last = self.products[product]['last']
            prev = self.products[product]['prev']
            
            self.products[product]['updates'] = self.products[product]['updates'] + 1
            self.products[product]['prev'] = last
            self.products[product]['last_bid'] = cm.tick.bid_px/100000000.0
            self.products[product]['last_ask'] = cm.tick.ask_px/100000000.0
            self.products[product]['last_bid_qty'] = cm.tick.bid_qty/100000000.0
            self.products[product]['last_ask_qty'] = cm.tick.ask_qty/100000000.0
            
            if last < prev:
                self.products[product]['last_color'] = curses.color_pair(1)
            elif last > prev:
                self.products[product]['last_color'] = curses.color_pair(2)
        elif cm.type == cc.CoypuMessage.TRADE:
            if cm.trade.source == 0:
                log = logging.getLogger('websockets')
                log.info(cm)
            product, last_y = self.init_product(product=cm.trade.key, source=cm.trade.source)
                        
            self.products[product]['vol'] = cm.trade.last_size
            self.products[product]['last'] = cm.trade.last_px
            self.products[product]['updates'] = self.products[product]['updates'] + 1
            self.products[product]['tot_qty'] = self.products[product]['tot_qty'] + cm.trade.last_size
            
        self.draw_line(product)
        self.blotter_refresh()


    async def snap_book(self, product):
        snap_req = cc.CoypuRequest()
        l = product.split('.')
        max_levels = self.stdscr.getmaxyx()[0] - 5 
        snap_req.type = cc.CoypuRequest.BOOK_SNAPSHOT_REQUEST
        snap_req.snap.key = l[0]
        snap_req.snap.source = int(l[1])
        snap_req.snap.levels = max_levels

        reader, writer = await asyncio.open_connection(self.snap_host, self.snap_port, loop=self.loop)
        log = logging.getLogger('websockets')

        writer.write(struct.pack("!I", snap_req.ByteSize()))
        writer.write(snap_req.SerializeToString())
                             
        data = await reader.read(4) # read 4 bytes
        size = struct.unpack("!I", data)

        data = await reader.read(size[0])
        cm = cc.CoypuMessage()
        cm.ParseFromString(data)
        if cm.type == cc.CoypuMessage.BOOK_SNAP:
            y = 2
            for level in cm.snap.bid:
                f = "{:12.4f}".format(level.qty/100000000.0)
                self.stdscr.addstr(y, 15, f)
                f = "{:14.7f}".format(level.px/100000000.0)
                self.stdscr.addstr(y, 28, f)
                self.stdscr.addstr(y, 43, '.')
                y = y + 1

            y = 2
            for level in cm.snap.ask:
                self.stdscr.addstr(y, 43, '.')
                f = "{:14.7f}".format(level.px/100000000.0)
                self.stdscr.addstr(y, 45, f)
                f = "{:12.4f}".format(level.qty/100000000.0)
                self.stdscr.addstr(y, 60, f)

                y = y + 1

            self.stdscr.refresh()
            
        writer.close()


    async def get_ch(self):
        while True:
            char = await self.loop.run_in_executor(None, self.blotter_pad.getch)
            try:
                if char == ord('q'):
                    loop.stop()
                elif char == ord('b'):
                    if not self.show_book:
                        self.height=1
                        self.show_book = True
                        product = self.row_product_map[self.selected_row]

                        f = self.loop.create_task(self.snap_book(product))
                    else:
                        self.height,self.width = self.stdscr.getmaxyx()
                        self.show_book = False
                        
                    self.stdscr.clear()
                    self.stdscr.refresh()
                    self.blotter_refresh()
                    
                elif char == ord('h'):
                    self.row_product_map = self.sort_lines(False)
                elif char == ord('H'):
                    self.row_product_map = self.sort_lines(True)
                elif char == ord(' '):
                    self.products[self.row_product_map[self.selected_row]]['selected'] = not self.products[self.row_product_map[self.selected_row]]['selected']
                    self.draw_line(self.row_product_map[self.selected_row])
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
                    self.selected_row = 1
                    self.draw_line(self.row_product_map[self.selected_row])                
                    self.blotter_refresh()
                elif char == curses.KEY_RIGHT:
                    if self.selected_col < 8:
                        self.selected_col += 1
                        self.draw_line(self.row_product_map[self.selected_row])
                elif char == curses.KEY_LEFT:
                    if self.selected_col > 0:
                        self.selected_col -= 1
                        self.draw_line(self.row_product_map[self.selected_row])
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
                logging.getLogger('websockets').exception("get_ch", e)

                    
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", help="increase output verbosity", action="store_true")
    parser.add_argument("-p", "--port", help="coypu webservice port", default=8080, type=int)
    parser.add_argument("-w", "--host", help="coypu host", default='localhost', type=str)
    parser.add_argument("-s", "--snap_port", help="coypu book snap port", default=8088, type=int)
    parser.add_argument("-t", "--snap_host", help="coypu book snap host", default='localhost', type=str)
    parser.add_argument("-o", "--offset", help="offset", default=-1, type=int)
    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)
    else:
        logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.INFO)
        
    fileh = logging.FileHandler("client.log")
    fileh.setFormatter(logging.Formatter('%(asctime)s %(name)-12s: %(levelname)-8s %(message)s'))
    logger = logging.getLogger('')
    logger.setLevel(logging.INFO)
    logger.addHandler(fileh)

    loop = asyncio.get_event_loop()
    
    with Display(loop, args.snap_host, args.snap_port) as display:
        task1 = loop.create_task(display.get_ch())
        task2 = loop.create_task(coypu(display, args.host, args.port, args.offset))
        loop.run_forever()

        # cleans up display for some reason
        task1.cancel()
        try:
            loop.run_until_complete(task1)
        except Exception as e:
            logging.getLogger('websockets').exception("__main", e)

