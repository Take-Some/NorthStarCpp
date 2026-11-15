// Canonical plugin selection identity shared by Project Browser and host.
// The on-wire NorthStarModule provider descriptor remains unchanged.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace northstarcpp {
inline std::string canonical_provider_id(std::string value,
                                         std::string const& capability={}) {
    for(auto& ch:value)
        ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if(value=="direct3d 11")return "d3d11";
    if(value=="direct3d 12")return "d3d12";
    if(value=="directory vfs" && capability=="engine.assets")return "directory";
    if(value=="win32/xinput" && capability=="engine.input")return "win32";
    if(value=="northstar geometry" && capability=="engine.geometry")return "geometry";
    if(value=="improved perlin" && capability=="engine.perlin")return "improved";
    if(value=="v8" && capability=="engine.scripting")return "v8_sidecar";
    // Preserve stable ASCII identifiers. External providers may use display
    // labels with spaces; normalize separators to a persistent selection ID.
    std::string normalized;
    for(unsigned char ch:value) {
        if((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')) {
            normalized.push_back(static_cast<char>(ch));
        } else if(ch=='_'||ch=='-'||ch==' '||ch=='/') {
            if(!normalized.empty()&&normalized.back()!='_')
                normalized.push_back('_');
        } else {
            return {}; // Unsupported/unsafe provider identifier.
        }
    }
    while(!normalized.empty()&&normalized.back()=='_')normalized.pop_back();
    return normalized;
}
}
