// Material Graph v1 serialized as XML typed value nodes in YMAT files.
// JSON is accepted/emitted only in memory to preserve the existing HLSL
// graph compiler's interface. No JSON text is persisted inside YMAT.
#include "material_graph_xml.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp::shared_assets::detail {
namespace {
using Tree=boost::property_tree::ptree;
struct Value {
    enum class Kind {Null,Boolean,Number,String,Object,Array};
    Kind kind=Kind::Null;
    std::string content;
    std::vector<std::pair<std::string,Value>> children;
};
void unicode(std::string& output,unsigned scalar) {
    if(scalar<=0x7f)output.push_back(char(scalar));
    else if(scalar<=0x7ff) {
        output.push_back(char(0xc0|(scalar>>6)));
        output.push_back(char(0x80|(scalar&63)));
    }else if(scalar<=0xffff) {
        output.push_back(char(0xe0|(scalar>>12)));
        output.push_back(char(0x80|((scalar>>6)&63)));
        output.push_back(char(0x80|(scalar&63)));
    }else {
        output.push_back(char(0xf0|(scalar>>18)));
        output.push_back(char(0x80|((scalar>>12)&63)));
        output.push_back(char(0x80|((scalar>>6)&63)));
        output.push_back(char(0x80|(scalar&63)));
    }
}
struct JsonReader {
    std::string const& source;
    std::size_t pos=0;
    unsigned depth=0;
    std::size_t node_count=0;
    void spaces() {
        while(pos<source.size() && (source[pos]==' '||source[pos]=='\r'||
              source[pos]=='\n'||source[pos]=='\t'))++pos;
    }
    bool match(char c) {
        spaces();
        if(pos<source.size()&&source[pos]==c){++pos;return true;}
        return false;
    }
    unsigned hex4() {
        unsigned v=0;
        for(int i=0;i<4;++i) {
            if(pos>=source.size())
                throw std::invalid_argument("Invalid Unicode escape in graph");
            char c=source[pos++];
            int n=c>='0'&&c<='9'?c-'0':
                  c>='a'&&c<='f'?c-'a'+10:
                  c>='A'&&c<='F'?c-'A'+10:-1;
            if(n<0)throw std::invalid_argument("Invalid XML graph Unicode escape");
            v=(v<<4)|unsigned(n);
        }
        return v;
    }
    std::string quoted() {
        if(!match('"'))
            throw std::invalid_argument("Expected JSON string for XML Graph");
        std::string out;
        while(pos<source.size()) {
            unsigned char c=static_cast<unsigned char>(source[pos++]);
            if(c=='"')return out;
            if(c=='\\') {
                if(pos>=source.size())
                    throw std::invalid_argument("Truncated JSON Graph escape");
                c=static_cast<unsigned char>(source[pos++]);
                switch(c) {
                    case '"':out.push_back('"');break;
                    case '\\':out.push_back('\\');break;
                    case '/':out.push_back('/');break;
                    case 'b':out.push_back('\b');break;
                    case 'f':out.push_back('\f');break;
                    case 'n':out.push_back('\n');break;
                    case 'r':out.push_back('\r');break;
                    case 't':out.push_back('\t');break;
                    case 'u': {
                        unsigned u=hex4();
                        if(u>=0xd800&&u<=0xdbff) {
                            if(pos+2>source.size()||source.substr(pos,2)!="\\u")
                                throw std::invalid_argument("Unpaired Unicode surrogate");
                            pos+=2;
                            unsigned lo=hex4();
                            if(lo<0xdc00||lo>0xdfff)
                                throw std::invalid_argument("Invalid Unicode surrogate pair");
                            u=0x10000+((u-0xd800)<<10)+(lo-0xdc00);
                        }else if(u>=0xdc00&&u<=0xdfff)
                            throw std::invalid_argument("Unpaired low surrogate");
                        unicode(out,u);
                        break;
                    }
                    default:throw std::invalid_argument("Invalid JSON Graph escape");
                }
            }else {
                if(c<0x20)
                    throw std::invalid_argument("Invalid JSON Graph control character");
                out.push_back(char(c));
            }
        }
        throw std::invalid_argument("Unterminated JSON Graph string");
    }
    Value value() {
        spaces();
        if(pos>=source.size()||source.size()>512*1024||
           ++node_count>32768||depth>=96)
            throw std::invalid_argument("Material Graph JSON exceeds structural limits");
        ++depth;
        struct Depth {unsigned& d;~Depth(){--d;}} leave{depth};
        Value out;
        char c=source[pos];
        if(c=='{') {
            out.kind=Value::Kind::Object;
            ++pos;
            if(match('}'))return out;
            do {
                auto key=quoted();
                if(!match(':'))
                    throw std::invalid_argument("Material Graph object missing ':'");
                for(auto const& field:out.children)
                    if(field.first==key)
                        throw std::invalid_argument("Duplicate Material Graph object key");
                out.children.emplace_back(std::move(key),value());
                if(match('}'))return out;
                if(!match(','))
                    throw std::invalid_argument("Material Graph object missing ','");
            }while(true);
        }
        if(c=='[') {
            out.kind=Value::Kind::Array;
            ++pos;
            if(match(']'))return out;
            do {
                out.children.emplace_back("",value());
                if(match(']'))return out;
                if(!match(','))
                    throw std::invalid_argument("Material Graph array missing ','");
            }while(true);
        }
        if(c=='"') {
            out.kind=Value::Kind::String;
            out.content=quoted();
            return out;
        }
        if(source.compare(pos,4,"true")==0) {
            out.kind=Value::Kind::Boolean;out.content="true";pos+=4;return out;
        }
        if(source.compare(pos,5,"false")==0) {
            out.kind=Value::Kind::Boolean;out.content="false";pos+=5;return out;
        }
        if(source.compare(pos,4,"null")==0) {
            out.kind=Value::Kind::Null;pos+=4;return out;
        }
        auto begin=pos;
        if(source[pos]=='-')++pos;
        if(pos>=source.size())
            throw std::invalid_argument("Incomplete Material Graph number");
        if(source[pos]=='0')++pos;
        else {
            if(source[pos]<'1'||source[pos]>'9')
                throw std::invalid_argument("Invalid Material Graph JSON token");
            while(pos<source.size()&&std::isdigit(
                static_cast<unsigned char>(source[pos])))++pos;
        }
        if(pos<source.size()&&source[pos]=='.') {
            ++pos;
            if(pos>=source.size()||!std::isdigit(
                static_cast<unsigned char>(source[pos])))
                throw std::invalid_argument("Invalid Material Graph number fraction");
            while(pos<source.size()&&std::isdigit(
                static_cast<unsigned char>(source[pos])))++pos;
        }
        if(pos<source.size()&&(source[pos]=='e'||source[pos]=='E')) {
            ++pos;
            if(pos<source.size()&&(source[pos]=='-'||source[pos]=='+'))++pos;
            if(pos>=source.size()||!std::isdigit(
                static_cast<unsigned char>(source[pos])))
                throw std::invalid_argument("Invalid Material Graph number exponent");
            while(pos<source.size()&&std::isdigit(
                static_cast<unsigned char>(source[pos])))++pos;
        }
        out.kind=Value::Kind::Number;
        out.content=source.substr(begin,pos-begin);
        return out;
    }
};
char const* tag(Value::Kind kind) {
    switch(kind) {
    case Value::Kind::Null:return "null";
    case Value::Kind::Boolean:return "bool";
    case Value::Kind::Number:return "number";
    case Value::Kind::String:return "string";
    case Value::Kind::Object:return "object";
    case Value::Kind::Array:return "array";
    }
    return "null";
}
Tree emit(Value const& v) {
    Tree tree;
    tree.put("<xmlattr>.type",tag(v.kind));
    if(v.kind==Value::Kind::Array||v.kind==Value::Kind::Object) {
        for(auto const& [key,child]:v.children) {
            auto value=emit(child);
            if(v.kind==Value::Kind::Object)
                value.put("<xmlattr>.name",key);
            tree.add_child("Value",value);
        }
    }else tree.put_value(v.content);
    return tree;
}
std::string escaped(std::string const& text) {
    std::ostringstream out;out<<'"';
    static char const* hex="0123456789abcdef";
    for(unsigned char c:text) {
        switch(c) {
            case '"':out<<"\\\"";break;
            case '\\':out<<"\\\\";break;
            case '\n':out<<"\\n";break;
            case '\r':out<<"\\r";break;
            case '\t':out<<"\\t";break;
            case '\b':out<<"\\b";break;
            case '\f':out<<"\\f";break;
            default:
                if(c<32)out<<"\\u00"<<hex[c>>4]<<hex[c&15];
                else out<<char(c);
        }
    }
    out<<'"';return out.str();
}
std::string parse_xml(Tree const& tree,unsigned depth=0) {
    if(depth>96)
        throw std::invalid_argument("XML Material Graph nesting too deep");
    auto type=tree.get<std::string>("<xmlattr>.type");
    if(type=="null")return "null";
    if(type=="string")return escaped(tree.get_value<std::string>());
    if(type=="bool") {
        auto value=tree.get_value<std::string>();
        if(value!="true" && value!="false")
            throw std::invalid_argument("Invalid XML Material Graph boolean");
        return value;
    }
    if(type=="number") {
        auto const raw=tree.get_value<std::string>();
        JsonReader verify{raw};
        auto number=verify.value();verify.spaces();
        if(number.kind!=Value::Kind::Number||verify.pos!=raw.size())
            throw std::invalid_argument("Invalid XML Material Graph number");
        return raw;
    }
    if(type!="array"&&type!="object")
        throw std::invalid_argument("Unsupported XML Material Graph value type");
    std::ostringstream result;
    result<<(type=="object"?'{':'[');
    std::set<std::string> names;
    bool first=true;
    for(auto const& [key,node]:tree) {
        if(key=="<xmlattr>")continue;
        if(key!="Value")
            throw std::invalid_argument("Unexpected XML Material Graph child");
        if(!first)result<<',';
        first=false;
        if(type=="object") {
            auto name=node.get<std::string>("<xmlattr>.name");
            if(!names.insert(name).second)
                throw std::invalid_argument("Duplicate XML Material Graph member");
            result<<escaped(name)<<':';
        }
        result<<parse_xml(node,depth+1);
    }
    result<<(type=="object"?'}':']');
    return result.str();
}
}
boost::property_tree::ptree typed_xml_value_from_json(
    std::string const& source) {
    JsonReader parser{source};
    auto data=parser.value();
    parser.spaces();
    if(parser.pos!=source.size())
        throw std::invalid_argument("Invalid JSON value for XML serialization");
    return emit(data);
}
boost::property_tree::ptree graph_xml_value_from_json(
    std::string const& source) {
    auto data=typed_xml_value_from_json(source);
    if(data.get<std::string>("<xmlattr>.type")!="object")
        throw std::invalid_argument("Material Graph must be an object");
    return data;
}
std::string typed_material_graph_from_json_library(
    std::string const& source,std::size_t material_index) {
    JsonReader parser{source};
    auto document=parser.value();parser.spaces();
    if(document.kind!=Value::Kind::Object ||
       parser.pos!=source.size())
        throw std::invalid_argument("Invalid imported material JSON library");
    for(auto const& [name,field]:document.children) {
        if(name!="materials")continue;
        if(field.kind!=Value::Kind::Array ||
           material_index>=field.children.size())
            throw std::invalid_argument("Missing imported material record");
        auto const& material=field.children[material_index].second;
        if(material.kind!=Value::Kind::Object)
            throw std::invalid_argument("Invalid imported material record");
        for(auto const& [key,value]:material.children) {
            if(key!="graph")continue;
            if(value.kind!=Value::Kind::Object)
                throw std::invalid_argument("Imported Material Graph is not an object");
            return parse_xml(emit(value));
        }
        return {};
    }
    throw std::invalid_argument("Missing legacy materials array");
}
std::string graph_json_from_xml_value(Tree const& node) {
    auto source=parse_xml(node);
    JsonReader verify{source};
    auto graph=verify.value();verify.spaces();
    if(graph.kind!=Value::Kind::Object||verify.pos!=source.size())
        throw std::invalid_argument("Material XML Graph must contain an object");
    return source;
}
} // namespace northstarcpp::shared_assets::detail
