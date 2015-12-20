#~/usr/bin/env python
# coding=utf8

import pygame
import time

pygame.init()
#pygame.display.init()
maxsize = pygame.display.list_modes()[0]
screen = pygame.display.set_mode(maxsize, pygame.FULLSCREEN)
size = width, height = screen.get_rect()[2:4]
myfont = pygame.font.Font(pygame.font.match_font("default"), height/5)

pygame.mouse.set_visible(False)
print "start"
t = myfont.render("Hello", True, (255,255,255))
screen.blit(t, t.get_rect())
if 0:
	for i in range(0,width, 10):
		pygame.draw.line(screen, (255,i%255,0), (0, height), (i, 0))
		pygame.display.flip()
	for i in range(0,height, 10):
		pygame.draw.line(screen, (255,i%255,0), (0, i), (width, 0))
		pygame.display.flip()
if 1:
	for i in range(0,width, 10):
		pygame.draw.line(screen, (255,i%255,0), (0, height), (i, 0))
		pygame.display.flip()
	for i in range(0,height, 10):
		pygame.draw.line(screen, (255,i%255,0), (0, height), (width, i))
		pygame.display.flip()
print "sleep time. Press ctrl+d to exit"
import sys
sys.stdin.read()
