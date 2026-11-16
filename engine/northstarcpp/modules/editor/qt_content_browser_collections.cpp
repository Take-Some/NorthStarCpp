#include "qt_content_browser.hpp"
#include "asset_browser_collections.hpp"
#include "asset_team_collections.hpp"
#include <QComboBox>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QListView>
#include <QMessageBox>
#include <QSignalBlocker>

namespace northstarcpp::editor {
void QtContentBrowser::refreshCollections() {
    QSignalBlocker blocker(collections_);
    QString const selected=active_collection_;
    collections_->clear();
    collections_->addItem("All Content",QString{});
    auto const named=asset_collections::load(root_);
    for(auto it=named.cbegin();it!=named.cend();++it)
        collections_->addItem(it.key(),it.key());
    if(!engine_selected_) {
        asset_collections::Items team;
        QString issue;
        if(asset_team_collections::load(root_,team,issue))
            for(auto it=team.cbegin();it!=team.cend();++it)
                collections_->addItem("Team: "+it.key(),"@team/"+it.key());
    }
    int index=collections_->findData(selected);
    if(index<0){active_collection_.clear();index=0;}
    collections_->setCurrentIndex(index);
}
void QtContentBrowser::setActiveCollection(QString const& name) {
    if(name.isEmpty()) {
        active_collection_.clear();
        collections_->setCurrentIndex(0);
        populateItems();
        return;
    }
    int index=collections_->findData(name);
    if(index<0)return;
    active_collection_=name;
    collections_->setCurrentIndex(index);
    populateItems();
}
QStringList QtContentBrowser::selectedPaths() const {
    QStringList paths;
    if(!grid_ || !grid_->selectionModel())return paths;
    for(auto const& index:grid_->selectionModel()->selectedRows()) {
        QString const path=itemPath(index);
        if(!path.isEmpty() && !paths.contains(path))paths.append(path);
    }
    // Current item may be displayed without an active selection after navigation.
    if(paths.isEmpty()) {
        auto path=selectedPath();
        if(!path.isEmpty())paths.append(path);
    }
    return paths;
}
void QtContentBrowser::createCollection() {
    if(root_.isEmpty())return;
    bool accepted=false;
    auto name=QInputDialog::getText(this,"New Asset Collection",
        "Collection name:",QLineEdit::Normal,{},&accepted).trimmed();
    if(!accepted)return;
    if(!asset_collections::validName(name)) {
        QMessageBox::warning(this,"Collection",
            "Use 1-64 letters, digits, spaces, hyphens or underscores.");
        return;
    }
    auto named=asset_collections::load(root_);
    if(named.contains(name) || named.size()>=128) {
        QMessageBox::warning(this,"Collection",
            "Collection name exists or the collection limit was reached.");
        return;
    }
    named.insert(name,{});
    for(auto const& path:selectedPaths())
        asset_collections::add(named,name,path);
    asset_collections::save(root_,named);
    refreshCollections();
    setActiveCollection(name);
}
void QtContentBrowser::createTeamCollection() {
    if(engine_selected_ || project_root_.isEmpty())return;
    bool accepted=false;
    auto name=QInputDialog::getText(this,"New Team Collection",
        "Name (saved in project .northstar metadata):",
        QLineEdit::Normal,{},&accepted).trimmed();
    if(!accepted)return;
    if(!asset_collections::validName(name)) {
        QMessageBox::warning(this,"Team Collection","Invalid name");return;
    }
    QString issue;
    asset_collections::Items named;
    if(!asset_team_collections::load(project_root_,named,issue)) {
        QMessageBox::warning(this,"Team Collection",issue);return;
    }
    if(named.contains(name) || named.size()>=128) {
        QMessageBox::warning(this,"Team Collection",
            "Name already exists or project collection limit reached");return;
    }
    named.insert(name,{});
    for(QString const& path:selectedPaths())
        asset_collections::add(named,name,path);
    if(!asset_team_collections::save(project_root_,named,issue)) {
        QMessageBox::warning(this,"Team Collection",issue);return;
    }
    refreshCollections();
    setActiveCollection("@team/"+name);
}
void QtContentBrowser::publishLocalCollection() {
    if(engine_selected_ || project_root_.isEmpty())return;
    auto local=asset_collections::load(root_);
    if(local.empty()) {
        QMessageBox::information(this,"Team Collections",
            "Create a local collection before publishing");return;
    }
    bool accepted=false;
    QString name=QInputDialog::getItem(this,"Publish Team Collection",
        "Local collection:",local.keys(),0,false,&accepted);
    if(!accepted || name.isEmpty())return;
    asset_collections::Items team;
    QString issue;
    if(!asset_team_collections::load(project_root_,team,issue)) {
        QMessageBox::warning(this,"Team Collections",issue);return;
    }
    if(team.contains(name) &&
       QMessageBox::question(this,"Replace Team Collection",
           "Replace the project-shared collection with your local list?",
           QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
        return;
    team.insert(name,local.value(name));
    if(!asset_team_collections::save(project_root_,team,issue)) {
        QMessageBox::warning(this,"Team Collections",issue);return;
    }
    refreshCollections();
    setActiveCollection("@team/"+name);
}
void QtContentBrowser::addSelectionToCollection(QString const& name) {
    if(root_.isEmpty())return;
    bool const team=name.startsWith("@team/");
    auto named=asset_collections::Items{};
    QString issue;
    if(team) {
        if(engine_selected_ || !asset_team_collections::load(root_,named,issue))return;
    } else named=asset_collections::load(root_);
    auto actual=team?name.mid(6):name;
    bool added=false;
    for(auto const& path:selectedPaths())
        added|=asset_collections::add(named,actual,path);
    if(added) {
        if(team) {
            if(!asset_team_collections::save(root_,named,issue))
                QMessageBox::warning(this,"Team Collection",issue);
        }else asset_collections::save(root_,named);
        if(active_collection_==name)populateItems();
    }
}
void QtContentBrowser::removeSelectionFromCollection(QString const& name) {
    if(root_.isEmpty())return;
    bool const team=name.startsWith("@team/");
    asset_collections::Items named;
    QString issue;
    if(team) {
        if(engine_selected_ || !asset_team_collections::load(root_,named,issue))return;
    }else named=asset_collections::load(root_);
    QString actual=team?name.mid(6):name;
    if(!named.contains(actual))return;
    bool changed=false;
    for(auto const& path:selectedPaths())
        changed|=named[actual].removeAll(path)>0;
    if(changed) {
        if(team) {
            if(!asset_team_collections::save(root_,named,issue))
                QMessageBox::warning(this,"Team Collection",issue);
        }else asset_collections::save(root_,named);
        if(active_collection_==name)populateItems();
    }
}
}
