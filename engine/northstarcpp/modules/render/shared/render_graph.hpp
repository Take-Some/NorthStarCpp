#pragma once
// NorthStarCpp render pass scheduler. Backend-neutral; no GPU API objects cross the plugin ABI.
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace northstarcpp::render {
using ResourceId = std::uint32_t;
using PassId = std::uint32_t;

// Backend-neutral intent. Backends lower each access into D3D12 resource
// states or Vulkan stage/access masks and image layouts.
enum class ResourceAccess : std::uint8_t {
    Undefined, ShaderRead, ColorAttachment, DepthWrite, DepthRead,
    CopySource, CopyDestination, Present
};
struct Resource {
    std::string name;
    bool imported = false;
    ResourceAccess initial_access = ResourceAccess::Undefined;
};
struct ResourceUsage {
    ResourceId resource;
    ResourceAccess access;
};
struct AccessTransition {
    ResourceId resource;
    ResourceAccess before;
    ResourceAccess after;
};
struct Pass {
    std::string name;
    std::vector<ResourceId> reads;
    std::vector<ResourceId> writes;
    std::vector<PassId> after;
    std::vector<ResourceUsage> usages; // Optional for legacy graph clients.
};
struct ScheduledPass {
    PassId id;
    std::vector<ResourceId> barriers; // Resources requiring state/hazard handling before this pass.
    std::vector<AccessTransition> transitions; // Typed state requests for this pass.
};
struct RenderGraphPlan {
    std::vector<ScheduledPass> passes;
    std::vector<std::pair<int,int>> lifetimes; // First and last scheduled use; {-1,-1} if unused.
};

class RenderGraph {
public:
    ResourceId add_resource(std::string name, bool imported = false,
                            ResourceAccess initial = ResourceAccess::Undefined) {
        if (name.empty()) throw std::invalid_argument("Render resource name is empty");
        if(!imported && initial!=ResourceAccess::Undefined)
            throw std::invalid_argument("Only imported resources may declare initial access");
        for (auto const& r : resources_)
            if (r.name == name) throw std::invalid_argument("Duplicate render resource: " + name);
        resources_.push_back({std::move(name), imported, initial});
        return static_cast<ResourceId>(resources_.size() - 1);
    }

    PassId add_pass(Pass pass) {
        if (pass.name.empty()) throw std::invalid_argument("Render pass name is empty");
        for (auto const& p : passes_)
            if (p.name == pass.name) throw std::invalid_argument("Duplicate render pass: " + pass.name);
        for (auto r : pass.reads) validate_resource(r);
        for (auto r : pass.writes) validate_resource(r);
        for (auto dependency : pass.after)
            if (dependency >= passes_.size()) throw std::out_of_range("Pass dependency must precede its consumer");
        std::unordered_set<ResourceId> usages;
        for (auto const& usage : pass.usages) {
            validate_resource(usage.resource);
            if (usage.access == ResourceAccess::Undefined)
                throw std::invalid_argument("Undefined render graph resource access");
            if (!usages.insert(usage.resource).second)
                throw std::invalid_argument("Duplicate typed usage in render pass");
            const bool declared_read = std::find(pass.reads.begin(),pass.reads.end(),
                usage.resource)!=pass.reads.end();
            const bool declared_write = std::find(pass.writes.begin(),pass.writes.end(),
                usage.resource)!=pass.writes.end();
            const bool write_access = usage.access==ResourceAccess::ColorAttachment||
                usage.access==ResourceAccess::DepthWrite||
                usage.access==ResourceAccess::CopyDestination;
            if (!(declared_write || declared_read) || (write_access && !declared_write))
                throw std::invalid_argument("Typed usage contradicts render pass reads/writes");
        }
        passes_.push_back(std::move(pass));
        return static_cast<PassId>(passes_.size() - 1);
    }

    RenderGraphPlan compile() const {
        struct ResourceState {
            ResourceAccess access;
            bool written = false;
            bool previous_write = false;
        };
        RenderGraphPlan plan;
        plan.passes.reserve(passes_.size());
        plan.lifetimes.resize(resources_.size(), {-1,-1});
        std::vector<ResourceState> states;
        states.reserve(resources_.size());
        for (auto const& resource : resources_)
            states.push_back({resource.initial_access});

        // add_pass only accepts earlier explicit dependencies, and resource
        // hazards also point to earlier passes. Smallest-ready-first therefore
        // always produced declaration order, even for independent passes.
        for (PassId id = 0; id < passes_.size(); ++id) {
            auto const& pass = passes_[id];
            // Validate reads against preceding passes, including resources
            // that this pass also writes.
            for (auto resource : pass.reads) {
                if (!states[resource].written && !resources_[resource].imported)
                    throw std::logic_error("Read before first write: " + resources_[resource].name);
            }

            ScheduledPass compiled{id, {}, {}};
            const auto scheduled = static_cast<int>(id);
            auto record_use = [&](ResourceId resource, bool write) {
                auto& life = plan.lifetimes[resource];
                if (life.second == scheduled)
                    return;
                auto& state = states[resource];
                if (life.first == -1)
                    life.first = scheduled;
                else if (state.previous_write || write)
                    compiled.barriers.push_back(resource);
                life.second = scheduled;
                state.previous_write = write;
                state.written |= write;
            };
            // A read/write resource has a single lifetime update and one
            // barrier. Process writes first so the next pass sees a write.
            for (auto resource : pass.writes) record_use(resource, true);
            for (auto resource : pass.reads) record_use(resource, false);
            std::sort(compiled.barriers.begin(), compiled.barriers.end());
            for (auto const& usage : pass.usages) {
                auto& state = states[usage.resource].access;
                if (state != usage.access) {
                    compiled.transitions.push_back({usage.resource, state, usage.access});
                    state = usage.access;
                }
            }
            plan.passes.push_back(std::move(compiled));
        }
        return plan;
    }
    std::vector<Pass> const& passes() const noexcept { return passes_; }
    std::vector<Resource> const& resources() const noexcept { return resources_; }
private:
    void validate_resource(ResourceId id) const {
        if (id >= resources_.size()) throw std::out_of_range("Unknown render resource");
    }
    std::vector<Resource> resources_;
    std::vector<Pass> passes_;
};
} // namespace northstarcpp::render
