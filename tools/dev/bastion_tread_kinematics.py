"""Deterministic world-space pose sampler for Bastion's visible caterpillar treads.

The authored low-poly belt path is parameterized by arc length. +world Z is
Bastion's forward direction; the bottom belt runs in -local Z to match
traction without sliding. No terrain-relative or camera-relative scrolling.
"""
from __future__ import annotations
import bisect
import math

LINK_COUNT=48
TRACK_X=23.0
WHEEL_X=26.1
WHEEL_Y=2.85
WHEEL_RADIUS=1.88
WHEEL_Z=(-21.0,-14.0,-7.0,0.0,7.0,14.0,21.0)

def path_points():
    r=2.4
    cy=2.85
    points=[]
    for i in range(27):
        points.append((cy-r,-22.0+44.0*i/26))
    for i in range(1,15):
        t=-math.pi/2+math.pi*i/14
        points.append((cy+r*math.sin(t),22.0+r*math.cos(t)))
    for i in range(1,27):
        points.append((cy+r,22.0-44.0*i/26))
    for i in range(1,15):
        t=math.pi/2+math.pi*i/14
        points.append((cy+r*math.sin(t),-22.0+r*math.cos(t)))
    return tuple(points)

POINTS=path_points()
LENGTHS=[0.0]
for i in range(len(POINTS)):
    a=POINTS[i]
    b=POINTS[(i+1)%len(POINTS)]
    LENGTHS.append(LENGTHS[-1]+math.hypot(b[0]-a[0],b[1]-a[1]))
BELT_LENGTH=LENGTHS[-1]

def belt_pose(link_index:int, travel_m:float=0.0):
    if not (0<=link_index<LINK_COUNT and math.isfinite(travel_m)):
        raise ValueError("Invalid tread index/displacement")
    # At road contact, relative tread velocity is opposite to the city.
    d=(link_index*BELT_LENGTH/LINK_COUNT-travel_m)%BELT_LENGTH
    seg=min(len(POINTS)-1,bisect.bisect_right(LENGTHS,d)-1)
    a,b=POINTS[seg],POINTS[(seg+1)%len(POINTS)]
    fraction=(d-LENGTHS[seg])/(LENGTHS[seg+1]-LENGTHS[seg])
    y=a[0]+(b[0]-a[0])*fraction
    z=a[1]+(b[1]-a[1])*fraction
    angle=-math.degrees(math.atan2(b[0]-a[0],b[1]-a[1]))
    return y,z,angle
