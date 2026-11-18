#!/usr/bin/env python3
"""Compare REAL captured sky-only GPU readback frames, no synthetic images.

PPM is uncompressed RGB8. Report is descriptive: no ungrounded claim of
cross-provider photometric equality or temporal reprojection acceptance.
"""
from pathlib import Path
from statistics import mean,median
import argparse,json,re

ROOT=Path(__file__).resolve().parents[2]
DEFAULT=ROOT/"out/qa/sky_frames"

def load(path):
    with path.open("rb") as f:
        assert f.readline().strip()==b"P6",path
        width,height=map(int,f.readline().split())
        assert f.readline().strip()==b"255"
        rgb=f.read()
    assert len(rgb)==width*height*3,(path,len(rgb),width,height)
    return (width,height),rgb

def difference(left,right,stride=1,dx=0):
    (w,h),a=left
    dims,b=right
    assert dims==(w,h),dims
    total=0
    count=0
    over24=0
    largest=0
    # Shift right image along X to approximate uniform yaw camera motion.
    for y in range(0,h,stride):
        for x in range(max(0,-dx),min(w,w-dx),stride):
            aa=(y*w+x)*3
            bb=(y*w+x+dx)*3
            d=(abs(a[aa]-b[bb])+abs(a[aa+1]-b[bb+1])+abs(a[aa+2]-b[bb+2]))/3
            total+=d
            largest=max(largest,d)
            count+=1
            over24+=d>24
    return {"mean_abs_rgb8":round(total/count,3),
            "fraction_pixel_delta_gt24":round(over24/count,4),
            "peak_pixel_delta":round(largest,2),
            "sampled_pixels":count}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--root",type=Path,default=DEFAULT)
    args=parser.parse_args()
    path=args.root
    report={"captures":{},"provider_parity":{},"motion":{}}
    for backend in ("vulkan","d3d12"):
        frames=sorted(path.glob(backend+"_*.ppm"))
        assert len(frames)>=19,(backend,len(frames))
        report["captures"][backend]=len(frames)
    for n in range(19):
        va=list(path.glob(f"vulkan_{n}_*.ppm"))
        da=list(path.glob(f"d3d12_{n}_*.ppm"))
        assert len(va)==len(da)==1
        suffix=va[0].stem.split("_",2)[-1]
        assert suffix==da[0].stem.split("_",2)[-1]
        a,b=load(va[0]),load(da[0])
        report["provider_parity"][suffix]=difference(a,b,stride=2)
    for backend in ("vulkan","d3d12"):
        motion=[]
        for n in range(8,19):
            prev=next(path.glob(f"{backend}_{n-1}_cloud-motion-*.ppm"))
            now=next(path.glob(f"{backend}_{n}_cloud-motion-*.ppm"))
            before,current=load(prev),load(now)
            candidates=[(difference(before,current,stride=2,dx=offset),offset)
                        for offset in range(-3,4)]
            best,offset=min(candidates,key=lambda item:item[0]["mean_abs_rgb8"])
            motion.append({"frame":n-7,"approx_yaw_shift_pixels":offset,
                           "registered":best,
                           "unregistered":difference(before,current,stride=2)})
        report["motion"][backend]={"pairs":len(motion),
            "median_registered_mad":round(median(m["registered"]["mean_abs_rgb8"] for m in motion),3),
            "median_unregistered_mad":round(median(m["unregistered"]["mean_abs_rgb8"] for m in motion),3),
            "frames":motion}
    output=path/"sky_parity_report.json"
    output.write_text(json.dumps(report,indent=2),encoding="utf8")
    print("PASS captured RGB frames parsed, D3D12/Vulkan aligned by capture label")
    print("PROVIDER_MAD",json.dumps({
        k:v["mean_abs_rgb8"] for k,v in report["provider_parity"].items()
        if k in ("noon","noon-cloud-coverage","overcast","cloud-motion-0","cloud-motion-11")}))
    print("MOTION_MAD",json.dumps({
        k:(v["median_unregistered_mad"],v["median_registered_mad"])
        for k,v in report["motion"].items()}))
    print("REPORT",output)
if __name__=="__main__":main()
