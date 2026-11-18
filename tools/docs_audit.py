#!/usr/bin/env python3
"""Audit and regenerate the NorthStarCpp documentation index.

python tools/docs_audit.py --write-index
python tools/docs_audit.py --check
"""
from pathlib import Path
from collections import defaultdict
from urllib.parse import urlsplit, unquote
import argparse, json, re, sys, os

ROOT=Path(__file__).resolve().parents[1]
DOCS=ROOT/"docs"
INDEX=DOCS/"INDEX.md"
EXCLUDE={".git","out","dist","build","vendor","node_modules","__pycache__","generated",".northstarcpp"}
CANONICAL={
"INDEX.md":"Full documentation catalog (generated)",
"DOC_POLICY.md":"Documentation style, ownership and historical evidence",
"CURRENT_STATUS.md":"Current engineering status and release acceptance",
"START_HERE.md":"Supported build and launch guide",
"ARCHITECTURE.md":"Architecture and dependency boundaries",
"MODULES.md":"Provider plugin and ABI contracts",
"ENGINE_LAYOUT.md":"Source layout and migration provenance",
"SCRIPTING_API.md":"Native scripting contract",
"SCENE_V3_STREAMING.md":"Scene V3 and streaming",
"PREFABS.md":"Prefab format and authoring",
"CAMERA_SYSTEM.md":"Camera API",
"VEHICLE_SYSTEM.md":"Vehicle API",
"UI_API.md":"UI API",
"NAC_FORMAT_V1.md":"Audio NAC format",
"YMAT_XML_ONLY_MIGRATION_2026-11-18.md":"XML YMAT migration and format acceptance",
"ASSET_PIPELINE_P1_FOUNDATION_2026-11-18.md":"Asset Pipeline and DDC foundation",
"GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md":"Dated gameplay test evidence",
"NORTHSTARCPP_1_0_EXECUTION_PLAN_2026-11-18.md":"Release acceptance criteria",
"DISTRIBUTION.md":"Packaging and distribution",
"AUTHORSHIP.md":"Authorship and third-party notices"}
CATEGORIES=[
("Characters and animation",("ABBY","ELLIE","TLOU","ANIMATION","SKELET","IK_","GROUNDED","POSE","CLAVICLE")),
("Gameplay and weapons",("GAMEPLAY","PLAYER","FPS","PAWN","WEAPON","PISTOL","BALLISTIC","ROOT_MOTION","FIRST_PERSON")),
("Render and GPU",("RENDER","VULKAN","D3D","SHADER","SKY","CLOUD","GPU","ZF_","FRAME_PIPELINE")),
("Editor and tools",("EDITOR","QT_","UE532_EDITOR","WORLD_OUTLINER","CONTENT_BROWSER","PROJECT_BROWSER","PLACE_ACTORS","PIE_","PLAY_IN_EDITOR","ASSET_BROWSER")),
("Assets and materials",("ASSET","MATERIAL","YMAT","NEF8","PREFAB","VFS","NATIVE_MODEL","WORLD_SURFACES","NSP_")),
("World, scene and vehicles",("VEHICLE","WORLD","SCENE","MT28","BASTION","RUSTSEA")),
("Scripting and APIs",("SCRIPT","V8_","ENGINE_API","SDK","PERLIN","NUI")),
("UI and audio",("AUDIO","FOOTSTEP","NAC_","UI_","NUI_")),
("Release and architecture",("RELEASE","BUILD","QA","DISTRIBUTION","ARCHITECT","ENGINE_LAYOUT","GCC","PERFORMANCE","FOUNDATION","LOGGER","NORTHSTARCPP_"))]

def source_files():
    bases=[ROOT/"README.md",*DOCS.rglob("*.md"),*(ROOT/"Projects").rglob("*.md"),
           *(ROOT/"engine").rglob("*.md"),*(ROOT/"tools").rglob("*.md"),
           *(ROOT/"tests").rglob("*.md")]
    return sorted({p.resolve() for p in bases if p.is_file()
                   and not (EXCLUDE & set(p.relative_to(ROOT).parts))})

def without_fences(content):
    out=[]; marker=None
    for line in content.splitlines():
        match=re.match(r"^\s*(~{3,}|"+chr(96)+r"{3,})",line)
        if match:
            c=match.group(1)[0]
            if marker is None: marker=c
            elif marker==c: marker=None
            out.append("")
        else:out.append("" if marker else line)
    return "\n".join(out)

def doc_title(p):
    match=re.search(r"(?m)^#\s+(.+)$",
                    without_fences(p.read_text("utf8",errors="replace")))
    return match.group(1).strip() if match else p.stem.replace("_"," ")

def rel(src,dst):
    return Path(os.path.relpath(dst,src.parent)).as_posix()

