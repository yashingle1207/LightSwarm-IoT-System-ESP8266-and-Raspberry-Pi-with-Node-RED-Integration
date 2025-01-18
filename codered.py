#!/usr/bin/env python3
"""
Raspberry Pi code for LightSwarm with LED Matrix, Logging, and Node-RED Integration.

Updated to:
- Log each master reading as "Master Data: Device ID: 0xXX, IP: ..., Reading: XXX"
- Log interval averages in the format "Interval average reading: XXX.XX, Scaled: X"

Line and bar chart logic remain as before.
"""

import spidev
import time
import socket
import struct
import threading
import datetime
import requests
from gpiozero import Button, LED

MCAST_GRP = '224.1.1.1'
MCAST_PORT = 2910
LIGHT_UPDATE_PACKET = 0
RESET_SWARM_PACKET = 1

BUTTON_PIN = 17
YELLOW_LED_PIN = 18

MATRIX_WIDTH = 8
MATRIX_HEIGHT = 8
matrix_trace = [0]*MATRIX_WIDTH

log_file = None
data_lock = threading.Lock()
device_data = {}
listener_thread = None
interval_thread = None
stop_event = threading.Event()
is_running = False

current_interval_readings = []
current_master = None
master_start_time = None
have_first_master_reading = False
current_master_ip = ""

master_cumulative_durations = {}  # device_id -> total seconds as master

nodeRedLineUrl = "http://localhost:1880/data-line"
nodeRedBarUrl = "http://localhost:1880/data-bar"

last_press_time = 0.0

button = Button(BUTTON_PIN, pull_up=True)
yellow_led = LED(YELLOW_LED_PIN)

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000

NO_OP = 0x00
DIGIT_BASE = 0x01
DECODE_MODE = 0x09
INTENSITY = 0x0A
SCAN_LIMIT = 0x0B
SHUTDOWN = 0x0C
DISPLAY_TEST = 0x0F

def max7219_write(address, data):
    spi.xfer([address, data])

def max7219_init():
    max7219_write(SHUTDOWN, 0x01)
    max7219_write(DISPLAY_TEST, 0x00)
    max7219_write(DECODE_MODE, 0x00)
    max7219_write(SCAN_LIMIT, 0x07)
    max7219_write(INTENSITY, 0x08)
    clear_matrix()

def clear_matrix():
    for i in range(1,9):
        max7219_write(i, 0x00)

def update_matrix(trace):
    row_data=[0x00]*8
    for col in range(MATRIX_WIDTH):
        value = trace[col]
        for row in range(MATRIX_HEIGHT):
            if row<value:
                row_data[7-row] |= (1<<(7-col))
    for row in range(8):
        max7219_write(DIGIT_BASE+row,row_data[row])

def create_log_file():
    global log_file
    if log_file and not log_file.closed:
        log_file.close()
    timestamp=datetime.datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
    filename=f"log_{timestamp}.txt"
    log_file=open(filename,'w')
    print(f"Log file created: {filename}")

def log(message):
    if log_file and not log_file.closed:
        timestamp=datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        log_file.write(f"{timestamp} - {message}\n")
        log_file.flush()

def send_reset_packet():
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM,socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL,1)
    packet=bytearray(14)
    packet[0]=0xF0
    packet[1]=RESET_SWARM_PACKET
    packet[13]=0x0F
    sock.sendto(packet,(MCAST_GRP,MCAST_PORT))
    sock.close()
    log("Sent RESET_SWARM_PACKET to multicast group.")
    print("DEBUG: RESET_SWARM_PACKET sent to multicast group.")

def handle_button_press():
    global is_running, matrix_trace, log_file, interval_thread, last_press_time, have_first_master_reading, master_cumulative_durations
    current_time=time.time()
    if (current_time - last_press_time)<0.5:
        return
    last_press_time=current_time

    print("DEBUG: Button pressed!")
    yellow_led.on()
    time.sleep(3)
    yellow_led.off()
    send_reset_packet()

    print("DEBUG: Resetting Node-RED charts (line and bar) due to button press.")
    # On reset, send null for line chart
    requests.post(nodeRedLineUrl, json={"value":None,"topic":"reset"})
    requests.post(nodeRedBarUrl, json={"series":[],"data":[],"labels":[]})

    have_first_master_reading = False
    master_cumulative_durations.clear()

    if not is_running:
        is_running=True
        print("DEBUG: Starting system...")
        create_log_file()
        stop_event.clear()
        global listener_thread, interval_thread
        listener_thread=threading.Thread(target=listen_to_multicast)
        listener_thread.start()
        interval_thread=threading.Thread(target=interval_processing)
        interval_thread.start()
    else:
        is_running=False
        print("DEBUG: Stopping system...")
        stop_event.set()
        if log_file:
            log_file.close()
            log_file=None
        clear_matrix()
        for i in range(MATRIX_WIDTH):
            matrix_trace[i]=0
        print("DEBUG: Resetting Node-RED charts (line and bar) due to system stop.")
        requests.post(nodeRedLineUrl, json={"value":None,"topic":"reset"})
        requests.post(nodeRedBarUrl, json={"series":[],"data":[],"labels":[]})

