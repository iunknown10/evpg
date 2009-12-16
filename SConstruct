#!/usr/bin/python
import os, sys

env = Environment()
env.Append(CFLAGS='-I/usr/include/postgresql')
env.Append(CFLAGS="-ggdb")

env.StaticLibrary('libevpg', ['evpg.c'])
env.Program('test.c', LIBS=['evpg', 'event', 'pq'], LIBPATH='.')
