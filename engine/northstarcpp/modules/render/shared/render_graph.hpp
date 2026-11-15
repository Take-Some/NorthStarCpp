#pragma once
// NorthStarCpp render pass scheduler. Backend-neutral; no GPU API objects cross the plugin ABI.
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
        const auto count = passes_.size();
        std::vector<std::unordered_set<PassId>> dependencies(count);
        std::vector<int> last_writer(resources_.size(), -1);
        std::vector<std::vector<PassId>> readers(resources_.size());
        for (PassId i = 0; i < count; ++i) {
            auto const& pass = passes_[i];
            for (PassId dep : pass.after) dependencies[i].insert(dep);
            for (ResourceId r : pass.reads) {
                if (last_writer[r] < 0 && !resources_[r].imported)
                    throw std::logic_error("Read before first write: " + resources_[r].name);
                if (last_writer[r] >= 0) dependencies[i].insert(static_cast<PassId>(last_writer[r]));
                readers[r].push_back(i);
            }
            for (ResourceId r : pass.writes) {
                if (last_writer[r] >= 0) dependencies[i].insert(static_cast<PassId>(last_writer[r]));
                for (PassId reader : readers[r])
                    if (reader != i) dependencies[i].insert(reader);
                readers[r].clear();
                last_writer[r] = static_cast<int>(i);
            }
            dependencies[i].erase(i);
        }

        RenderGraphPlan plan;
        plan.lifetimes.resize(resources_.size(), {-1,-1});
        std::vector<ResourceAccess> access_state(resources_.size(), ResourceAccess::Undefined);
        for(std::size_t i=0;i<resources_.size();++i)
            access_state[i]=resources_[i].initial_access;
        std::vector<bool> visited(count, false);
        for (std::size_t scheduled = 0; scheduled < count; ++scheduled) {
            PassId next = static_cast<PassId>(count);
            for (PassId i = 0; i < count; ++i) {
                if (visited[i]) continue;
                bool ready = true;
                for (auto dep : dependencies[i])
                    if (!visited[dep]) { ready = false; break; }
                if (ready) { next = i; break; }
            }
            if (next == count) throw std::logic_error("Render graph dependency cycle");
            visited[next] = true;
            ScheduledPass compiled{next, {}};
            auto const& pass = passes_[next];
            std::unordered_set<ResourceId> touched;
            for (auto r : pass.reads) touched.insert(r);
            for (auto r : pass.writes) touched.insert(r);
            for (auto r : touched) {
                auto& life = plan.lifetimes[r];
                if (life.first == -1) life.first = static_cast<int>(scheduled);
                life.second = static_cast<int>(scheduled);
                // Backend maps these hazards into D3D12 transitions or Vulkan barriers.
                if (scheduled > 0) {
                    for (auto const& previous : plan.passes) {
                        auto const& prior = passes_[previous.id];
                        const bool prior_write = std::find(prior.writes.begin(), prior.writes.end(), r) != prior.writes.end();
                        const bool current_write = std::find(pass.writes.begin(), pass.writes.end(), r) != pass.writes.end();
                        const bool prior_read = std::find(prior.reads.begin(), prior.reads.end(), r) != prior.reads.end();
                        if (prior_write || (prior_read && current_write)) {
                            compiled.barriers.push_back(r); break;
                        }
                    }
                }
            }
            std::sort(compiled.barriers.begin(), compiled.barriers.end());
            for (auto const& usage : pass.usages) {
                auto& state=access_state[usage.resource];
                if (state!=usage.access) {
                    compiled.transitions.push_back({usage.resource,state,usage.access});
                    state=usage.access;
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