def generate_index():
    all_docs=sorted(DOCS.rglob("*.md"))
    buckets=defaultdict(list)
    for p in all_docs:
        if p.name in CANONICAL:continue
        upper=p.name.upper()
        topic=next((name for name,keywords in CATEGORIES
                    if any(keyword in upper for keyword in keywords)),
                   "Other research and work notes")
        buckets[topic].append(p)
    tick=chr(96)
    lines=[
        "# NorthStarCpp — Documentation Index","",
        "> Generated using "+tick+"python tools/docs_audit.py --write-index"+tick+".",
        "> Do not edit this inventory manually. See [Documentation Policy](DOC_POLICY.md).","",
        "## Where to start","",
        "Use [Current Status](CURRENT_STATUS.md) for live engineering conclusions,",
        "[Quick Start](START_HERE.md) for builds, and",
        "[Architecture](ARCHITECTURE.md) for provider boundaries.",
        "Dated reports preserve historical evidence and are not current specifications.","",
        "## Canonical and maintained references","",
        "| Document | Responsibility |","| --- | --- |"]
    for name,descr in CANONICAL.items():
        p=DOCS/name
        if p.exists() or p==INDEX:lines.append("| ["+name+"]("+rel(INDEX,p)+") | "+descr+" |")
    lines+=["","## Dated engineering reports and historical notes","",
            "Reports remain intact for provenance and original QA data.",
            "Later findings do not retroactively overwrite earlier test evidence.",""]
    for name in [*[x for x,_ in CATEGORIES],"Other research and work notes"]:
        entries=sorted(buckets.pop(name,[]))
        if not entries:continue
        lines+=["### "+name+" ("+str(len(entries))+")",""]
        for p in entries:lines.append("- ["+doc_title(p)+"]("+rel(INDEX,p)+")")
        lines.append("")
    others=sorted(p for p in source_files()
                  if not p.is_relative_to(DOCS) and p!=ROOT/"README.md")
    lines+=["## Project, engine and tool documentation ("+str(len(others))+")","",
            "These retain their own ownership; their contents are not reproduced here.",""]
    folders=defaultdict(list)
    for p in others:folders[p.relative_to(ROOT).parts[0]].append(p)
    for folder,items in sorted(folders.items()):
        lines+=["### "+folder+" ("+str(len(items))+")",""]
        for p in items:
            relative=p.relative_to(ROOT).as_posix()
            lines.append("- ["+relative+"]("+rel(INDEX,p)+")")
        lines.append("")
    return "\n".join(lines).rstrip()+"\n"

LINK=re.compile(r"!?\[[^]\n]*\]\((<[^>]+>|[^\s)]+)")
HTML=re.compile(r"""(?:href|src)\s*=\s*["']([^"']+)["']""")
def audit():
    src=source_files()
    bad=[]; h1=[]; blocks=defaultdict(set);local_links=0
    for p in src:
        text=without_fences(p.read_text("utf8",errors="replace"))
        text=re.sub(chr(96)+r"[^"+chr(96)+r"\n]*"+chr(96),"",text)
        relative=p.relative_to(ROOT).as_posix()
        if p.is_relative_to(DOCS):
            count=len(re.findall(r"(?m)^#\s+\S",text))
            if count!=1:h1.append([relative,count])
        for url in [*LINK.findall(text),*HTML.findall(text)]:
            url=url.strip("<>")
            if url.startswith(("#","//","mailto:","tel:","data:")):continue
            parts=urlsplit(url)
            if parts.scheme or parts.netloc:continue
            target=unquote(parts.path)
            if not target or any(x in target for x in ("{","}","*")):continue
            local_links+=1
            if not (p.parent/target).exists():bad.append([relative,url])
        for para in re.split(r"\n\s*\n",text):
            flat=" ".join(line.strip() for line in para.splitlines()).strip()
            if len(flat)>=180 and not flat.startswith(("#","|","<")):
                blocks[flat].add(relative)
    repeat=[[len(t),sorted(loc)] for t,loc in blocks.items() if len(loc)>1]
    # Reusable template copies and immutable migration snapshots intentionally
    # remain independent; these are not duplicate engine specifications.
    def intentional(loc):
        paths=set(loc)
        pairs=[
            {"Projects/MovingCitiesPrototype/README.md",
             "tools/templates/moving_cities/README.md"},
            {"Projects/AbbyPistolDemo/README.md",
             "tools/templates/abby_pistol_demo/README.md"},
            {"Projects/KaylaNightCity/prefabs/README.md",
             "Projects/NorthStarFirstPersonPlatform/prefabs/README.md"}
        ]
        snapshots=(len(paths)>=2 and all(
            x.startswith("Projects/MovingCitiesPrototype/migration/") and
            x.endswith("/README.md") for x in paths))
        return snapshots or any(paths<=allowed for allowed in pairs)
    unexpected=[row for row in repeat if not intentional(row[1])]
    return {"documents":len(src),"docs_markdown":len(list(DOCS.rglob("*.md"))),
            "links_checked":local_links,"broken_links":sorted(bad),
            "h1_violations":sorted(h1),
            "cross_document_identical_long_paragraphs":sorted(repeat,reverse=True),
            "unexpected_duplicate_paragraphs":sorted(unexpected,reverse=True),
            "index_current":INDEX.exists() and INDEX.read_text("utf8")==generate_index()}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-index",action="store_true")
    parser.add_argument("--check",action="store_true")
    parser.add_argument("--json",action="store_true")
    args=parser.parse_args()
    if args.write_index:INDEX.write_text(generate_index(),encoding="utf8",newline="\n")
    findings=audit()
    if args.json:print(json.dumps(findings,ensure_ascii=True,indent=2))
    else:
        for key,val in findings.items():
            print(key,len(val) if isinstance(val,list) else val)
            if key in ("broken_links","h1_violations"):
                for item in val[:20]:print(str(item).encode("ascii","backslashreplace").decode())
    return int(args.check and (findings["broken_links"] or findings["h1_violations"]
                               or findings["unexpected_duplicate_paragraphs"]
                               or not findings["index_current"]))
if __name__=="__main__":sys.exit(main())
