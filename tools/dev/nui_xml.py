#!/usr/bin/env python3
"""NorthStar UI XML v3: strict asset schema, deterministic serializer, migration.
Asset files are XML. Python's normalized dict is solely an internal/test IR.
No external dependencies. XML entities and DTDs are prohibited.
"""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

MAX_BYTES = 262144
IDENT = re.compile(r"^[A-Za-z][A-Za-z0-9_.-]{0,63}$")
HEX = re.compile(r"^#[0-9a-fA-F]{8}$")
KINDS = {"panel": "Panel", "text": "Text", "button": "Button",
         "image": "Image", "input": "Input"}
REV_KINDS = {v: k for k, v in KINDS.items()}
PROP_TYPES = {"string", "number", "bool", "color"}

def validate_name(name: str) -> str:
    if not isinstance(name, str) or not IDENT.fullmatch(name):
        raise ValueError("Invalid UI identifier: " + repr(name))
    return name

def type_of(value):
    if isinstance(value, dict) and set(value) == {"token"}:
        validate_name(value["token"])
        return "token"
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, (float, int)) and not isinstance(value, bool):
        if not -1000000 <= value <= 1000000:
            raise ValueError("UI number outside allowed range")
        return "number"
    if isinstance(value, str):
        if len(value.encode("utf-8")) > 2048:
            raise ValueError("UI string too long")
        return "color" if HEX.fullmatch(value) else "string"
    raise ValueError("Unsupported UI property " + repr(value))

def encode_prop(parent, name, value):
    validate_name(name)
    kind = type_of(value)
    attributes = {"name": name}
    if kind == "token":
        attributes["token"] = value["token"]
    else:
        attributes["type"] = kind
        attributes["value"] = (str(value).lower() if kind == "bool" else str(value))
    ET.SubElement(parent, "Property", attributes)

