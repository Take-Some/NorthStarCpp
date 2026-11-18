"""Deterministic flat wasteland generator for moving-cities vertical slices.

Physical surface is a uniform plane; procedural visual variety is generated
from seeded value noise. Keeping the collision level flat allows moving-city
local physics frames to remain deterministic until native moving platforms
are implemented.
"""
from __future__ import annotations
from dataclasses import dataclass
import math

@dataclass(frozen=True)
class TerrainSettings:
    seed: int = 20261117
    radius: int = 8
    cell_size: float = 28.0
    surface_y: float = -5.7
    corridor_half_width: float = 35.0

    def validate(self):
        if not 0 <= self.seed <= 0xffffffff:
            raise ValueError("terrain seed must be uint32")
        if not 3 <= self.radius <= 12:
            raise ValueError("terrain radius outside authoring budget")
        if not 12 <= self.cell_size <= 40:
            raise ValueError("terrain cell size outside authoring budget")
        if not -60 <= self.surface_y <= 2:
            raise ValueError("terrain surface outside supported range")
        return self

def _hash(seed: int, x: int, z: int) -> float:
    # Unsigned multiplication is explicitly 32-bit on all platforms.
    h = (seed ^ ((x * 0x9e3779b1) & 0xffffffff) ^
         ((z * 0x85ebca6b) & 0xffffffff)) & 0xffffffff
    h ^= h >> 16
    h = (h * 0x7feb352d) & 0xffffffff
    h ^= h >> 15
    h = (h * 0x846ca68b) & 0xffffffff
    h ^= h >> 16
    return h / 0xffffffff

def value_noise(seed: int, x: float, z: float, spacing: float) -> float:
    if not math.isfinite(x) or not math.isfinite(z) or spacing <= 0:
        raise ValueError("non-finite noise coordinate")
    px,pz=x/spacing,z/spacing
    ix,iz=math.floor(px),math.floor(pz)
    fx,fz=px-ix,pz-iz
    fx=fx*fx*(3-2*fx)
    fz=fz*fz*(3-2*fz)
    a=_hash(seed,ix,iz)
    b=_hash(seed,ix+1,iz)
    c=_hash(seed,ix,iz+1)
    d=_hash(seed,ix+1,iz+1)
    return (a+(b-a)*fx)*(1-fz)+(c+(d-c)*fx)*fz

def biome(seed: int, world_x: float, world_z: float) -> tuple[int, float]:
    # Nested noise frequencies, no global mutable RNG.
    coarse=value_noise(seed,world_x,world_z,200.0)
    fine=value_noise(seed ^ 0x510ba11,world_x,world_z,70.0)
    moisture=value_noise(seed ^ 0x13198a2e,world_x,world_z,340.0)
    terrain=0.68*coarse+0.32*fine
    if moisture<0.28:
        palette=((0x654b3b),(0x72513c),(0x7f6146),(0x8b6e51))
    elif terrain>0.63:
        palette=((0x6c6358),(0x786d5d),(0x887860),(0x9c865e))
    else:
        palette=((0x605a50),(0x6c6050),(0x796752),(0x877257))
    band=min(3,max(0,int(terrain*4)))
    return palette[band],terrain

def tiles(settings: TerrainSettings):
    settings.validate()
    for iz in range(-settings.radius,settings.radius+1):
        for ix in range(-settings.radius,settings.radius+1):
            x=ix*settings.cell_size
            z=iz*settings.cell_size
            color,_=biome(settings.seed,x,z)
            yield {
                "ix":ix,"iz":iz,"x":x,"z":z,"size":settings.cell_size,
                "bottom_y":settings.surface_y-settings.cell_size,
                "color":color,
            }

def outcrops(settings: TerrainSettings):
    """Sparse deterministic landmarks, never in either city's travel corridor."""
    settings.validate()
    for iz in range(-settings.radius, settings.radius+1):
        for ix in range(-settings.radius, settings.radius+1):
            if abs(ix*settings.cell_size)<settings.corridor_half_width+18:
                continue
            rnd=_hash(settings.seed ^ 0xd2a29f31, ix, iz)
            if rnd < 0.58:
                continue
            x=(ix+0.5*(_hash(settings.seed^0x45,ix,iz)-0.5))*settings.cell_size
            z=(iz+0.5*(_hash(settings.seed^0x46,ix,iz)-0.5))*settings.cell_size
            size=1.2+1.6*_hash(settings.seed^0x47,ix,iz)
            yield {"ix":ix,"iz":iz,"x":x,"z":z,"size":size,
                   "y":settings.surface_y+0.05,
                   "color":0x48443e if rnd<0.81 else 0x3e403e}