def listen_to_multicast():
    print("DEBUG: Listening for multicast packets...")
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM,socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    try:
        sock.bind(('',MCAST_PORT))
    except OSError as e:
        print(f"Failed to bind socket: {e}")
        return
    mreq=struct.pack("4sl",socket.inet_aton(MCAST_GRP),socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    while not stop_event.is_set():
        try:
            sock.settimeout(1.0)
            data,addr=sock.recvfrom(1024)
            if len(data)==14:
                print(f"DEBUG: Received packet from {addr}, data: {data}")
                process_packet(data, addr)
        except socket.timeout:
            continue
    sock.close()
    print("DEBUG: Listener thread stopped.")

def process_packet(data,addr):
    global current_interval_readings,current_master,master_start_time,have_first_master_reading,current_master_ip
    with data_lock:
        if data[0]==0xF0 and data[13]==0x0F:
            packet_type=data[1]
            device_id=data[2]
            reading=(data[5]<<8)|data[6]
            ip_address=addr[0]
            device_data[device_id]=ip_address

            if packet_type==LIGHT_UPDATE_PACKET:
                print(f"DEBUG: LIGHT_UPDATE_PACKET from device_id={device_id}, IP={ip_address}, reading={reading}")
                # If we have a current master and this reading is from the master, log it
                if current_master==device_id:
                    current_interval_readings.append(reading)
                    log(f"Master Data: Device ID: 0x{device_id:02X}, IP: {ip_address}, Reading: {reading}")
                    if reading>0 and not have_first_master_reading:
                        have_first_master_reading = True
                else:
                    # Master changed
                    if current_master is not None:
                        duration=time.time()-master_start_time
                        log(f"Device ID: 0x{current_master:02X} was master for {duration:.2f} seconds.")
                    current_master=device_id
                    current_master_ip=ip_address
                    master_start_time=time.time()
                    log(f"Device ID: 0x{device_id:02X} is now the master.")
                    # If first reading for the new master is positive
                    if reading>0:
                        have_first_master_reading = True
                        # Also log this reading now that we know master
                        current_interval_readings.append(reading)
                        log(f"Master Data: Device ID: 0x{device_id:02X}, IP: {ip_address}, Reading: {reading}")

            elif packet_type==RESET_SWARM_PACKET:
                print("DEBUG: Received RESET_SWARM_PACKET from network.")
                log("Received RESET_SWARM_PACKET")
        else:
            print("DEBUG: Received invalid packet")
            log("Received invalid packet")

def send_data_to_node_red(avg_reading):
    if not have_first_master_reading or current_master is None:
        return

    print(f"DEBUG: Preparing to send data to Node-RED. avg_reading={avg_reading}")

    master_ip = current_master_ip if current_master_ip else "no_master"

    # Increment cumulative time for current master each second
    if current_master is not None:
        master_cumulative_durations[current_master] = master_cumulative_durations.get(current_master,0)+1

    devices_list = list(master_cumulative_durations.keys())
    if current_master in devices_list:
        devices_list.remove(current_master)
        devices_list = [current_master]+devices_list

    series_names = []
    data_values = []
    for dev_id_ in devices_list:
        series_names.append(f"Dev0x{dev_id_:02X}")
        data_values.append([master_cumulative_durations[dev_id_]])

    if not devices_list:
        bar_payload=[{"series":[],"data":[],"labels":[]}]
    else:
        bar_payload=[{
            "series": series_names,
            "data": data_values,
            "labels": ["30s Window"]
        }]

    line_data={"value":avg_reading, "topic":master_ip}

    print(f"DEBUG: Sending line data to Node-RED: {line_data}")
    line_resp=requests.post(nodeRedLineUrl, json=line_data)
    print(f"DEBUG: Line POST response: {line_resp.status_code} {line_resp.reason}")

    print(f"DEBUG: Sending bar data to Node-RED: {bar_payload[0]}")
    bar_resp=requests.post(nodeRedBarUrl, json=bar_payload)
    print(f"DEBUG: Bar POST response: {bar_resp.status_code} {bar_resp.reason}")

def interval_processing():
    while not stop_event.is_set():
        time.sleep(1)
        with data_lock:
            if current_interval_readings:
                avg_reading=sum(current_interval_readings)/len(current_interval_readings)
                scaled_reading=int((avg_reading/1100.0)*MATRIX_HEIGHT)
                scaled_reading=max(0,min(scaled_reading,MATRIX_HEIGHT))
                matrix_trace.pop(0)
                matrix_trace.append(scaled_reading)
                update_matrix(matrix_trace)
                # Log interval average reading in the requested format:
                log(f"Interval average reading: {avg_reading}, Scaled: {scaled_reading}")
                print(f"DEBUG: Interval processed. avg_reading={avg_reading}")
                send_data_to_node_red(avg_reading)
                current_interval_readings.clear()
            else:
                # No readings this interval
                matrix_trace.pop(0)
                matrix_trace.append(0)
                update_matrix(matrix_trace)
                log("No readings received in this interval.")
                print("DEBUG: Interval processed with no readings.")

                # Send a baseline value to keep line visible
                send_data_to_node_red(100.0)

def main():
    max7219_init()
    button.when_pressed=handle_button_press
    print("Press the button to start logging and LED matrix visualization.")
    try:
        while True:
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("Interrupted by user.")
    finally:
        stop_event.set()
        if log_file:
            log_file.close()
        clear_matrix()
        spi.close()
        print("Program terminated.")

if __name__=="__main__":
    main()

