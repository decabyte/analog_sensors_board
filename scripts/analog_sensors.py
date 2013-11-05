# analog_sensors.py

# author: 	Valerio De Carolis <valerio.decarolis@gmail.com>
# date:		2013-10-30
# license:	MIT

import sys
import os
import time
import signal
import serial

from serial import Serial, SerialException


# default serial configuration
DEFAULT_CONF = {
	'port': '/dev/ttyACM3',
	'baudrate': 57600,
	'bytesize': serial.EIGHTBITS,
	'parity': serial.PARITY_NONE,
	'stopbits': serial.STOPBITS_ONE,
	'timeout': 5
}


class AnalogSensorsClient:

	def __init__(self):
		# battery
		self.batt0 = 0
		self.batt1 = 0
		self.batt2 = 0
		self.batt3 = 0
		self.raw_batt0 = 0
		self.raw_batt1 = 0
		self.raw_batt2 = 0
		self.raw_batt3 = 0

		# temperature
		self.temp0 = 0
		self.temp1 = 0
		self.temp2 = 0
		self.temp3 = 0
		self.raw_temp0 = 0
		self.raw_temp1 = 0
		self.raw_temp2 = 0
		self.raw_temp3 = 0

		# pressure
		self.bmp_temperature = 0
		self.bmp_pressure = 0
		self.bmp_ut = 0
		self.bmp_up = 0
		self.bmp_dirty = 0

		# humidity
		self.humidity = 0
		self.raw_humidity = 0

		# timestamps
		self.timestamp = 0

		# protocol parsers
		self.GRAMMAR = {
			'BMPCAL':  self.parse_bmpcal,
			'BAT': self.parse_battery,
			'TEMP': self.parse_temperature,
			'HIH': self.parse_humidity,
			'BMP': self.parse_pressure,
			'TIME': self.parse_timestamp
		}


	def print_status(self):
		print('BATTERY VOLTAGES: {}V {}V {}V {}V'.format(
			self.batt0, self.batt1, self.batt2, self.batt3))
		print('VEHICLE TEMPERATURES: {}C {}C {}C {}C'.format(
			self.temp0, self.temp1, self.temp2, self.temp3))
		print('VEHICLE ENVIRONMENT: {}C {}Pa {}RH%\n'.format(
			self.bmp_temperature, self.bmp_pressure, self.humidity))

 
	def parse_message(self, msg):
		'''
			An example serial message:
			$TEMP,122.10,123.10,123.10,127.85,488,492,492,511
		'''	

		# parse serial message
		items = msg.split(',')

		# look for identifier
		if items[0][0] is not '$':
			return

		# extract message type
		msg_type = items[0][1:]

		# check message type
		try:
			parser = self.GRAMMAR[msg_type]
			parser(items)
		except KeyError as ke:
			print('[WARN]: message not recognized! bad format?')


	def parse_battery(self, field):
		# battery voltages
		self.batt0 = float(field[1])
		self.batt1 = float(field[2])
		self.batt2 = float(field[3])
		self.batt3 = float(field[4])

		# raw analog readings
		self.raw_batt0 = int(field[5])
		self.raw_batt1 = int(field[6])
		self.raw_batt2 = int(field[7])
		self.raw_batt3 = int(field[8])

	def parse_bmpcal(self, field):
		pass

	def parse_temperature(self, field):
		# temperature
		self.temp0 = float(field[1])
		self.temp1 = float(field[2])
		self.temp2 = float(field[3])
		self.temp3 = float(field[4])

		# raw analog readings
		self.raw_temp0 = int(field[5])
		self.raw_temp1 = int(field[6])
		self.raw_temp2 = int(field[7])
		self.raw_temp3 = int(field[8])

	def parse_humidity(self, field):
		self.humidity = float(field[1])
		self.raw_humidity = int(field[2])

	def parse_pressure(self, field):
		self.bmp_temperature = float(field[1])
		self.bmp_pressure = float(field[2])
		self.bmp_ut = int(field[3])
		self.bmp_up = int(field[4])
		self.bmp_dirty = int(field[5])

	def parse_timestamp(self, field):
		self.timestamp = int(field[1])


def main():
	# control flags
	running = True
	connected = False
	sconn = None

	# signal handler
	def handler(signum, frame):
		running = False

	signal.signal(signal.SIGINT, handler)
	signal.signal(signal.SIGTERM, handler)

	# analog client
	client = AnalogSensorsClient()

	# connection main loop
	while running:
		try:
			sconn = Serial(**DEFAULT_CONF)
		except ValueError as ve:
			print('[FATAL]: bad port configuration!')
			sys.exit(-1)
		except SerialException as se:
			connected = False
			print('[ERROR]: device not found, waiting for device ...')

			# wait a little before trying to reconnect
			time.sleep(5)
			continue
		else:
			connected = True

		# data processing loop
		while connected:
			try:
				line = sconn.readline()
			except SerialException as se:
				connected = False
				print('[ERROR]: connection lost!')
				break

			if len(line) != 0:
				msg = line.strip()				# remove any return carriage
				client.parse_message(msg)		# digest the message

			# display status
			client.print_status()

		# release the serial connection
		if sconn.isOpen():
			sconn.close()

	# close the connection if hang
	if sconn is not None and sconn.isOpen():
		sconn.close()

	sys.exit(0)


if __name__ == '__main__':
	main()
