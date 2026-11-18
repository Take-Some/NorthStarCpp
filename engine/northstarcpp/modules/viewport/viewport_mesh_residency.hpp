// NorthStarCpp: reusable, non-owning Viewport mesh residency hash table.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace northstarcpp::viewport_detail {

class ActiveMeshRefs final {
public:
    // Keys borrow the current Scene's asset strings. Only query within draw().
    void begin(std::size_t possible_entries) {
        clear();
        if(possible_entries>names_.capacity())
            names_.reserve(possible_entries);
        auto capacity=std::size_t{16};
        while(capacity < possible_entries*2 && capacity <= (std::size_t(-1)/2))
            capacity*=2;
        if(slots_.size()<capacity)
            rehash(capacity);
    }

    void include(std::string_view asset) {
        auto const marker=asset.find('@');
        auto const filename=asset.substr(0,marker);
        if(filename.size()<4)return;
        auto const suffix=filename.substr(filename.size()-4);
        if(suffix!=".obj" && suffix!=".ydd" && suffix!=".ydr")
            return;
        if((names_.size()+1)*2 > slots_.size())
            rehash(slots_.empty()?16:slots_.size()*2);
        auto const mask=slots_.size()-1;
        auto index=std::hash<std::string_view>{}(asset)&mask;
        while(slots_[index].generation==generation_) {
            if(names_[slots_[index].key_index]==asset)
                return;
            index=(index+1)&mask;
        }
        auto& slot=slots_[index];
        // Set the slot only after push_back succeeds.
        names_.push_back(asset);
        slot.key_index=names_.size()-1;
        slot.generation=generation_;
    }

    // The open-addressed table is built incrementally: no sort needed.
    void finish() noexcept {}

    bool contains(std::string_view asset) const noexcept {
        if(names_.empty() || slots_.empty())return false;
        auto const mask=slots_.size()-1;
        auto index=std::hash<std::string_view>{}(asset)&mask;
        while(slots_[index].generation==generation_) {
            if(names_[slots_[index].key_index]==asset)
                return true;
            index=(index+1)&mask;
        }
        return false;
    }

    // Invalidate all borrowed names in O(1); retain the allocated slots.
    void clear() noexcept {
        names_.clear();
        if(++generation_==0) {
            for(auto& slot:slots_)slot.generation=0;
            generation_=1;
        }
    }

    std::size_t size() const noexcept { return names_.size(); }
    std::size_t capacity() const noexcept { return names_.capacity(); }

private:
    struct Slot {
        std::size_t key_index=0;
        std::uint32_t generation=0;
    };
    void rehash(std::size_t capacity) {
        std::vector<Slot> slots(capacity);
        auto const mask=capacity-1;
        for(std::size_t key=0;key<names_.size();++key) {
            auto index=std::hash<std::string_view>{}(names_[key])&mask;
            while(slots[index].generation==generation_)
                index=(index+1)&mask;
            slots[index]={key,generation_};
        }
        slots_=std::move(slots);
    }

    std::vector<std::string_view> names_;
    std::vector<Slot> slots_;
    std::uint32_t generation_=1;
};

} // namespace northstarcpp::viewport_detail
