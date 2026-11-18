#include <northstarcpp/animation_graph.hpp>
#include <northstarcpp/assets.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace northstarcpp {
AsyncYcdClipLoader::AsyncYcdClipLoader(AssetStore const& assets,
    std::shared_ptr<Skeleton const> skeleton,std::size_t max_decoders)
    : assets_(&assets),rig_(std::move(skeleton)),
      max_decoders_(std::clamp(max_decoders,std::size_t{1},std::size_t{4})) {
    if(!rig_||rig_->joints.empty())
        throw std::invalid_argument("YCD cache requires a live skeleton");
}
AsyncYcdClipLoader::~AsyncYcdClipLoader()=default;
void AsyncYcdClipLoader::request(std::string reference,AssetPriority priority) {
    if(entries_.count(reference))return;
    auto pos=reference.rfind('@');
    if(pos==std::string::npos||pos<4 || pos+1==reference.size() ||
        reference.size()>512 || reference.substr(pos-4,4)!=".ycd" ||
        reference.find("..")!=std::string::npos ||
        reference.find('\\')!=std::string::npos ||
        reference.find(':')!=std::string::npos)
        throw std::invalid_argument("Unsafe asynchronous YCD clip reference");
    Entry task;
    task.path=reference.substr(0,pos);
    task.name=reference.substr(pos+1);
    task.request=assets_->request_bytes(task.path,priority);
    entries_.emplace(std::move(reference),std::move(task));
}
void AsyncYcdClipLoader::poll() {
    std::size_t running=0;
    for(auto& [reference,entry]:entries_) {
        if(entry.ready||entry.failed)continue;
        if(entry.decoding.valid()) {
            if(entry.decoding.wait_for(std::chrono::seconds(0))==
                std::future_status::ready) {
                try {entry.ready=entry.decoding.get();}
                catch(std::exception const& err) {
                    entry.error=err.what();entry.failed=true;
                } catch(...) {
                    entry.error="Unknown YCD decode failure";entry.failed=true;
                }
            } else ++running;
        }
    }
    for(auto& [reference,entry]:entries_) {
        if(running>=max_decoders_)break;
        if(entry.ready||entry.failed||entry.decoding.valid()||!entry.request)continue;
        if(!entry.request.ready())continue;
        try {
            if(entry.request.stale())
                throw std::runtime_error("YCD request cancelled by VFS remount");
            auto bytes=entry.request.get();
            if(!bytes || bytes->size()>64ull*1024ull*1024ull)
                throw std::runtime_error("Invalid or oversized YCD bytes");
            auto skeleton=rig_;
            auto name=entry.name;
            // Capture a pinned blob and skeleton; the AssetStore itself is
            // never touched by the CPU decoder worker.
            entry.decoding=std::async(std::launch::async,
                [bytes=std::move(bytes),skeleton=std::move(skeleton),
                 name=std::move(name)]() -> std::shared_ptr<SkeletalClip const> {
                    auto clips=load_skeletal_clips_from_bytes(*bytes,{name},*skeleton);
                    if(clips.size()!=1)
                        throw std::runtime_error("Requested YCD clip missing");
                    return std::make_shared<SkeletalClip const>(std::move(clips[0]));
                });
            ++running;
        } catch(std::exception const& err) {
            entry.error=err.what();entry.failed=true;
        }
    }
}
std::shared_ptr<SkeletalClip const> AsyncYcdClipLoader::find(
    std::string const& reference) const {
    auto it=entries_.find(reference);
    return it==entries_.end()?nullptr:it->second.ready;
}
std::string AsyncYcdClipLoader::error(std::string const& reference) const {
    auto it=entries_.find(reference);
    return it==entries_.end()?std::string{}:it->second.error;
}
std::size_t AsyncYcdClipLoader::pending() const noexcept {
    std::size_t count=0;
    for(auto const& entry:entries_)
        if(!entry.second.ready&&!entry.second.failed)++count;
    return count;
}
}