def write_xml(doc: dict) -> bytes:
    if doc.get("target") not in {"screen", "world"}:
        raise ValueError("Invalid UI target")
    nodes = doc.get("nodes")
    if not isinstance(nodes, list) or len(nodes) > 256:
        raise ValueError("Invalid UI nodes")
    options = {"version": "3", "target": doc["target"]}
    for key in ("layer", "entityId", "width", "height", "worldWidth", "worldHeight"):
        if key in doc:
            options[key] = str(doc[key])
    root = ET.Element("NorthStarUI", options)
    tokens = doc.get("tokens", {})
    styles = doc.get("styles", {})
    if not isinstance(tokens, dict) or len(tokens) > 128 or \
       not isinstance(styles, dict) or len(styles) > 128:
        raise ValueError("Invalid UI design system")
    if tokens:
        group = ET.SubElement(root, "Tokens")
        for name, value in tokens.items():
            validate_name(name)
            kind = type_of(value)
            if kind == "token":
                raise ValueError("Tokens cannot reference other tokens")
            ET.SubElement(group, "Token", {
                "name": name, "type": kind,
                "value": str(value).lower() if kind == "bool" else str(value)})
    if styles:
        group = ET.SubElement(root, "Styles")
        for name, props in styles.items():
            validate_name(name)
            if not isinstance(props, dict) or len(props) > 48:
                raise ValueError("Invalid UI style")
            element = ET.SubElement(group, "Style", {"name": name})
            for prop, value in props.items():
                encode_prop(element, prop, value)
    components = doc.get("components", {})
    if len(components)>64:
        raise ValueError("Too many UI components")
    if components:
        component_group=ET.SubElement(root,"Components")
        for name,spec in components.items():
            validate_name(name)
            if spec.get("type") not in KINDS: raise ValueError("Invalid component kind")
            widget=ET.SubElement(component_group,"Component",
                                 {"name":name,"type":spec["type"]})
            for key,value in spec.get("props",{}).items():
                encode_prop(widget,key,value)
    group = ET.SubElement(root, "Elements")
    known = {""}
    for node in nodes:
        if not isinstance(node, dict):
            raise ValueError("Invalid UI element")
        key = validate_name(node["id"])
        parent = node.get("parent", "")
        if key in known or parent not in known:
            raise ValueError("Duplicate or forward-referenced UI element: " + key)
        known.add(key)
        kind = node.get("type")
        if kind not in KINDS:
            raise ValueError("Unknown UI element type: " + repr(kind))
        properties = node.get("props", {})
        if not isinstance(properties, dict) or len(properties) > 48:
            raise ValueError("Invalid UI element properties")
        attrs = {"id": key}
        if parent:
            attrs["parent"] = parent
        if node.get("style"):
            attrs["style"] = validate_name(node["style"])
            if node["style"] not in styles:
                raise ValueError("Unknown style " + node["style"])
        component=node.get("component")
        if component:
            if component not in components or kind != components[component]["type"]:
                raise ValueError("Unknown or mismatched component")
            attrs["component"]=component
        item = ET.SubElement(group, "Use" if component else KINDS[kind], attrs)
        for name, value in properties.items():
            encode_prop(item, name, value)
    actions=doc.get("actions", [])
    if actions:
        group=ET.SubElement(root,"Actions")
        for action in actions:
            ET.SubElement(group,"Action",{
                "id":action["id"],"event":action["event"],"emit":action["emit"],
                **({"animation":action["animation"]} if action.get("animation") else {})})
    animations=doc.get("animations",[])
    if animations:
        group=ET.SubElement(root,"Animations")
        for entry in animations:
            ET.SubElement(group,"Animation",{
                "name":entry["name"],"id":entry["id"],"property":entry["property"],
                "from":str(entry["from"]),"to":str(entry["to"]),
                "duration":str(entry["duration"]),
                "easing":entry.get("easing","linear"),
                "trigger":entry.get("trigger","manual")})
    breakpoints=doc.get("breakpoints",[])
    if breakpoints:
        group=ET.SubElement(root,"Breakpoints")
        for bp in breakpoints:
            b=ET.SubElement(group,"Breakpoint",{
                "minWidth":str(bp.get("minWidth",0)),
                "maxWidth":str(bp.get("maxWidth",16384))})
            for target,props in bp["overrides"].items():
                item=ET.SubElement(b,"Override",{"id":target})
                for key,value in props.items():encode_prop(item,key,value)
    ET.indent(root, space="  ")
    payload = ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n"
    if len(payload) > MAX_BYTES:
        raise ValueError("UI XML document exceeds 256 KiB")
    return payload

def parse_prop(item):
    if item.tag != "Property" or set(item.attrib) - {"name", "type", "value", "token"}:
        raise ValueError("Unexpected UI property definition")
    name = validate_name(item.attrib["name"])
    if list(item) or (item.text or "").strip():
        raise ValueError("UI properties cannot contain nested content")
    if "token" in item.attrib:
        if "value" in item.attrib or "type" in item.attrib:
            raise ValueError("A token reference cannot also have a literal value")
        return name, {"token": validate_name(item.attrib["token"])}
    kind = item.attrib["type"]
    value = item.attrib["value"]
    if kind == "bool":
        if value not in ("true", "false"):
            raise ValueError("Invalid boolean literal")
        return name, value == "true"
    if kind == "number":
        v = float(value)
        if not -1000000 <= v <= 1000000:
            raise ValueError("Number outside allowed range")
        return name, v
    if kind == "color":
        if not HEX.fullmatch(value):
            raise ValueError("Invalid #RRGGBBAA color")
        return name, value
    if kind == "string":
        if len(value.encode("utf-8")) > 2048:
            raise ValueError("UI text too long")
        return name, value
    raise ValueError("Unknown property type " + kind)

def parse_props(node):
    if len(node) > 48:
        raise ValueError("More than 48 properties")
    props = {}
    for item in node:
        key, val = parse_prop(item)
        if key in props:
            raise ValueError("Duplicate property " + key)
        props[key] = val
    return props

