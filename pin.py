#~/usr/bin/env python
# coding=utf8

import pygame
import time
import sys
import select
import serial
import os

# Disable screen blanking; our inputs aren't coming from X events, so X would otherwise powersave/blank the monitor.
os.system("xset -dpms")
os.system("xset s noblank")
os.system("xset s noexpose")
os.system("xset s reset")

pygame.mixer.pre_init(44100,-16,2, 1024)
pygame.init()
#pygame.display.init()
maxsize = pygame.display.list_modes()[0]
screen = pygame.display.set_mode(maxsize, pygame.FULLSCREEN)
size = width, height = screen.get_rect()[2:4]
myfont = pygame.font.Font(pygame.font.match_font("default"), height/5)

pygame.mouse.set_visible(False)
print "start"
t = myfont.render("Hello", True, (255,255,255))
s = serial.Serial("/dev/ttyAMA0", 9600)
print "got", s.name
pygame.mixer.music.load("more.ogg")
pygame.mixer.music.play(-1)
screen.blit(t, t.get_rect())
if 0:
	for i in range(0,width, 10):
		pygame.draw.line(screen, (255,i%255,0), (0, height), (i, 0))
		pygame.display.flip()
	for i in range(0,height, 10):
		pygame.draw.line(screen, (255,i%255,0), (0, i), (width, 0))
		pygame.display.flip()
def linebase(w):
	color = 0
	for i in range(0,width, 10+w):
		pygame.draw.line(screen, (255,color%255,0), (0, height), (i, 0))
		color += 10
	for i in range(10 - (width - i),height, 10+w):
		pygame.draw.line(screen, (255,color%255,0), (0, height), (width, i))
		color += 10
s.write("r")
cnt = 0
dir = 1
w = 5
wdir = 1
r = t.get_rect()
val = 0
score = 0
snd = pygame.mixer.Sound("punch.wav")
while True:
	screen.fill((0,0,0))
	linebase(w)
	w += wdir
	if w < 5:
		wdir = 1
	elif w > 23:
		wdir = -1
	r[0] = cnt * 5
	cnt+=dir
	if cnt < 0:
		dir = 1
	elif cnt > 100:
		dir = -1
	screen.blit(t, r)
	t2 = myfont.render("Score: %i" % (score), True, (255,255,255))
	r2 = t2.get_rect()
	r2[0] = 100
	r2[1] = 400
	screen.blit(t2, r2)
	t3 = myfont.render("Pos: %i" % val, True, (128,255,255))
	r3 = t3.get_rect()
	r3[0] = 100
	r3[1] = 600
	screen.blit(t3, r3)
	pygame.display.flip()
	ready = select.select([sys.stdin, s], [], [], 0)[0]
	if sys.stdin in ready:
		line = sys.stdin.readline()
		if "quit" in line:
			sys.exit(0)
		elif "silent" in line:
			pygame.mixer.music.pause()
		elif "reset" in line:
			score = 0
			s.write("r")
			s.flushOutput()
		elif not line:
			sys.exit(0)
		else:
			print "Unknown command",line
	if s in ready:
		line = s.readline().strip()
		if line.startswith("U"):
			val = int(line[2:])
			print line, val
		elif line.startswith("P"):
			score += int(line[2:])
			print line, score
			snd.play()
