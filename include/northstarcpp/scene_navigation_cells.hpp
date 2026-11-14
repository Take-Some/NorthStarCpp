// Coarse streaming-cell navigation graph. Not a NavMesh or an obstacle solver.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace northstarcpp {
class SceneWorld;

// Reconciles coarse cell connectivity with verified NEF8 payload readiness.
// Actual polygon NavMesh, portals/doors, agent radii and dynamic avoidance
// require a future engine.navigation provider; never treat this as walkability.
class SceneNavigationCells final {
public:
    explicit SceneNavigationCells(Scene const& authored);
    void sync(SceneWorld const& world);
    bool resident(std::string const& cell) const;
    std::size_t resident_count() const noexcept {return resident_.size();}
    // Returns a coarse cell sequence, or empty when no ready route exists.
    std::vector<std::string> route(std::string const& from,
                                   std::string const& to) const;
private:
    std::vector<SceneCell> authored_;
    std::unordered_map<std::string,SceneCell> resident_;
};
} // namespace northstarcpp
