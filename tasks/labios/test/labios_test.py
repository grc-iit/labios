import ctypes
import os
import random
import string

# Load the shared library
lib = ctypes.CDLL("/home/rpawar4/grc-labios/tasks/build/labios/test/liblabios_wrapper.so")

# lib = ctypes.CDLL("./liblabios_wrapper.so")

# Function prototypes
lib.labios_create_client.restype = ctypes.c_void_p
lib.labios_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
lib.labios_read.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.labios_read.restype = ctypes.c_char_p

# Generate random string
def generate_random_data(size_bytes):
    return ''.join(random.choices(string.printable[:-6], k=size_bytes))

# Use wrapper
client = lib.labios_create_client()
key = b"first_key"
data = generate_random_data(1024).encode()

lib.labios_write(client, key, data)
print("[Python] Data written: ", data)

read_data = lib.labios_read(client, key)
print("[Python] Data read: ", read_data.decode())
