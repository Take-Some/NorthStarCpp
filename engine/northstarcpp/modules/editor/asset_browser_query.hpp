#pragma once
// Bounded Content Browser expression language. Parser compiled once per scan,
// predicate evaluated against indexed metadata: never opens asset payloads.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <functional>
#include <vector>
#include <algorithm>
#include <cmath>

namespace northstarcpp::editor {
class AssetQuery {
public:
    using Predicate=std::function<bool(AssetRecord const&)>;
    explicit AssetQuery(QString query) {
        if(query.size()>1024) {error_="Search must be at most 1024 characters";return;}
        if(query.trimmed().isEmpty()) {predicate_=[](auto const&){return true;};return;}
        tokenize(query);
        if(!error_.isEmpty())return;
        index_=0;
        predicate_=parseOr(0);
        if(error_.isEmpty() && index_!=tokens_.size())
            error_="Unexpected search token: "+tokens_[index_];
    }
    bool valid() const noexcept {return error_.isEmpty();}
    QString error() const {return error_;}
    bool matches(AssetRecord const& entry) const {
        return valid() && (!predicate_ || predicate_(entry));
    }
private:
    QStringList tokens_;
    QString error_;
    qsizetype index_=0;
    Predicate predicate_;
    bool consume(QString const& token) {
        if(index_<tokens_.size() &&
           tokens_[index_].compare(token,Qt::CaseInsensitive)==0) {
            ++index_;
            return true;
        }
        return false;
    }
    bool atEnd() const {return index_>=tokens_.size();}
    void tokenize(QString const& q) {
        QString word;
        bool quote=false,escape=false;
        auto flush=[&]{
            if(!word.isEmpty()) {tokens_.append(word);word.clear();}
        };
        for(QChar c:q) {
            if(escape) {word+=c;escape=false;continue;}
            if(c=='\\' && quote) {escape=true;continue;}
            if(c=='"') {quote=!quote;continue;}
            if(!quote && (c=='(' || c==')' || c=='|' || c=='!')) {
                flush();tokens_.append(c);continue;
            }
            if(!quote && c.isSpace()) {flush();continue;}
            word+=c;
            if(word.size()>256) {
                error_="Search term exceeds 256 characters";return;
            }
        }
        if(quote || escape) {error_="Unterminated search quotes";return;}
        flush();
        if(tokens_.size()>128)error_="Too many search tokens";
    }
    Predicate parseOr(int depth) {
        auto left=parseAnd(depth);
        while(error_.isEmpty() && (consume("OR")||consume("|"))) {
            auto right=parseAnd(depth);
            left=[left=std::move(left),right=std::move(right)](auto const& r) {
                return left(r)||right(r);
            };
        }
        return left;
    }
    Predicate parseAnd(int depth) {
        auto left=parseUnary(depth);
        while(error_.isEmpty() && !atEnd() &&
              tokens_[index_]!=")" &&
              tokens_[index_].compare("OR",Qt::CaseInsensitive)!=0 &&
              tokens_[index_]!="|") {
            consume("AND");consume("&");
            if(atEnd()) {error_="Expected term after AND";break;}
            auto right=parseUnary(depth);
            left=[left=std::move(left),right=std::move(right)](auto const& r) {
                return left(r)&&right(r);
            };
        }
        return left;
    }
    Predicate parseUnary(int depth) {
        if(depth>16) {error_="Search nesting too deep";return [](auto const&){return false;};}
        if(atEnd()) {error_="Expected search term";return [](auto const&){return false;};}
        if(consume("NOT") || consume("!") || consume("-")) {
            auto inner=parseUnary(depth+1);
            return [inner=std::move(inner)](auto const& r){return !inner(r);};
        }
        if(consume("(")) {
            auto inner=parseOr(depth+1);
            if(!consume(")"))error_="Expected closing parenthesis";
            return inner;
        }
        auto text=tokens_[index_++];
        if(text.startsWith('-') && text.size()>1) {
            auto inner=makeTerm(text.mid(1));
            return [inner=std::move(inner)](auto const& r){return !inner(r);};
        }
        return makeTerm(text);
    }
    Predicate makeTerm(QString term) {
        QString key,value;
        int colon=term.indexOf(':');
        if(colon>0) {key=term.left(colon).toLower();value=term.mid(colon+1);}
        else {
            static QRegularExpression sizeRe("^size([<>]=?|=)(.+)$",
                QRegularExpression::CaseInsensitiveOption);
            auto match=sizeRe.match(term);
            if(match.hasMatch()) {
                key="size";value=match.captured(1)+match.captured(2);
            }else value=term;
        }
        if(value.isEmpty()) {error_="Empty search predicate";return [](auto const&){return false;};}
        if(key=="size") {
            static QRegularExpression expr("^([<>]=?|=)?([0-9]+(?:\\.[0-9]+)?)(B|KB|MB|GB)?$",
                QRegularExpression::CaseInsensitiveOption);
            auto m=expr.match(value);
            if(!m.hasMatch()) {error_="Invalid size comparison";return [](auto const&){return false;};}
            bool ok=false;
            double quantity=m.captured(2).toDouble(&ok);
            QString unit=m.captured(3).toUpper();
            if(unit=="KB")quantity*=1024.;
            if(unit=="MB")quantity*=1024.*1024.;
            if(unit=="GB")quantity*=1024.*1024.*1024.;
            if(!ok || !std::isfinite(quantity)) {error_="Invalid size";return [](auto const&){return false;};}
            QString op=m.captured(1);
            return [quantity,op](AssetRecord const& a) {
                auto size=static_cast<double>(a.size);
                if(op==">")return size>quantity;
                if(op==">=")return size>=quantity;
                if(op=="<")return size<quantity;
                if(op=="<=")return size<=quantity;
                return size==quantity;
            };
        }
        if(key=="is") {
            value=value.toLower();
            if(value=="folder")return [](auto const& a){return a.directory;};
            if(value=="virtual")return [](auto const& a){return a.is_virtual_entry();};
            if(value=="asset")return [](auto const& a){return !a.directory;};
            if(value=="prefab")return [](auto const& a){return a.kind==AssetKind::prefab;};
            if(value=="invalid")return [](auto const& a) {
                return a.kind==AssetKind::prefab && !a.prefab_valid;
            };
            if(value=="container")return [](auto const& a){return a.is_list_file();};
            error_="Unknown is: predicate";return [](auto const&){return false;};
        }
        if(!key.isEmpty() && key!="name" && key!="path" && key!="type" &&
           key!="kind" && key!="ext") {
            error_="Unknown search field: "+key;return [](auto const&){return false;};
        }
        return [key,value](AssetRecord const& a) {
            QString field;
            if(key=="name")field=QString::fromStdString(a.filename);
            else if(key=="type"||key=="kind")
                field=QString::fromLatin1(kind_name(a.kind));
            else if(key=="ext")
                field=QString::fromStdString(
                    std::filesystem::path(a.filename).extension().u8string());
            else field=QString::fromStdString(a.virtual_path);
            if(key=="ext" && !value.startsWith('.'))
                field=field.mid(1);
            return field.contains(value,Qt::CaseInsensitive);
        };
    }
};
}
