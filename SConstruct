#!/usr/bin/python
import os, sys

env = Environment()
env.Append(CFLAGS='-I/usr/include/postgresql')

env.StaticLibrary('libevpg', ['evpg.c'])
