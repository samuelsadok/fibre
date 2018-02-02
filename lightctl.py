#!/usr/bin/env python3
"""
Set the color on a fibre-enabled light controller.
"""
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)) + "/fibre/python")
import fibre
  

# Parse arguments
parser = argparse.ArgumentParser(description='Set the color on a fibre-enabled light controller.')
parser.add_argument("-v", "--verbose", action="store_true",
                    help="print debug information")
parser.add_argument("--host", metavar="HOSTNAME", action="store",
                    help="Specifies the host or IP address of the light controller.")
parser.add_argument("color", metavar="WWRRGGBB", type=str, nargs=1,
                    help="Specifies the color code.")
parser.add_argument("-t", "--time", metavar="SECONDS", type=float,
                    help="Fade duration. Defaults to 0.")
parser.add_argument("-l", "--limit-brightness", action="store_true",
                    help="don't increase brightness")
parser.set_defaults(host="192.168.178.36", time=0)
args = parser.parse_args()

try:
  color = int(args.color[0], 16)
except ValueError:
  parser.print_usage(file=sys.stderr)
  sys.stderr.write("error: expected hexadecimal color code\n")
  sys.exit(1)

# Connect to device
if (args.verbose):
  printer = print
else:
  printer = lambda x: None
lightcontroller = fibre.open_tcp(args.host + ':9910', printer=printer, device_stdout=print)

# Set color
lightcontroller.set_color(float((color >> 24) & 0xff) / 255,
                          float((color >> 16) & 0xff) / 255,
                          float((color >> 8) & 0xff) / 255,
                          float((color >> 0) & 0xff) / 255,
                          args.time,
                          1 if args.limit_brightness else 0)