def read_xml(source: bytes | str) -> dict:
    if isinstance(source, str):
        source = source.encode("utf-8")
    if not 0 < len(source) <= MAX_BYTES or \
       b"<!DOCTYPE" in source.upper() or b"<!ENTITY" in source.upper():
        raise ValueError("Unsafe or oversized UI XML")
    root = ET.fromstring(source)
    if root.tag != "NorthStarUI" or root.get("version") != "3":
        raise ValueError("Expected NorthStarUI XML version 3")
    allowed = {"version", "target", "layer", "entityId", "width", "height",
               "worldWidth", "worldHeight"}
    if set(root.attrib) - allowed or root.get("target") not in {"screen", "world"}:
        raise ValueError("Invalid UI root")
    doc = {"schema": "northstar.ui/2", "target": root.attrib["target"],
           "nodes": [], "styles": {}, "tokens": {}, "components": {},
           "actions": [], "animations": [], "breakpoints": []}
    for key in ("layer", "entityId", "width", "height", "worldWidth", "worldHeight"):
        if key in root.attrib:
            doc[key] = float(root.get(key)) if key in \
                {"width", "height", "worldWidth", "worldHeight"} else root.get(key)
    groups = {}
    for item in root:
        if item.tag not in {"Tokens", "Styles", "Components", "Elements", "Actions", "Animations", "Breakpoints"} or item.tag in groups:
            raise ValueError("Duplicate or unknown UI group " + item.tag)
        groups[item.tag] = item
    for item in groups.get("Tokens", []):
        if item.tag != "Token" or set(item.attrib) != {"name", "type", "value"}:
            raise ValueError("Invalid token")
        name = validate_name(item.get("name"))
        value = parse_prop(ET.Element("Property", item.attrib))
        if name in doc["tokens"]:
            raise ValueError("Duplicate token")
        doc["tokens"][name] = value[1]
    for item in groups.get("Styles", []):
        if item.tag != "Style" or set(item.attrib) != {"name"}:
            raise ValueError("Invalid style")
        key = validate_name(item.get("name"))
        if key in doc["styles"]:
            raise ValueError("Duplicate style")
        doc["styles"][key] = parse_props(item)
    if len(doc["tokens"]) > 128 or len(doc["styles"]) > 128:
        raise ValueError("Too many UI styles/tokens")
    for item in groups.get("Components", []):
        if item.tag!="Component" or set(item.attrib)!={"name","type"}:
            raise ValueError("Expected typed Component")
        name=validate_name(item.get("name"))
        kind=item.get("type")
        if kind not in KINDS or name in doc["components"]:
            raise ValueError("Invalid reusable component")
        doc["components"][name]={"type":kind,"props":parse_props(item)}
    if len(doc["components"])>64:raise ValueError("Too many components")
    known = {""}
    for item in groups.get("Elements", []):
        if item.tag not in REV_KINDS and item.tag!="Use" or \
           set(item.attrib) - {"id", "parent", "style", "component"}:
            raise ValueError("Unexpected UI widget: " + item.tag)
        key = validate_name(item.get("id"))
        parent = item.get("parent", "")
        if key in known or parent not in known:
            raise ValueError("Invalid UI hierarchy " + key)
        known.add(key)
        component=item.get("component")
        if item.tag=="Use":
            if component not in doc["components"]:
                raise ValueError("Unknown reusable UI component")
            kind=doc["components"][component]["type"]
        else:
            if component is not None:raise ValueError("Only Use can declare component")
            kind=REV_KINDS[item.tag]
        node={"id":key,"type":kind,"parent":parent,"props":parse_props(item)}
        if component:node["component"]=component
        if "style" in item.attrib:
            node["style"] = validate_name(item.get("style"))
            if node["style"] not in doc["styles"]:
                raise ValueError("Unknown UI style")
        doc["nodes"].append(node)
    if len(doc["nodes"]) > 256:
        raise ValueError("Too many UI elements")
    for item in groups.get("Actions", []):
        if item.tag!="Action" or set(item.attrib)-{"id","event","emit","animation"}:
            raise ValueError("Invalid UI Action")
        entry=dict(item.attrib)
        if entry.get("id") not in known or entry.get("event") not in ("click","press","change"):
            raise ValueError("Invalid UI action target/event")
        if not re.fullmatch(r"[A-Za-z0-9_.:-]{1,96}",entry.get("emit","")):
            raise ValueError("Invalid event name")
        entry.setdefault("animation","")
        doc["actions"].append(entry)
    if len(doc["actions"])>128:raise ValueError("Too many UI actions")
    animations=set()
    for item in groups.get("Animations", []):
        if item.tag!="Animation" or set(item.attrib)-{"name","id","property","from","to","duration","easing","trigger"}:
            raise ValueError("Invalid UI Animation")
        entry=dict(item.attrib)
        validate_name(entry["name"])
        if entry["name"] in animations or entry.get("id") not in known or            entry.get("property") not in ("x","y","width","height","font_scale"):
            raise ValueError("Invalid animation name/target/property")
        animations.add(entry["name"])
        if entry.get("easing","linear") not in ("linear","easeOutCubic","easeInOut") or            entry.get("trigger","manual") not in ("manual","mount"):
            raise ValueError("Invalid animation mode")
        for key in ("from","to","duration"):
            entry[key]=float(entry[key])
            if not __import__("math").isfinite(entry[key]) or                abs(entry[key])>1000000:raise ValueError("Invalid animation number")
        if not 0.001<=entry["duration"]<=30:raise ValueError("Invalid animation duration")
        entry.setdefault("easing","linear");entry.setdefault("trigger","manual")
        doc["animations"].append(entry)
    if len(doc["animations"])>128:raise ValueError("Too many animations")
    for action in doc["actions"]:
        if action["animation"] and action["animation"] not in animations:
            raise ValueError("Action references unknown animation")
    for item in groups.get("Breakpoints", []):
        if item.tag!="Breakpoint" or set(item.attrib)-{"minWidth","maxWidth"}:
            raise ValueError("Invalid UI Breakpoint")
        low=int(item.get("minWidth","0"));high=int(item.get("maxWidth","16384"))
        if low<0 or high>16384 or low>high:raise ValueError("Invalid responsive range")
        bp={"minWidth":low,"maxWidth":high,"overrides":{}}
        for override in item:
            if override.tag!="Override" or set(override.attrib)!={"id"} or                override.get("id") not in known or override.get("id") in bp["overrides"]:
                raise ValueError("Invalid breakpoint override")
            bp["overrides"][override.get("id")]=parse_props(override)
        doc["breakpoints"].append(bp)
    if len(doc["breakpoints"])>64:raise ValueError("Too many breakpoints")
    for props in [*doc["styles"].values(),*(item["props"] for item in doc["components"].values()),
                  *(node["props"] for node in doc["nodes"]),
                  *(props for bp in doc["breakpoints"] for props in bp["overrides"].values())]:
        for name, value in props.items():
            if isinstance(value, dict) and value.get("token") not in doc["tokens"]:
                raise ValueError("Undefined UI token: " + str(value.get("token")))
    return doc

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=("migrate", "check", "to-json", "to-json-stdin", "from-json-stdin"))
    ap.add_argument("paths", nargs="*")
    args = ap.parse_args()
    if args.action == "to-json-stdin":
        print(json.dumps(read_xml(sys.stdin.buffer.read()), ensure_ascii=False))
        return
    if args.action == "from-json-stdin":
        sys.stdout.buffer.write(write_xml(json.load(sys.stdin)))
        return
    if not args.paths:
        ap.error("No .nui files specified")
    for filename in args.paths:
        p = Path(filename)
        if args.action == "migrate":
            existing = p.read_bytes()
            if existing.lstrip().startswith(b"<"):
                doc = read_xml(existing)
            else:
                doc = json.loads(existing)
            data = write_xml(doc)
            if read_xml(data)["nodes"] != doc["nodes"]:
                raise ValueError("Lossy XML migration at " + str(p))
            p.write_bytes(data)
            print("XML", p)
        elif args.action == "check":
            doc = read_xml(p.read_bytes())
            print("PASS", p, len(doc["nodes"]), "widgets")
        else:
            print(json.dumps(read_xml(p.read_bytes()), ensure_ascii=False))
if __name__ == "__main__":
    main()
