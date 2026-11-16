from pathlib import Path
p=Path(__file__).resolve().parents[1]/"sharedAssets/scripts/northstar/bootstrap.js"
s=p.read_text(encoding="utf-8-sig")
a=s.index("    class UI {")
b=s.index("    class Logger {",a)
s=s[:a]+r'''    // Game-agnostic UI renderer. Project scripts own every semantic layer:
    // quests, health, inventory, notifications, and screen advertising.
    // Only engine.ui.apply(document) crosses into native C++.
    class UI {
        static #layers = new Map();

        static setTitle(text) { return engine.ui.setTitle(text); }

        static #typed(value) {
            if (typeof value === "number") {
                if (!Number.isFinite(value))
                    throw new TypeError("UI numeric property must be finite");
                return {type:"number",value};
            }
            if (typeof value === "boolean") return {type:"bool",value};
            if (typeof value === "string") {
                if (/^#[0-9a-fA-F]{8}$/.test(value))
                    return {type:"color",value};
                return {type:"text",value};
            }
            throw new TypeError("UI property must be string, boolean or number");
        }

        static #flush() {
            const nodes = [];
            for (const [layer, items] of UI.#layers) {
                for (const node of items) {
                    const props = {};
                    for (const [key, value] of Object.entries(node.props ?? {}))
                        props[key] = UI.#typed(value);
                    nodes.push({
                        id: layer + "." + node.id,
                        parent: node.parent ? layer + "." + node.parent : "",
                        type: node.type,
                        props
                    });
                }
            }
            return engine.ui.apply({target:"screen",nodes});
        }

        static mount(layer, elements) {
            if (typeof layer !== "string" ||
                !/^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/.test(layer) ||
                !Array.isArray(elements) || elements.length > 256)
                throw new TypeError("UI.mount requires a layer and bounded element array");
            // Clone user data so later mutation cannot silently desynchronize
            // the native document from its script-owned source.
            const owned = elements.map(node => {
                if (!node || typeof node !== "object" ||
                    !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.id) ||
                    (node.parent !== undefined && node.parent !== "" &&
                     !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.parent)) ||
                    !["panel","text","button","image","input"].includes(node.type))
                    throw new TypeError("UI node descriptor is invalid");
                return {
                    id: node.id, type:node.type, parent:node.parent ?? "",
                    props:{...node.props}
                };
            });
            UI.#layers.set(layer, owned);
            return UI.#flush();
        }

        static unmount(layer) {
            if (UI.#layers.delete(layer)) return UI.#flush();
        }

        static render(elements) { return UI.mount("default", elements); }

        static clear() {
            UI.#layers.clear();
            return UI.#flush();
        }
    }

'''+s[b:]
p.write_text(s,encoding="utf-8",newline="")
print("Game-agnostic JS layers render through engine.ui.apply only.")
