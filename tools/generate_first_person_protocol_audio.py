"""Generate original, license-clean NorthStar mission UI WAV cues."""
from pathlib import Path
import math
import struct
import wave
ROOT=Path(__file__).resolve().parents[1]
TARGET=ROOT/"Projects/NorthStarFirstPersonPlatform/assets/audio"
TARGET.mkdir(parents=True,exist_ok=True)
RATE=44100
def cue(name,notes,duration):
    n=int(duration*RATE)
    out=bytearray()
    for i in range(n):
        t=i/RATE
        # Soft attack, mechanical envelope and polished damped release.
        attack=min(1.,t/.015)
        release=min(1.,(duration-t)/.16)
        env=max(0.,min(attack,release))**1.3
        val=0.
        for start,length,freq,amp in notes:
            if start<=t<start+length:
                u=t-start
                # Slight frequency sweep and industrial overtone.
                hz=freq*(1.0+.035*math.exp(-u*18))
                partial=math.sin(2*math.pi*hz*u)
                partial+=.22*math.sin(2*math.pi*hz*2.01*u)
                partial+=.08*math.sin(2*math.pi*hz*3.98*u)
                val+=partial*amp*(1-math.exp(-u*145))*math.exp(-u*2.4)
        # Avoid abrupt edge clicks and clipping.
        sample=max(-1.,min(1.,val*env*.48))
        out+=struct.pack("<h",int(sample*32767))
    dest=TARGET/f"protocol_{name}.wav"
    with wave.open(str(dest),"wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(out)
    print(f"{dest.relative_to(ROOT)}: {len(out)} bytes audio")
cue("start",[(0,.22,247,.65),(.18,.31,370,.55)],.62)
cue("checkpoint",[(0,.14,329,.57),(.12,.18,494,.65)],.43)
cue("complete",[(0,.23,261,.45),(.16,.30,392,.5),(.36,.32,523,.6)],.80)
