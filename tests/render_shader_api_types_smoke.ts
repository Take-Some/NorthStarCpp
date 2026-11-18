import {
    API, Render, RenderShaders,
    type ShaderCatalog, type ShaderCatalogEntry,
    type RenderShaderAsset
} from "@northstar/engine";

const catalog: Readonly<ShaderCatalog> = API.render.shaders.catalog();
const list: readonly ShaderCatalogEntry[] = catalog.builtin;
const source: string = API.render.shaders.source("shaders/sky/atmosphere.glsl");
const loaded: Readonly<RenderShaderAsset> =
    API.render.shaders.load("shaders/sky/sun_optics.glsl");
const backend: string | null = API.render.provider.name();
const reloadCount: number = API.render.shaders.queueCompiled(
    "shaders/water.frag", "shaders/water.spv");
const active: boolean = RenderShaders.supportsHotReload;
const name: typeof Render = API.render;
const polling: Readonly<{pending:boolean;completed:readonly string[]}> =
    API.render.shaders.poll();
void [catalog,list,source,loaded,backend,reloadCount,active,name,polling];
