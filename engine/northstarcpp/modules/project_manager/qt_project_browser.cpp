// NorthStarCpp Qt Project Browser
// Copyright (c) 2026 Kayla's Systems — Kayla Verner
// Qt 6 Widgets + Font Awesome 6 Free (see resources/fontawesome/LICENSE.txt).
#include <northstarcpp/project_catalog.hpp>
#include <northstarcpp/logger.hpp>

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ns = northstarcpp::projects;
namespace fs = std::filesystem;

namespace {
QString fontFamily;
constexpr char const* stylesheet = R"QSS(
QMainWindow, QWidget#root, QDialog { background:#f6f8fb; color:#202b39; }
QWidget#sidebar { background:#edf2f8; border-right:1px solid #dce4ef; }
QWidget#details { background:#fff; border-left:1px solid #dce4ef; }
QFrame#projectCard {
  background:#fff; border:1px solid #dce4ed; border-radius:12px;
}
QFrame#projectCard:hover { border:2px solid #329dcc; background:#f5fbff; }
QFrame#projectCard[selected="true"] { border:2px solid #167cac; background:#eff8fd; }
QFrame#cardBanner { border-radius:8px; background:#e7f1f8; }
QLabel#headline { color:#16283b; font-size:22px; font-weight:700; }
QLabel#section { color:#273b50; font-size:18px; font-weight:700; }
QLabel#cardTitle { color:#203246; font-size:15px; font-weight:700; }
QLabel#muted { color:#6f8296; font-size:12px; }
QLabel#brand { color:#182c45; font-size:19px; font-weight:800; }
QLabel#eyebrow { color:#1478a5; font-weight:700; }
QPushButton, QToolButton {
  padding:9px 13px; background:#fff; color:#233749;
  border:1px solid #d5dfe9; border-radius:8px;
}
QPushButton:hover, QToolButton:hover { background:#e8f3fc; border-color:#78b8db; }
QPushButton:disabled, QToolButton:disabled { color:#9ba8b8; background:#f3f6fa; }
QPushButton#primary {
  background:#197fae; border-color:#197fae; color:#fff; font-weight:700;
}
QPushButton#primary:hover { background:#126b98; }
QToolButton#nav { border:0; text-align:left; padding:13px 17px; background:transparent; }
QToolButton#nav:checked { background:#dbeaf6; color:#135c83; font-weight:700; }
QLineEdit, QComboBox {
 background:white; border:1px solid #d5dfe9; border-radius:8px;
 padding:9px; selection-background-color:#197fae;
}
QLineEdit:focus, QComboBox:focus { border:1px solid #168fc6; }
QScrollArea { border:0; background:transparent; }
QScrollArea QWidget#scrollContent { background:transparent; }
QMessageBox QLabel { min-width:220px; }
)QSS";

QString qpath(fs::path const& p) { return QString::fromStdWString(p.wstring()); }
fs::path fspath(QString const& s) { return fs::path(s.toStdWString()); }
QString qwide(std::wstring const& s) { return QString::fromStdWString(s); }
QString qs(std::string const& s) { return QString::fromUtf8(s.c_str()); }
void messageError(QWidget* parent, QString const& title, std::exception const& e)
{
    QMessageBox::critical(parent,title,QString::fromUtf8(e.what()));
}
QIcon awesome(char32_t glyph, QColor color = QColor("#197fae"), int size = 18)
{
    QPixmap pixmap(size*2,size*2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(color);
    QFont font(fontFamily);
    font.setPixelSize(size*2-2);
    painter.setFont(font);
    painter.drawText(pixmap.rect(),Qt::AlignCenter,QString(QChar(static_cast<ushort>(glyph))));
    painter.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}
QPushButton* actionButton(QString label, char32_t glyph, QWidget* owner, bool primary=false)
{
    auto* button = new QPushButton(awesome(glyph,primary?Qt::white:QColor("#197fae")),label,owner);
    if (primary) button->setObjectName("primary");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}
QString statusLabel(ns::Project const& project)
{
    return qwide(ns::status_text(project.status));
}
QLabel* makeLabel(QString text, QString const& id = {}, QWidget* owner=nullptr)
{
    auto* l=new QLabel(std::move(text),owner);
    if (!id.isEmpty()) l->setObjectName(id);
    return l;
}

class Card final : public QFrame {
public:
    Card(ns::Project const& project, bool selected, std::function<void(bool)> callback,
         QWidget* parent = nullptr)
        : QFrame(parent), callback_(std::move(callback)), path_(project.directory)
    {
        setObjectName("projectCard");
        setProperty("selected",selected);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumWidth(236);
        setMinimumHeight(195);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        auto* vertical=new QVBoxLayout(this);
        vertical->setContentsMargins(13,13,13,13);
        vertical->setSpacing(8);
        auto* banner=new QFrame(this);
        banner->setObjectName("cardBanner");
        banner->setFixedHeight(66);
        auto* bannerLayout=new QHBoxLayout(banner);
        auto* mark=new QLabel(banner);
        mark->setPixmap(awesome(project.template_id=="city"||project.template_id=="architecture"
                                    ?0xf1ad:0xf11b,QColor("#227dae"),27).pixmap(27,27));
        bannerLayout->addWidget(mark);
        bannerLayout->addStretch();
        auto* category=makeLabel(qwide(project.category),"eyebrow",banner);
        bannerLayout->addWidget(category);
        vertical->addWidget(banner);
        auto* name=makeLabel(qwide(project.name),"cardTitle",this);
        name->setWordWrap(true);
        vertical->addWidget(name);
        auto* footer=new QHBoxLayout;
        auto* state=makeLabel(statusLabel(project),"muted",this);
        footer->addWidget(state);
        footer->addStretch();
        if (project.favorite) {
            auto* star=new QLabel(this);
            star->setPixmap(awesome(0xf005,QColor("#dfaa36"),15).pixmap(15,15));
            footer->addWidget(star);
        }
        vertical->addLayout(footer);
        auto* location=makeLabel(qpath(project.directory),"muted",this);
        location->setToolTip(qpath(project.directory));
        location->setMaximumHeight(18);
        vertical->addWidget(location);
        setToolTip(qwide(project.description));
        // Child labels should not consume clicks intended to select/open a card.
        for (auto* widget : findChildren<QWidget*>())
            widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    fs::path const& path() const { return path_; }
    void setSelected(bool selected)
    {
        if (property("selected").toBool() == selected) return;
        setProperty("selected", selected);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button()==Qt::LeftButton) callback_(false);
        QFrame::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button()==Qt::LeftButton) callback_(true);
        QFrame::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter) callback_(true);
        else if (event->key()==Qt::Key_Space) callback_(false);
        else QFrame::keyPressEvent(event);
    }
private:
    std::function<void(bool)> callback_;
    fs::path path_;
};

