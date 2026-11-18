#!/usr/bin/env python3
"""Author a cohesive, project-owned native YDR city silhouette for The Rust Sea.

A static visual art pass only; gameplay/controller/physics colliders unchanged.
Outputs original geometry, avoiding third-party movie/game art. Models are native
NEF8 YDR with YMAT material sections and texture-dictionary references.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.dev.convert_rustsea_native import ASSETS, DEST, native_ydr, verify

MATERIAL = {
    "bastion_outer_hull": "hull_dark",
    "bastion_armor_belt": "hull_armor",
    "bastion_topdeck": "deck_worn",
    "bastion_walkway": "hull_dark",
    "bastion_tread_belts": "tread_dark",
    "bastion_tread_shoes": "hull_armor",
    "bastion_wheels": "hull_dark",
    "bastion_wheel_hubs": "hull_armor",
    "bastion_tread_link": "hull_armor",
    "bastion_wheel_spokes": "hull_armor",
    "bastion_dust_puff": "dust_translucent",
    "bastion_superstructure": "city_cladding",
    "bastion_roofwork": "hull_dark",
    "bastion_steel_ribs": "hull_armor",
    "bastion_amber_marks": "warning_amber",
    "bastion_windows": "glass_dark",
    "bastion_pipework": "hull_armor",
    "rustsea_boulder_a": "ground_gravel",
    "rustsea_boulder_b": "ground_gravel",
    "rustsea_boulder_c": "ground_dry",
}

class Mesh:
    def __init__(self):
        self.vertices=[]
        self.indices=[]
    def face(self,vertices, texel=0.22, two_sided=False):
        if len(vertices)<3:
            raise ValueError("Face requires >=3 vertices")
        a,b,c=vertices[:3]
        ab=tuple(b[i]-a[i] for i in range(3))
        ac=tuple(c[i]-a[i] for i in range(3))
        n=(ab[1]*ac[2]-ab[2]*ac[1],
           ab[2]*ac[0]-ab[0]*ac[2],
           ab[0]*ac[1]-ab[1]*ac[0])
        norm=math.sqrt(sum(v*v for v in n))
        if norm<1e-7:return
        normal=tuple(v/norm for v in n)
        span0=math.sqrt(sum(v*v for v in ab))
        if span0<1e-7:return
        basis=tuple(v/span0 for v in ab)
        basis_v=(normal[1]*basis[2]-normal[2]*basis[1],
                 normal[2]*basis[0]-normal[0]*basis[2],
                 normal[0]*basis[1]-normal[1]*basis[0])
        first=len(self.vertices)
        for p in vertices:
            d=tuple(p[i]-a[i] for i in range(3))
            u=sum(d[i]*basis[i] for i in range(3))*texel
            v=sum(d[i]*basis_v[i] for i in range(3))*texel
            self.vertices.append(tuple(p)+normal+(u,v))
        for i in range(1,len(vertices)-1):
            self.indices.extend((first,first+i,first+i+1))
            if two_sided:self.indices.extend((first+i+1,first+i,first))
    def quad(self,a,b,c,d,texel=.22):
        self.face((a,b,c,d),texel)
    def box(self,x0,x1,y0,y1,z0,z1,texel=.22):
        if not (x1>x0 and y1>y0 and z1>z0):raise ValueError("Invalid box dimensions")
        self.quad((x0,y1,z0),(x0,y1,z1),(x1,y1,z1),(x1,y1,z0),texel)
        self.quad((x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1),texel)
        self.quad((x0,y0,z0),(x0,y1,z0),(x1,y1,z0),(x1,y0,z0),texel)
        self.quad((x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1),texel)
        self.quad((x0,y0,z0),(x0,y0,z1),(x0,y1,z1),(x0,y1,z0),texel)
        self.quad((x1,y0,z0),(x1,y1,z0),(x1,y1,z1),(x1,y0,z1),texel)
    def pipe(self,a,b,r,segments=10):
        # Solid circular-section pipe, arbitrary 3D endpoints.
        dx,dy,dz=(b[i]-a[i] for i in range(3))
        n=math.sqrt(dx*dx+dy*dy+dz*dz)
        if n<.01: return
        axis=(dx/n,dy/n,dz/n)
        e=(0.,1.,0.) if abs(axis[1])<.8 else (1.,0.,0.)
        ux=axis[1]*e[2]-axis[2]*e[1]
        uy=axis[2]*e[0]-axis[0]*e[2]
        uz=axis[0]*e[1]-axis[1]*e[0]
        un=math.sqrt(ux*ux+uy*uy+uz*uz)
        u=(ux/un,uy/un,uz/un)
        v=(axis[1]*u[2]-axis[2]*u[1],
           axis[2]*u[0]-axis[0]*u[2],
           axis[0]*u[1]-axis[1]*u[0])
        ring0=[];ring1=[]
        for i in range(segments):
            angle=i*2*math.pi/segments
            radial=tuple(r*(u[k]*math.cos(angle)+v[k]*math.sin(angle)) for k in range(3))
            ring0.append(tuple(a[k]+radial[k] for k in range(3)))
            ring1.append(tuple(b[k]+radial[k] for k in range(3)))
        for i in range(segments):
            j=(i+1)%segments
            self.quad(ring0[i],ring0[j],ring1[j],ring1[i],.35)
        self.face(list(reversed(ring0)),.22)
        self.face(ring1,.22)

def build_hull():
    m=Mesh()
    # Long, faceted traction-city hull: taper fore and aft.
    # Surface of the playable deck remains y=5.7; hull stops below that.
    sections=[
        (-32.0, -2.0, 2.0, 1.9, 3.6),
        (-29.0,-10.0,10.0, 0.85,4.8),
        (-25.5,-18.2,18.2,0.6,5.25),
        (-18.,-19.,19.,0.65,5.3),
        (18.,-19.,19.,0.65,5.3),
        (25.5,-18.,18.,0.65,5.1),
        (29.5,-10.,10.,1.1,4.1),
        (31.0,-3.,3.,1.7,3.7),
    ]
    for a,b in zip(sections,sections[1:]):
        za,xa0,xa1,ya0,ya1=a
        zb,xb0,xb1,yb0,yb1=b
        m.quad((xa0,ya0,za),(xb0,yb0,zb),(xb0,yb1,zb),(xa0,ya1,za))
        m.quad((xb1,yb0,zb),(xa1,ya0,za),(xa1,ya1,za),(xb1,yb1,zb))
        m.quad((xa0,ya1,za),(xb0,yb1,zb),(xb1,yb1,zb),(xa1,ya1,za))
        m.quad((xa1,ya0,za),(xb1,yb0,zb),(xb0,yb0,zb),(xa0,ya0,za))
    m.quad((sections[0][1],sections[0][3],sections[0][0]),
           (sections[0][1],sections[0][4],sections[0][0]),
           (sections[0][2],sections[0][4],sections[0][0]),
           (sections[0][2],sections[0][3],sections[0][0]))
    return m

def build_armor():
    m=Mesh()
    for side in (-1.,1.):
        for z in range(-24,25,6):
            # Sloped overlapping steel sheets (not flat pasted containers).
            cx=side
            p0=(cx*19.8,1.0,z-3.15)
            p1=(cx*19.8,1.0,z+3.15)
            p2=(cx*18.1,5.45,z+3.0)
            p3=(cx*18.1,5.45,z-3.0)
            if side<0:m.quad(p0,p3,p2,p1)
            else:m.quad(p1,p2,p3,p0)
            # Wide welded seam / thick plate edge.
            m.box(side*19.8-0.18,side*19.8+0.18,
                  0.9,1.25,z-3.15,z+3.15)
        for z in (-23.5,-17.5,-11.5,-5.5,0.5,6.5,12.5,18.5,24.5):
            m.box(side*19.3-.24,side*19.3+.24,2.0,5.45,z-.18,z+.18)
    # A central massive plough blade at the bow (front = -Z).
    m.quad((-17.,3.9,-26.),(-8.,2.,-31.5),(8.,2.,-31.5),(17.,3.9,-26.))
    m.quad((-8.,2.,-31.5),(-2.3,1.0,-34.),(2.3,1.0,-34.),(8.,2.,-31.5))
    return m

def build_deck():
    m=Mesh()
    # One uninterrupted upper deck. Old 6m primitive tiles become invisible
    # collision-only supports. Less repetition, no artificial seams.
    m.quad((-18.,5.76,-25.7),(-18.,5.76,25.7),
           (18.,5.76,25.7),(18.,5.76,-25.7),.20)
    for side in (-1,1):
        x=side*17.5
        m.box(x-.55,x+.55,5.58,5.91,-26.,26.)
    return m

def build_walkway():
    m=Mesh()
    # Narrow continuous raised center path: player -12 -> helm +8 stays clear.
    m.box(-2.4,2.4,5.77,5.89,-23.,12.4,.40)
    # Service lanes between industrial blocks.
    for side in (-1,1):
        x=side*7.1
        m.box(x-1.,x+1.,5.78,5.9,-21.0,20.0,.46)
        for z in (-20,-14,-8,-2,4,10,16):
            m.box(x-1.5,x+1.5,5.91,5.97,z-.09,z+.09,.4)
    return m

def loop_points():
    r=2.4; center_y=2.85
    out=[]
    for i in range(27):
        out.append((center_y-r,-22+44*i/26))
    for i in range(1,15):
        theta=-math.pi/2+math.pi*i/14
        out.append((center_y+r*math.sin(theta),22+r*math.cos(theta)))
    for i in range(1,27):
        out.append((center_y+r,22-44*i/26))
    for i in range(1,15):
        theta=math.pi/2+math.pi*i/14
        out.append((center_y+r*math.sin(theta),-22+r*math.cos(theta)))
    return out

def tread_segment(m,x0,x1,pa,pb):
    ay,az=pa;by,bz=pb
    dy,dz=by-ay,bz-az
    length=math.hypot(dy,dz)
    if length<.01:return
    ty,tz=dy/length,dz/length
    ny,nz=-tz,ty
    cy,cz=(ay+by)*.5,(az+bz)*.5
    corners=[]
    for x in (x0,x1):
        for thick in (-.12,.20):
            for along in (-.43*length,.43*length):
                corners.append((x,cy+ny*thick+ty*along,cz+nz*thick+tz*along))
    # x0/1, thickness- / thickness+, along- / along+.
    a,b,c,d,e,f,g,h=corners
    m.quad(a,b,f,e);m.quad(c,g,h,d);m.quad(a,c,d,b)
    m.quad(e,f,h,g);m.quad(b,d,h,f);m.quad(a,e,g,c)

def build_tracks(belt):
    m=Mesh()
    path=loop_points()
    for side in (-1,1):
        xa,xb=(20.0,25.8) if side>0 else (-25.8,-20.0)
        if belt:
            # Outer belt ribbon facing side; duplicated on inner track side.
            for i,first in enumerate(path):
                second=path[(i+1)%len(path)]
                y0,z0=first;y1,z1=second
                m.quad((xa,y0,z0),(xb,y0,z0),(xb,y1,z1),(xa,y1,z1))
                m.quad((xb,y0,z0),(xb,y1,z1),(xa,y1,z1),(xa,y0,z0))
        else:
            for i,first in enumerate(path):
                tread_segment(m,xa-.15,xb+.15,first,path[(i+1)%len(path)])
    return m

def build_wheels(hubs):
    m=Mesh()
    for side in (-1,1):
        for z in (-21,-14,-7,0,7,14,21):
            if hubs:
                x=26.02 if side>0 else -26.02
                m.pipe((x,2.88,z),(x+(0.42 if side>0 else -0.42),2.88,z),
                       .56,14)
                for t in range(5):
                    theta=2*math.pi*t/5
                    m.pipe((x,2.88,z),(x,2.88+1.6*math.cos(theta),
                                      z+1.6*math.sin(theta)),.15,6)
            else:
                x0,x1=(25.25,25.94) if side>0 else (-25.94,-25.25)
                m.pipe((x0,2.85,z),(x1,2.85,z),1.88,16)
    return m

def build_superstructure():
    m=Mesh()
    # Front-side engineering districts, not disjoint columns.
    for x0,x1,z0,z1,roof in [
        (-16.2,-7.9,-19.,-8.5,12.3),
        (8.1,16.4,-20.,-6.5,14.0),
        (-16.1,-7.5,-2.5,10.5,11.4),
        (9.0,16.0,5.0,18.5,12.2),
        (-15.7,-9.1,13.,23.2,17.3),
    ]:
        m.box(x0,x1,5.92,roof,z0,z1,.28)
        # Massing has recessed industrial panel bays.
        for z in (z0+2,z1-2):
            if z0+1<z<z1-1:
                m.box(x0+.5,x1-.5,roof-.65,roof+.30,z-.45,z+.45,.4)
    # Main command bridge cantilevered over rear of the city.
    m.box(-8.3,8.3,6.0,12.9,15.,25.,.24)
    m.box(-6.8,6.8,12.95,18.25,18.2,24.7,.25)
    # Upper command observation deck.
    m.box(-7.7,7.7,18.25,18.65,17.6,25.5,.5)
    m.box(-3.6,3.6,18.65,21.0,19.5,23.5,.35)
    # Playable helm pedestal at z=8 stays visually obvious without a beige cube.\n    m.box(-1.35,1.35,5.87,6.6,6.65,9.45,.45)\n    m.box(-.86,.86,6.6,7.45,7.5,8.55,.48)\n    # Stern bunker and broad prow supports.
    m.box(-16.3,-10.,5.9,8.2,-24.1,-19.,.5)
    m.box(10.,16.3,5.9,8.2,-24.1,-19.,.5)
    return m

def build_roofwork():
    m=Mesh()
    for x0,x1,z0,z1,base_y,h in [
        (-16.6,-7.5,-19.4,-8.1,12.4,2.2),
        (7.7,16.7,-20.3,-6.1,14.1,2.5),
        (-16.5,-7.2,-2.9,10.9,11.6,2.0),
        (8.6,16.4,4.6,18.9,12.3,2.3),
        (-16.,-8.8,12.7,23.4,17.4,2.3),
        (-8.7,8.7,14.6,25.4,12.9,2.2),
    ]:
        cx=(x0+x1)*.5
        m.quad((x0,base_y,z0),(x0,base_y,z1),
               (cx,base_y+h,z1),(cx,base_y+h,z0),.36)
        m.quad((cx,base_y+h,z0),(cx,base_y+h,z1),
               (x1,base_y,z1),(x1,base_y,z0),.36)
        m.box(x0,x1,base_y-.22,base_y+.08,z0-.1,z0+.10,.30)
        m.box(x0,x1,base_y-.22,base_y+.08,z1-.1,z1+.10,.30)
    return m

def build_ribs():
    m=Mesh()
    # Long hull edge beams and diagonals.
    for side in (-1,1):
        x=side*18.1
        m.box(x-.38,x+.38,5.26,6.18,-25.3,25.3,.5)
        for z in range(-24,25,6):
            m.pipe((side*18.55,5.7,z-2.8),
                   (side*19.8,1.75,z+2.5),.19,8)
        # Parapet posts, not giant lemon-colored cubes.
        for z in range(-24,25,4):
            m.box(x-.18,x+.18,6.1,8.05,z-.16,z+.16)
        m.box(x-.18,x+.18,7.84,8.13,-25.,25.)
    for z in (-25.,25.):
        for x in range(-16,17,4):
            m.box(x-.17,x+.17,6.1,7.65,z-.18,z+.18)
        m.box(-17.,17.,7.60,7.85,z-.17,z+.17)
    # Cradle supports under the rear bridge.
    for side in (-1,1):
        x=side*7.3
        m.pipe((x,6.0,15.2),(x,12.9,24.3),.25,8)
    return m

def build_warning():
    m=Mesh()
    # Fine but deliberate hazard-striped outlines / identity panels.
    for side in (-1,1):
        x=side*18.2
        for z in (-23.,-15.,-7.,1.,9.,17.):
            m.box(x-.10,x+.10,5.7,5.8,z,z+2.1,.45)
        for z in (-22.,-12.,-2.,8.,18.):
            xcenter=side*17.6
            m.box(xcenter-.65,xcenter+.65,7.6,7.79,z-.3,z+.3,.45)
    # Amber control-housing trim around the real helm interaction point.\n    m.box(-.83,.83,7.45,7.54,7.5,8.55,.40)\n    m.box(-1.65,1.65,5.9,5.99,6.3,6.54,.55)\n    # Sighting line on the deck to command helm at z=8.
    for z in (-22,-19,-16,-13,-10,-7,-4,-1,2,5):
        m.box(-.14,.14,5.89,5.905,z-.46,z+.46,.5)
    return m

def build_windows():
    m=Mesh()
    # Dark glass on bridge front, realistic rhythms instead of giant holes.
    for x in range(-6,7,2):
        m.box(x-.76,x+.76,15.1,17.5,18.03,18.10,.5)
    for side in (-1,1):
        x=side*8.35
        for z in (17.3,20.,22.7):
            m.box(x-.08,x+.08,9.3,11.7,z-.74,z+.74,.5)
    return m

def build_pipes():
    m=Mesh()
    # Heavy exhaust stacks and interlinked engine manifolds.
    for x,z,r,height in [(-12.4,-13.4,1.05,16.8),(12.2,-15.5,1.4,19.7),
                          (-12.1,15.7,.85,20.2),(11.7,13.2,.72,16.9)]:
        m.pipe((x,7.5,z),(x,height,z),r,14)
        m.pipe((x,11.0,z),(x+.85,11.0,z),r*1.22,14)
        m.pipe((x,height-.3,z),(x,height+.15,z),r*1.15,14)
    for side in (-1,1):
        x=side*17.0
        m.pipe((x,7.,-18.),(x,7.,18.),.55,10)
        for z in (-15.,-4.,8.,18.):
            m.pipe((x,7.,z),(side*13.,7.,z),.38,8)
    return m

def build_tread_link():
    # One physical-looking grouser, whose centre follows the closed belt path.
    # Reused as an instanced YDR reference by all 96 authored Scene entities.
    m=Mesh()
    m.box(-2.9,2.9,-.15,.15,-1.01,1.01,.35)
    m.box(-2.85,2.85,.14,.31,-.31,.31,.55)
    for x in (-2.48,0.,2.48):
        m.box(x-.20,x+.20,.28,.34,-.80,.80,.35)
    return m

def build_wheel_spokes():
    m=Mesh()
    # Local rotation about X directly corresponds to ground travel / radius.
    m.pipe((-.22,0.,0.),(.22,0.,0.),.48,12)
    for j in range(6):
        a=2*math.pi*j/6
        cy,cz=1.35*math.cos(a),1.35*math.sin(a)
        m.pipe((0.,0.,0.),(0.,cy,cz),.17,7)
        m.pipe((-.15,cy,cz),(.15,cy,cz),.23,7)
    return m

def build_dust_puff():
    # Crossed upright cutout planes with transparent soft radial YTD albedo.
    m=Mesh()
    m.face(((-1.15,0,0),(1.15,0,0),(1.15,2.25,0),(-1.15,2.25,0)),.43,True)
    m.face(((0,0,-1.15),(0,0,1.15),(0,2.25,1.15),(0,2.25,-1.15)),.43,True)
    return m

def build_boulder(seed):
    m=Mesh()
    # Low-poly fractured rock with varied layers and stable UVs; avoids cubes.
    n=12
    phases=(.0,2*math.pi/n*.4,2*math.pi/n*.9,2*math.pi/n*1.2)
    rings=[]
    for ring,(height,radius) in enumerate(((.03,.58),(.29,.93),(.75,.72),(1.25,.30))):
        row=[]
        for i in range(n):
            a=2*math.pi*i/n+phases[ring]
            noise=.83+.25*math.sin((i+1)*(seed+3)*.79)+.1*math.cos((i+1)*3.7+seed)
            row.append((math.cos(a)*radius*noise,height*(.84+.13*math.cos(a*3+seed)),
                        math.sin(a)*radius*noise))
        rings.append(row)
    for k in range(len(rings)-1):
        for i in range(n):
            j=(i+1)%n
            m.quad(rings[k][i],rings[k][j],rings[k+1][j],rings[k+1][i],.42)
    m.face(rings[-1],.4)
    return m

def author():
    builders={
        "bastion_outer_hull":build_hull,
        "bastion_armor_belt":build_armor,
        "bastion_topdeck":build_deck,
        "bastion_walkway":build_walkway,
        "bastion_tread_belts":lambda:build_tracks(True),
        "bastion_tread_shoes":lambda:build_tracks(False),
        "bastion_wheels":lambda:build_wheels(False),
        "bastion_wheel_hubs":lambda:build_wheels(True),
        "bastion_tread_link":build_tread_link,
        "bastion_wheel_spokes":build_wheel_spokes,
        "bastion_dust_puff":build_dust_puff,
        "bastion_superstructure":build_superstructure,
        "bastion_roofwork":build_roofwork,
        "bastion_steel_ribs":build_ribs,
        "bastion_amber_marks":build_warning,
        "bastion_windows":build_windows,
        "bastion_pipework":build_pipes,
        "rustsea_boulder_a":lambda:build_boulder(1),
        "rustsea_boulder_b":lambda:build_boulder(5),
        "rustsea_boulder_c":lambda:build_boulder(9),
    }
    report={}
    DEST.mkdir(parents=True,exist_ok=True)
    for name,builder in builders.items():
        mesh=builder()
        ref="materials/rustsea.ymat@"+MATERIAL[name]
        if not mesh.vertices or len(mesh.vertices)>=200000:
            raise ValueError("Invalid authoring mesh "+name)
        path=DEST/(name+".ydr")
        path.write_bytes(native_ydr(name,"main",ref,mesh.vertices,mesh.indices))
        report[path.name]=verify(path,ref)
    output=ASSETS/"bastion_art_pass.json"
    output.write_text(json.dumps({
        "schema":1,"profile":"bastion.traction-city.art-pass.v2",
        "source":"procedurally authored for Sea of Rust",
        "licenses":"Original game-authored geometry; material sources remain CC0 Poly Haven",
        "playable_deck_y_m":5.7,
        "tracks":"visual-only, not simulated rotating belts",
        "models":report,
    },indent=2)+"\n",encoding="utf8")
    print(json.dumps({"ok":True,"additional_ydr":len(report),
                      "triangles":sum(x["triangles"] for x in report.values()),
                      "vertices":sum(x["vertices"] for x in report.values())}))
if __name__=="__main__":
    author()
