// NorthStarCpp Content Browser
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <cstdint>
#include "editor_asset_thumbnails.hpp"
#include <optional>
#include <string>
#include <vector>

namespace northstarcpp {

class EditorContentBrowser {
public:
    enum ControlId { tree_id = 650, files_id, search_id, category_id,
                     up_id, reload_id };
    explicit EditorContentBrowser(HWND parent);
    ~EditorContentBrowser();
    void layout(int x, int y, int width, int height);
    void set_visible(bool visible);
    void set_project(std::filesystem::path scene_file);
    void refresh();
    void poll_thumbnails();
    bool on_command(int id, int notification);
    bool on_notify(NMHDR const* header);
    std::optional<std::string> selected_asset() const;
    void show_selected_in_explorer() const;
    bool available() const;
    bool dragging() const noexcept { return dragging_; }
    std::optional<std::string> finish_drag();
    void cancel_drag() noexcept;
private:
    struct Entry {
        std::filesystem::path relative;
        bool folder = false;
        std::wstring label;
    };
    HWND parent_{}, tree_{}, files_{}, search_{}, categories_{};
    HWND path_{}, caption_{}, up_{}, reload_{};
    std::filesystem::path root_, current_;
    std::vector<Entry> entries_;
    std::vector<std::filesystem::path> folders_;
    bool populating_tree_ = false;
    HIMAGELIST previews_=nullptr;
    std::map<std::wstring,int> icon_indices_;
    struct ThumbnailJob {
        std::uint64_t generation=0;
        std::filesystem::path root,relative;
    };
    struct ThumbnailResult {
        std::uint64_t generation=0;
        std::filesystem::path relative;
        editor_tools::Thumbnail image;
    };
    std::mutex preview_mutex_;
    std::condition_variable preview_wake_;
    std::deque<ThumbnailJob> preview_jobs_;
    std::deque<ThumbnailResult> preview_results_;
    std::thread preview_worker_;
    std::uint64_t generation_=0;
    bool stop_worker_=false;
    void thumbnail_worker();
    void queue_thumbnails();
    int fallback_icon(std::filesystem::path const& file,bool folder);
    bool dragging_ = false;
    std::optional<std::string> drag_asset_;
    void build_tree();
    void refresh_files();
    bool accepts(std::filesystem::path const& path) const;
    bool within_project(std::filesystem::path const& path) const;
    std::optional<std::filesystem::path> selected_relative() const;
    void enter(std::filesystem::path relative);
};

} // namespace northstarcpp