class Browser final : public QMainWindow {
public:
    explicit Browser(bool smoke)
        : smoke_(smoke), catalog_(smoke
             ? fs::path(temp_.path().toStdWString()) / "Projects"
             : ns::default_projects_directory(),
             smoke ? fs::path(temp_.path().toStdWString()) / "catalog.json"
                   : ns::default_catalog_file(),NORTHSTARCPP_ENGINE_VERSION)
    {
        setObjectName("projectBrowser");
        setWindowTitle("NorthStarCpp | Project Browser");
        setMinimumSize(960,620);
        resize(1390,850);
        setWindowIcon(QIcon(":/logoStar.ico"));
        buildUi();
        refresh();
    }
    int verifySmoke()
    {
        try {
            auto p=catalog_.create(L"Qt Smoke Project","sandbox");
            refresh(p.directory);
            if (projects_.empty() || !selection() || !fontFamily.size()) return 3;
            catalog_.favorite(p.directory,true);
            refresh(p.directory);
            if (!selection() || !selection()->favorite) return 4;
            search_->setText("Smoke Project");
            if (visibleCount_ != 1) return 5;
            search_->setText("absent");
            if (visibleCount_ != 0) return 6;
            search_->clear();
            if (visibleCount_ != 1) return 7;

            // Regression: a deleted project can still appear in the catalog.
            // Double-clicking its card must not destroy that card mid-event,
            // start the editor, or terminate the Project Browser.
            auto stale = catalog_.duplicate(p.directory, L"Deleted Qt Smoke Project");
            refresh(stale.directory);
            if (!selection() || selection()->status != ns::Project::Status::ready ||
                !projectActions_.at(0)->isEnabled()) return 10;
            Card* staleCard = nullptr;
            for (int i = 0; i < grid_->count(); ++i) {
                auto* card = dynamic_cast<Card*>(grid_->itemAt(i)->widget());
                if (card && card->path() == stale.directory) staleCard = card;
            }
            if (!staleCard) return 11;
            // Delete AFTER the UI has rendered the card, before the user
            // double-clicks it. The queued open must catch this race.
            std::error_code error;
            fs::remove_all(stale.directory, error);
            if (error) return 9;
            QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(10,10),
                                    QPointF(10,10), Qt::LeftButton, Qt::LeftButton,
                                    Qt::NoModifier);
            QApplication::sendEvent(staleCard, &doubleClick);
            bool warningShown = false;
            QTimer::singleShot(0, this, [&warningShown] {
                for (auto* widget : QApplication::topLevelWidgets())
                    if (auto* dialog = dynamic_cast<QMessageBox*>(widget)) {
                        warningShown = true;
                        dialog->accept();
                    }
            });
            QApplication::processEvents();
            if (!warningShown || projects_.size() != 1 ||
                !selection() || selection()->status != ns::Project::Status::ready ||
                std::any_of(projects_.begin(), projects_.end(), [&](auto const& item) {
                    return item.directory == stale.directory;
                }))
                return 12;
            return 0;
        } catch (std::exception const& e) {
            qWarning("Qt Project Browser smoke failure: %s",e.what());
            return 8;
        }
    }
