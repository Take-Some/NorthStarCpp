#include <northstarcpp/perlin.hpp>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    std::string_view const mode = argv[1];
    try {
        northstarcpp::PerlinNoise perlin;
        if (mode == "builtin")
            return perlin.provider_name() == "Improved Perlin" && perlin.sample2(0, 0) == 0.f ? 0 : 1;
        if (mode == "external")
            return perlin.provider_name() == "Fixture Perlin" && perlin.sample2(0, 0) == 0.375f ? 0 : 1;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        if (mode == "reject" && std::string_view(error.what()).find("engine.perlin") != std::string_view::npos)
            return 0;
    }
    return 1;
}
