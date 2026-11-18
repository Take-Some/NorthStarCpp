// Header-only regression: project sky is data, independent of asset names.
#include <northstarcpp/sky_project_config.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using northstarcpp::project_sky::parse;
template<class F> bool rejected(F&& callback) {
    try {callback();} catch(std::exception const&) {return true;}
    return false;
}
int main() {
    auto defaults=parse(R"({"engine":"NorthStarCpp"})");
    assert(!defaults.settings.enabled && !defaults.explicitly_disabled);
    assert(defaults.mode=="disabled" && defaults.drawable.empty());
    auto procedural=parse(R"({"sky":{"mode":"procedural","sun":false,"timeOfDay":9.5}})");
    assert(procedural.settings.enabled && !procedural.settings.sun);
    assert(procedural.settings.time_of_day==9.5);
    auto custom=parse(R"({"sky":{"mode":"drawable","drawable":"worlds/night.ydd@vault","clouds":false}})");
    assert(!custom.settings.enabled);
    assert(custom.drawable=="worlds/night.ydd@vault");
    auto hybrid=parse(R"({"sky":{"mode":"hybrid","drawable":"worlds/night.ydr","cloudCoverage":0.65,"fogDensity":0.045}})");
    assert(hybrid.settings.enabled && hybrid.drawable=="worlds/night.ydr");
    assert(hybrid.settings.fog_density==0.045);
    auto disabled=parse(R"({"sky":{"mode":"disabled","drawable":"worlds/night.ydd@vault"}})");
    assert(!disabled.settings.enabled && disabled.drawable.empty());
    assert(disabled.explicitly_disabled);
    assert(rejected([] {parse(R"({"sky":{"mode":"animated"}})");}));
    assert(rejected([] {parse(R"({"sky":{"mode":"drawable"}})");}));
    assert(rejected([] {parse(R"({"sky":{"mode":"hybrid","drawable":"../sky.ydd@v"}})");}));
    assert(rejected([] {parse(R"({"sky":{"mode":"hybrid","drawable":"worlds/sky.ydd"}})");}));
    assert(rejected([] {parse(R"({"sky":{"cloudCoverage":10.5}})");}));
    assert(rejected([] {parse(R"({"sky":{"fogDensity":0.5}})");}));
    std::ifstream input("Projects/AbbyPistolDemo/northstarcpp.project.json");
    assert(input.good());
    std::string manifest((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    auto project=parse(manifest);
    assert(project.mode=="hybrid" && project.settings.enabled);
    assert(!project.drawable.empty());
    assert(project.settings.cloud_coverage==0.72);
    assert(project.settings.fog_density==0.045);
    assert(parse(manifest,"main.nscn").settings.enabled);
    assert(!parse(manifest,"levels/indoor.nscn").settings.enabled);
    assert(northstarcpp::project_sky::scene_profile("levels/indoor.nscn")==
           "levels/indoor.nscn.sky.json");
    assert(northstarcpp::project_sky::scene_reference("Projects","other.nscn").empty());
    assert(rejected([] {parse(R"({"sky":42})");}));
    assert(rejected([] {parse(R"({"sky":[{"mode":"disabled"}]})");}));
    assert(parse(manifest,"main.nscn").settings.enabled);
    assert(!parse(manifest,"levels/indoor.nscn").settings.enabled);
    assert(northstarcpp::project_sky::scene_profile("levels/indoor.nscn")==
           "levels/indoor.nscn.sky.json");
    assert(northstarcpp::project_sky::scene_reference("Projects","other.nscn").empty());
    assert(rejected([] {parse(R"({"sky":42})");}));
    assert(rejected([] {parse(R"({"sky":[{"mode":"disabled"}]})");}));
    std::ifstream nightcity_sky("Projects/KaylaNightCity/main.nscn.sky.json");
    assert(nightcity_sky.good());
    std::string nightcity_json((std::istreambuf_iterator<char>(nightcity_sky)),
                               std::istreambuf_iterator<char>());
    auto nightcity=parse(nightcity_json);
    assert(nightcity.mode=="procedural" && nightcity.settings.enabled);
    assert(nightcity.settings.clouds && nightcity.settings.sun);
    assert(nightcity.settings.cloud_coverage>0.3);
    std::cout<<"PASS KaylaNightCity main.nscn authored procedural sky\n";
    std::cout<<"PASS sky.project: procedural/default/custom/hybrid/disabled, "
        "VFS validation, startup scene inheritance, Abby project manifest\n";
}