private:
    void buildUi()
    {
        auto* root=new QWidget(this);
        root->setObjectName("root");
        setCentralWidget(root);
        auto* overall=new QHBoxLayout(root);
        overall->setContentsMargins(0,0,0,0);
        overall->setSpacing(0);

        auto* sidebar=new QWidget(root);
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(236);
        auto* side=new QVBoxLayout(sidebar);
        side->setContentsMargins(17,24,17,18);
        side->setSpacing(9);
        auto* brand=new QHBoxLayout;
        auto* logo=new QLabel(sidebar);
        logo->setPixmap(awesome(0xf005,QColor("#167da9"),29).pixmap(29,29));
        brand->addWidget(logo);
        brand->addWidget(makeLabel("NORTHSTAR","brand",sidebar));
        brand->addStretch();
        side->addLayout(brand);
        side->addSpacing(12);
        side->addWidget(makeLabel("PROJECT WORKSPACE","eyebrow",sidebar));
        auto navButton=[&](QString name,char32_t icon,int view) {
            auto* button=new QToolButton(sidebar);
            button->setObjectName("nav");
            button->setText(name);
            button->setIcon(awesome(icon));
            button->setIconSize({19,19});
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setCheckable(true);
            button->setMinimumHeight(44);
            button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
            connect(button,&QToolButton::clicked,this,[this,view]{filter_=view;refresh();});
            side->addWidget(button);
            nav_.push_back(button);
        };
        navButton("All Projects",0xf009,0);
        navButton("Recent",0xf1da,1);
        navButton("Favorites",0xf005,2);
        nav_.front()->setChecked(true);
        side->addSpacing(13);
        auto* create=actionButton("New Project",0xf067,sidebar,true);
        connect(create,&QPushButton::clicked,this,[this]{createProject();});
        side->addWidget(create);
        auto* import=actionButton("Import Existing",0xf07c,sidebar);
        connect(import,&QPushButton::clicked,this,[this]{importProject();});
        side->addWidget(import);
        auto* scan=actionButton("Scan Directory",0xf002,sidebar);
        connect(scan,&QPushButton::clicked,this,[this]{scanProjects();});
        side->addWidget(scan);
        side->addStretch();
        auto* notice=makeLabel("NorthStarCpp "+QString(NORTHSTARCPP_ENGINE_VERSION)
                             +"\nQt 6 Widgets · Font Awesome 6","muted",sidebar);
        notice->setWordWrap(true);
        side->addWidget(notice);
        overall->addWidget(sidebar);

        auto* content=new QWidget(root);
        auto* main=new QVBoxLayout(content);
        main->setContentsMargins(28,26,24,24);
        main->setSpacing(14);
        auto* heading=new QHBoxLayout;
        auto* titleBox=new QVBoxLayout;
        titleBox->addWidget(makeLabel("PROJECT BROWSER","headline",content));
        titleBox->addWidget(makeLabel("Choose a project or create a new experience.","muted",content));
        heading->addLayout(titleBox);
        heading->addStretch();
        auto* refreshButton=actionButton("Refresh",0xf2f1,content);
        connect(refreshButton,&QPushButton::clicked,this,[this]{refresh();});
        heading->addWidget(refreshButton);
        main->addLayout(heading);
        auto* bar=new QHBoxLayout;
        search_=new QLineEdit(content);
        search_->setPlaceholderText("Search by name, path, category or tag...");
        search_->setClearButtonEnabled(true);
        connect(search_,&QLineEdit::textChanged,this,[this]{rebuildCards();});
        bar->addWidget(search_,1);
        sort_=new QComboBox(content);
        sort_->addItems({"Recent activity","Project name","Category"});
        connect(sort_,qOverload<int>(&QComboBox::currentIndexChanged),
                this,[this](int){rebuildCards();});
        bar->addWidget(sort_);
        main->addLayout(bar);
        auto* body=new QHBoxLayout;
        body->setSpacing(18);

        auto* listColumn=new QWidget(content);
        auto* listLayout=new QVBoxLayout(listColumn);
        listLayout->setContentsMargins(0,0,0,0);
        auto* sectionHeader=new QHBoxLayout;
        section_=makeLabel("All Projects","section",listColumn);
        sectionHeader->addWidget(section_);
        sectionHeader->addStretch();
        count_=makeLabel("0 projects","muted",listColumn);
        sectionHeader->addWidget(count_);
        listLayout->addLayout(sectionHeader);
        scroll_=new QScrollArea(listColumn);
        scroll_->setWidgetResizable(true);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollContent_=new QWidget;
        scrollContent_->setObjectName("scrollContent");
        grid_=new QGridLayout(scrollContent_);
        grid_->setContentsMargins(0,8,0,12);
        grid_->setHorizontalSpacing(16);
        grid_->setVerticalSpacing(16);
        grid_->setAlignment(Qt::AlignTop);
        scroll_->setWidget(scrollContent_);
        listLayout->addWidget(scroll_);
        body->addWidget(listColumn,3);

        auto* detail=new QWidget(content);
        detail->setObjectName("details");
        detail->setFixedWidth(286);
        auto* detailLayout=new QVBoxLayout(detail);
        detailLayout->setContentsMargins(16,18,16,16);
        detailLayout->setSpacing(10);
        detailLayout->addWidget(makeLabel("PROJECT DETAILS","eyebrow",detail));
        detailTitle_=makeLabel("Select a project","section",detail);
        detailTitle_->setWordWrap(true);
        detailLayout->addWidget(detailTitle_);
        detailDescription_=makeLabel("Select a project to view information.","muted",detail);
        detailDescription_->setWordWrap(true);
        detailLayout->addWidget(detailDescription_);
        detailPath_=makeLabel("","muted",detail);
        detailPath_->setWordWrap(true);
        detailPath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        detailLayout->addWidget(detailPath_);
        detailStatus_=makeLabel("","muted",detail);
        detailLayout->addWidget(detailStatus_);
        detailLayout->addSpacing(10);
        auto detailAction=[&](QString title,char32_t glyph,std::function<void()> handler,bool primary=false) {
            auto* button=actionButton(title,glyph,detail,primary);
            connect(button,&QPushButton::clicked,this,[handler=std::move(handler)]{handler();});
            detailLayout->addWidget(button);
            projectActions_.push_back(button);
            return button;
        };
        detailAction("Open Project",0xf04b,[this]{openProject();},true);
        detailAction("Favorite / Unfavorite",0xf005,[this]{favoriteProject();});
        detailAction("Rename",0xf044,[this]{renameProject();});
        detailAction("Duplicate",0xf0c5,[this]{duplicateProject();});
        detailAction("Open Folder",0xf07c,[this]{openFolder();});
        detailAction("Remove from List",0xf2ed,[this]{forgetProject();});
        detailLayout->addStretch();
        body->addWidget(detail,0);
        main->addLayout(body,1);
        overall->addWidget(content,1);
    }

    ns::Project const* selection() const
    {
        for (auto const& p:projects_)
            if (p.directory==selectedPath_) return &p;
        return nullptr;
    }
    void refresh(fs::path preferred={})
    {
        try { projects_=catalog_.list(); }
        catch (std::exception const& e) {messageError(this,"Cannot read project catalog",e);return;}
        if (!preferred.empty()) selectedPath_=preferred;
        if (!selection()) selectedPath_=projects_.empty()?fs::path{}:projects_.front().directory;
        for (int i=0;i<3;++i) nav_[i]->setChecked(i==filter_);
        rebuildCards();
    }
    void rebuildCards()
    {
        if (!grid_) return;
        while (auto* item=grid_->takeAt(0)) {
            if (auto* w=item->widget()) delete w;
            delete item;
        }
        std::vector<ns::Project> visible;
        auto const search=search_->text().trimmed();
        for (auto const& p:projects_) {
            if (filter_==1 && p.last_opened.empty()) continue;
            if (filter_==2 && !p.favorite) continue;
            QString const fields=qwide(p.name)+" "+qwide(p.category)+" "+
                qwide(p.description)+" "+qpath(p.directory);
            QStringList tags;
            for (auto const& tag:p.tags) tags<<qwide(tag);
            if (!search.isEmpty() && !fields.contains(search,Qt::CaseInsensitive) &&
                !tags.join(' ').contains(search,Qt::CaseInsensitive)) continue;
            visible.push_back(p);
        }
        if (sort_->currentIndex()==1) {
            std::sort(visible.begin(),visible.end(),
                [](auto const& a,auto const& b){return a.name<b.name;});
        } else if (sort_->currentIndex()==2) {
            std::sort(visible.begin(),visible.end(),
                [](auto const& a,auto const& b){
                    return a.category==b.category?a.name<b.name:a.category<b.category;
                });
        }
        visibleCount_=static_cast<int>(visible.size());
        static constexpr char const* headings[]={"All Projects","Recently Opened","Favorites"};
        section_->setText(headings[filter_]);
        count_->setText(QString("%1 of %2 projects").arg(visible.size()).arg(projects_.size()));
        int const available=std::max(360,scroll_->viewport()->width());
        int const columns=std::max(1,std::min(4,available/265));
        for (int index=0;index<static_cast<int>(visible.size());++index) {
            auto const path=visible[index].directory;
            auto callback=[this,path](bool open) {
                if (open) {
                    // Never rebuild/delete a card during its mouse/keyboard event.
                    QTimer::singleShot(0, this, [this,path] {
                        selectedPath_=path;
                        openProject();
                    });
                } else {
                    selectProject(path);
                }
            };
            grid_->addWidget(new Card(visible[index],path==selectedPath_,callback,scrollContent_),
                             index/columns,index%columns);
        }
        for (int i=0;i<columns;++i) grid_->setColumnStretch(i,1);
        if (visible.empty()) {
            auto* empty=makeLabel("No matching projects. Create one or import an existing folder.",
                                  "muted",scrollContent_);
            empty->setWordWrap(true);
            grid_->addWidget(empty,0,0);
        }
        updateDetails();
    }
    void selectProject(fs::path const& path)
    {
        selectedPath_ = path;
        for (int i = 0; i < grid_->count(); ++i) {
            auto* item = grid_->itemAt(i);
            if (auto* card = dynamic_cast<Card*>(item->widget()))
                card->setSelected(card->path() == path);
        }
        updateDetails();
    }
    void updateDetails()
    {
        auto const* p=selection();
        bool const ready=p && p->status==ns::Project::Status::ready;
        for (auto* button:projectActions_) button->setEnabled(p!=nullptr);
        if (!p) {
            detailTitle_->setText("Select a project");
            detailDescription_->clear();
            detailPath_->clear();
            detailStatus_->clear();
            return;
        }
        projectActions_.at(0)->setEnabled(ready);
        projectActions_.at(1)->setEnabled(ready);
        projectActions_.at(2)->setEnabled(ready);
        projectActions_.at(3)->setEnabled(ready);
        projectActions_.at(4)->setEnabled(fs::is_directory(p->directory));
        detailTitle_->setText(qwide(p->name));
        detailDescription_->setText(qwide(p->description).isEmpty()
                                  ?"No project description":qwide(p->description));
        detailPath_->setText(qpath(p->directory));
        detailStatus_->setText("Status: "+statusLabel(*p)+"\nEngine: "+
                               qs(p->engine_version.empty()?"unspecified":p->engine_version)+
                               (p->diagnostic.empty()?QString{}:"\n"+qwide(p->diagnostic)));
    }
    void openProject()
    {
        auto const path = selectedPath_;
        if (path.empty()) return;
        try {
            // Re-inspect immediately before launching: a previously valid card
            // may refer to a directory or scene removed after the last scan.
            auto const current = catalog_.inspect(path);
            if (current.status != ns::Project::Status::ready) {
                refresh(path);
                QMessageBox::warning(this, "Project unavailable",
                    "This project cannot be opened.\nStatus: " + statusLabel(current) +
                    (current.diagnostic.empty()?QString{}:"\n"+qwide(current.diagnostic)));
                return;
            }
            fs::path executable=ns::executable_directory()/L"northstarcpp_editor_qt.exe";
            if(!fs::exists(executable))
                executable=ns::executable_directory()/L"northstarcpp_editor.exe";
            if (!fs::is_regular_file(executable))
                executable=ns::executable_directory().parent_path()/
                    L"northstarcpp_editor.exe";
            if (!fs::is_regular_file(executable))
                throw std::runtime_error("NorthStarCpp editor is missing");
            QStringList args{"--scene",qpath(current.startup_scene)};
            if (!QProcess::startDetached(qpath(executable),args,qpath(executable.parent_path())))
                throw std::runtime_error("Failed to launch NorthStarCpp editor");
            catalog_.opened(path);
            refresh(path);
        } catch (std::exception const& e) {
            messageError(this,"Cannot open project",e);
            refresh(path);
        }
    }
    void createProject()
    {
        QDialog d(this);
        d.setWindowTitle("Create NorthStarCpp Project");
        d.resize(540,350);
        auto* layout=new QVBoxLayout(&d);
        auto* heading=makeLabel("Create a New Project","headline",&d);
        layout->addWidget(heading);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(&d);name->setPlaceholderText("My Game");
        form->addRow("Project name",name);
        auto* choice=new QComboBox(&d);
        auto const templates=ns::templates();
        for (auto const& t:templates) choice->addItem(qwide(t.label)+"  ·  "+qwide(t.category),qs(t.id));
        form->addRow("Template",choice);
        auto* location=new QLineEdit(qpath(catalog_.projects_directory()),&d);
        auto* browse=actionButton("Browse",0xf07c,&d);
        auto* pathRow=new QHBoxLayout;pathRow->addWidget(location,1);pathRow->addWidget(browse);
        connect(browse,&QPushButton::clicked,&d,[&] {
            QString const folder=QFileDialog::getExistingDirectory(&d,"Select project folder",location->text());
            if (!folder.isEmpty())location->setText(folder);
        });
        form->addRow("Location",pathRow);
        auto* description=new QLineEdit(&d);
        description->setPlaceholderText("Description (optional)");
        form->addRow("Description",description);
        layout->addLayout(form);
        auto* actions=new QDialogButtonBox(QDialogButtonBox::Cancel,&d);
        auto* create=actions->addButton("Create Project",QDialogButtonBox::AcceptRole);
        create->setObjectName("primary");
        layout->addWidget(actions);
        connect(actions,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        connect(create,&QPushButton::clicked,&d,[&] {
            try {
                auto p=catalog_.create(name->text().trimmed().toStdWString(),
                                       choice->currentData().toString().toStdString(),
                                       fspath(location->text()));
                if (!description->text().isEmpty())
                    catalog_.update_description(p.directory,description->text().toStdWString(),
                                                p.category);
                selectedPath_=p.directory;
                d.accept();
            } catch (std::exception const& e){messageError(&d,"Cannot create project",e);}
        });
        name->setFocus();
        if (d.exec()==QDialog::Accepted) refresh(selectedPath_);
    }
    void importProject()
    {
        QString const folder=QFileDialog::getExistingDirectory(
            this,"Import a NorthStarCpp project",qpath(catalog_.projects_directory()));
        if (folder.isEmpty()) return;
        try {
            auto p=catalog_.import(fspath(folder));
            filter_=0;refresh(p.directory);
        } catch (std::exception const& e){messageError(this,"Cannot import project",e);}
    }
    void scanProjects()
    {
        QString const folder=QFileDialog::getExistingDirectory(
            this,"Discover NorthStarCpp projects",qpath(catalog_.projects_directory()));
        if (folder.isEmpty()) return;
        try {
            auto const count=catalog_.scan(fspath(folder),3);
            filter_=0;refresh();
            QMessageBox::information(this,"Project Discovery",
                                     QString("Imported %1 projects.").arg(count));
        } catch (std::exception const& e){messageError(this,"Cannot discover projects",e);}
    }
    void favoriteProject()
    {
        auto const* p=selection();if(!p)return;
        auto const path=p->directory;
        try { catalog_.favorite(path,!p->favorite);refresh(path); }
        catch (std::exception const& e){messageError(this,"Cannot update favorite",e);}
    }
    void renameProject()
    {
        auto const* p=selection();if(!p)return;
        QDialog dialog(this);dialog.setWindowTitle("Rename Project");
        auto* layout=new QVBoxLayout(&dialog);
        auto* name=new QLineEdit(qwide(p->name),&dialog);
        layout->addWidget(makeLabel("Project name",{},&dialog));
        layout->addWidget(name);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        auto const path=p->directory;
        if (dialog.exec()==QDialog::Accepted) try {
            catalog_.rename(path,name->text().trimmed().toStdWString());refresh(path);
        } catch (std::exception const& e){messageError(this,"Cannot rename project",e);}
    }
    void duplicateProject()
    {
        auto const* p=selection();if(!p)return;
        QDialog dialog(this);dialog.setWindowTitle("Duplicate Project");
        auto* layout=new QVBoxLayout(&dialog);
        auto* name=new QLineEdit(qwide(p->name)+" Copy",&dialog);
        layout->addWidget(makeLabel("New project name",{},&dialog));
        layout->addWidget(name);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        auto const path=p->directory;
        if (dialog.exec()==QDialog::Accepted) try {
            auto copied=catalog_.duplicate(path,name->text().trimmed().toStdWString());
            refresh(copied.directory);
        } catch (std::exception const& e){messageError(this,"Cannot duplicate project",e);}
    }
    void openFolder()
    {
        auto const* p=selection();
        if(p && fs::is_directory(p->directory))
            QDesktopServices::openUrl(QUrl::fromLocalFile(qpath(p->directory)));
    }
    void forgetProject()
    {
        auto const* p=selection();if(!p)return;
        if (QMessageBox::question(this,"Remove from Project Browser",
           "Remove this project from the browser list?\nThe project files will NOT be deleted.",
           QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes) return;
        try {catalog_.forget(p->directory);selectedPath_.clear();refresh();}
        catch (std::exception const& e){messageError(this,"Cannot remove project",e);}
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QMainWindow::resizeEvent(event);
        if (scroll_) QTimer::singleShot(0,this,[this]{rebuildCards();});
    }
    bool smoke_;
    QTemporaryDir temp_;
    ns::Catalog catalog_;
    std::vector<ns::Project> projects_;
    fs::path selectedPath_;
    int filter_=0;
    int visibleCount_=0;
    std::vector<QToolButton*> nav_;
    std::vector<QPushButton*> projectActions_;
    QLineEdit* search_=nullptr;
    QComboBox* sort_=nullptr;
    QLabel *section_=nullptr,*count_=nullptr,*detailTitle_=nullptr,
           *detailDescription_=nullptr,*detailPath_=nullptr,*detailStatus_=nullptr;
    QScrollArea* scroll_=nullptr;
    QWidget* scrollContent_=nullptr;
    QGridLayout* grid_=nullptr;
};
} // anonymous namespace

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    bool const smoke=app.arguments().contains("--smoke");
    std::unique_ptr<northstarcpp::Logger> logger;
    if (!smoke || northstarcpp::application_log_directory_overridden()) {
        try {
            auto const logging=northstarcpp::application_logger_configuration(
                "NorthStarCpp-ProjectManager");
            if (logging.enabled) {
                northstarcpp::prepare_application_log_console(logging);
                logger=std::make_unique<northstarcpp::Logger>(logging.options);
                logger->info("projects","Qt Project Manager starting");
            }
        } catch(std::exception const& e) {
            std::fprintf(stderr,"Project Manager logger initialization: %s\n",e.what());
            qWarning("Project Manager logger initialization: %s",e.what());
        }
    }

    try {
        app.setApplicationName("NorthStarCpp Project Browser");
        app.setOrganizationName("Kayla's Systems");
        app.setStyleSheet(stylesheet);
        int const fontId=QFontDatabase::addApplicationFont(":/fontawesome/fa-solid-900.ttf");
        if (fontId<0 || QFontDatabase::applicationFontFamilies(fontId).empty()) {
            qCritical("Font Awesome 6 Free resource failed to load");
            if (logger) {logger->error("projects","Font Awesome resource missing");logger->flush();}
            return 18;
        }
        fontFamily=QFontDatabase::applicationFontFamilies(fontId).front();
        Browser window(smoke);
        window.show();
        if (smoke) QTimer::singleShot(0,&window,[&] {app.exit(window.verifySmoke());});
        int const result=app.exec();
        if(logger) {
            if(result==0)logger->info("projects","Qt Project Manager finished successfully");
            else logger->error("projects","Qt Project Manager exited with code " +
                std::to_string(result));
            logger->flush();
        }
        return result;
    } catch(std::exception const& e) {
        if(logger)try {logger->fatal("projects",e.what());logger->flush();}
            catch(std::exception const&) {}
        std::fprintf(stderr,"Qt Project Manager: %s\n",e.what());
        qCritical("Qt Project Manager: %s",e.what());
        return 1;
    }
}
