// NorthStarCpp Qt Project Browser
// Copyright (c) 2026 Kayla's Systems — Kayla Verner
// Qt 6 Widgets + Font Awesome 6 Free (see resources/fontawesome/LICENSE.txt).
#include <northstarcpp/project_catalog.hpp>
#include "provider_inventory.hpp"
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_feature_requirements.hpp>
#include <northstarcpp/logger.hpp>

#include <QApplication>
#include "../editor/qt_menu_theme.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
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
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <map>
#include <set>
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
QFrame#providerSettingCard {
  background:#ffffff; border:1px solid #dce4ed; border-radius:10px;
}
QFrame#providerSettingCard QLabel { border:0; background:transparent; }
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

using RenderCapabilityRow=northstarcpp::render::FeatureRequirement;
inline constexpr auto& renderCapabilities=
    northstarcpp::render::feature_requirements;

QString renderCapabilityStatus(ns::ProviderChoice const& option,
                               RenderCapabilityRow const& row)
{
    if(row.requires_extension) {
        return option.verified_render_apis&row.bit?
            QString("ABI exposed"):QString("ABI unavailable");
    }
    if(!option.has_feature_manifest)return "Not reported";
    return option.advertised_render_features&row.bit?
        QString("Declared"):QString("Not implemented");
}
QStringList incompatibleRendererRequirements(
    ns::ProviderChoice const& provider,
    std::vector<std::string> const& requirements)
{
    auto missing=northstarcpp::render::missing_feature_requirements(
        requirements,provider.verified_render_apis,
        provider.advertised_render_features,provider.has_feature_manifest);
    QStringList result;
    for(auto const& id:missing) {
        auto const* feature=northstarcpp::render::find_feature_requirement(id);
        result<<QString::fromUtf8(feature?feature->label:id.c_str());
    }
    return result;
}
bool lacksMaterialApis(ns::ProviderChoice const& option)
{
    constexpr auto required=NS_RENDER_FEATURE_TEXTURED_MATERIALS |
                            NS_RENDER_FEATURE_MATERIAL_GRAPH;
    return (option.verified_render_apis&required)!=required;
}
QString rendererCompatibilityWarning(ns::ProviderChoice const& option)
{
    QStringList missing;
    if(!(option.verified_render_apis&NS_RENDER_FEATURE_TEXTURED_MATERIALS))
        missing<<"PBR materials";
    if(!(option.verified_render_apis&NS_RENDER_FEATURE_MATERIAL_GRAPH))
        missing<<"Material Graph";
    if(!option.has_feature_manifest)
        missing<<"post-processing metadata (unknown)";
    else {
        for(auto const& row:renderCapabilities) {
            if(row.requires_extension)continue;
            if(!(option.advertised_render_features&row.bit))
                missing<<row.label;
        }
    }
    if(missing.isEmpty())return "All published features are available (hardware not tested).";
    return "Not available / not reported: "+missing.join(", ")+
           ". The renderer may omit parts of the scene.";
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
            if(ns::provider_environment_key("engine.render")!="NORTHSTARCPP_RENDER_PROVIDER" ||
               ns::provider_environment_key("engine.physics")!="NORTHSTARCPP_PHYSICS_PROVIDER")
                return 33;
            auto renderCount=std::count_if(providerChoices_.begin(),providerChoices_.end(),
                [](auto const& c){return c.capability=="engine.render";});
            auto physicsCount=std::count_if(providerChoices_.begin(),providerChoices_.end(),
                [](auto const& c){return c.capability=="engine.physics";});
            if(fs::is_regular_file(ns::executable_directory()/"render_d3d12.dll") &&
               fs::is_regular_file(ns::executable_directory()/"render_d3d11.dll") &&
               renderCount<2)return 34;
            if(fs::is_regular_file(ns::executable_directory()/"physics_bullet.dll") &&
               fs::is_regular_file(ns::executable_directory()/"physics_jolt.dll") &&
               physicsCount<2)return 35;
            if(fs::is_regular_file(ns::executable_directory()/"AssetManager.dll") &&
               std::none_of(providerChoices_.begin(),providerChoices_.end(),
                   [](auto const& c) {return c.capability=="engine.assets" &&
                                              c.id=="directory";}))
                return 44;
            // Actual exported feature declarations and extension tables must
            // be observed, not inferred from the binary filename.
            for(auto const& option:providerChoices_) {
                if(option.capability!="engine.render")continue;
                if(!option.has_feature_manifest)return 45;
                if(option.id=="vulkan") {
                    if(!(option.verified_render_apis&
                         NS_RENDER_FEATURE_TEXTURED_MATERIALS))
                        return 54; // New Vulkan PBR texture extension is mandatory.
                    if(!(option.verified_render_apis&
                         NS_RENDER_FEATURE_DYNAMIC_LIGHTS))
                        return 55; // Vulkan native lighting extension.
                    if(!(option.verified_render_apis&
                         NS_RENDER_FEATURE_GPU_SKINNING))
                        return 56; // Original eight native GPU influences.
                    if(!(option.verified_render_apis&
                         NS_RENDER_FEATURE_WORLD_SURFACES))
                        return 57; // BC1/BC5 original compressed world textures.
                    if(!(option.verified_render_apis&NS_RENDER_FEATURE_MATERIAL_GRAPH))
                        return 46; // GPU v2 now required of Vulkan.
                    if(option.advertised_render_features&NS_RENDER_FEATURE_HDR)
                        return 47;
                    if(!(option.verified_render_apis&NS_RENDER_FEATURE_GPU_TIMING))
                        return 48;
                }
                if(option.id=="d3d12") {
                    if(!(option.verified_render_apis&NS_RENDER_FEATURE_MATERIAL_GRAPH))
                        return 49;
                    if(!(option.advertised_render_features&NS_RENDER_FEATURE_HDR))
                        return 50;
                }
                if(option.id=="d3d11") {
                    if(!(option.verified_render_apis&NS_RENDER_FEATURE_WORLD_SURFACES))
                        return 51;
                    if(option.advertised_render_features&NS_RENDER_FEATURE_HDR)
                        return 52;
                }
            }
            if(renderCount>1) {
                QString selected;
                bool opened=false;
                bool sized=false;
                bool hasGpuProbe=false;
                QTimer::singleShot(0,this,[this,&opened,&sized,&selected,&hasGpuProbe] {
                    auto* dialog=findChild<QDialog*>("providerSettingsDialog");
                    if(!dialog)return;
                    opened=true;
                    auto* combo=dialog->findChild<QComboBox*>("providerChoice_engine.render");
                    auto* scroll=dialog->findChild<QScrollArea*>("providerSettingsScroll");
                    sized=dialog->width()>=580 && dialog->height()>=440 &&
                          scroll && scroll->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff;
                    hasGpuProbe=dialog->findChild<QPushButton*>("gpuProbe_engine.render") &&
                        dialog->findChild<QLabel*>("gpuProbeResult_engine.render");
                    if(combo && combo->count()>1) {
                        auto index=combo->findData("vulkan");
                        if(index<0)index=1;
                        selected=combo->itemData(index).toString();
                        combo->setCurrentIndex(index);
                    }
                    dialog->accept();
                });
                configureProjectProviders();
                if(!opened || !sized || selected.isEmpty())return 36;
                if(!hasGpuProbe)return 53;
                auto stored=catalog_.inspect(p.directory);
                if(stored.provider_choices.count("engine.render")==0 ||
                   qs(stored.provider_choices.at("engine.render"))!=selected)return 37;
                bool reopened=false;
                QTimer::singleShot(0,this,[this,&reopened] {
                    auto* dialog=findChild<QDialog*>("providerSettingsDialog");
                    if(!dialog)return;
                    auto* combo=dialog->findChild<QComboBox*>("providerChoice_engine.render");
                    if(!combo)return;
                    reopened=true;
                    combo->setCurrentIndex(0);
                    dialog->reject();
                });
                configureProjectProviders();
                if(!reopened)return 38;
                if(catalog_.inspect(p.directory).provider_choices.count("engine.render")==0)
                    return 45; // Cancel must not persist edits.
                QTimer::singleShot(0,this,[this] {
                    auto* dialog=findChild<QDialog*>("providerSettingsDialog");
                    if(!dialog)return;
                    auto* box=dialog->findChild<QDialogButtonBox*>();
                    if(box)for(auto* button:box->buttons())
                        if(box->buttonRole(button)==QDialogButtonBox::ResetRole) {
                            button->click();
                            break;
                        }
                    dialog->accept();
                });
                configureProjectProviders();
                if(catalog_.inspect(p.directory).provider_choices.count("engine.render")!=0)
                    return 39;
            }
            catalog_.set_provider(p.directory,"engine.physics","jolt");
            if(catalog_.inspect(p.directory).provider_choices.count("engine.physics")==0) return 30;
            if(catalog_.inspect(p.directory).provider_choices.at("engine.physics")!="jolt")
                return 20;
            catalog_.set_provider(p.directory,"engine.render","vulkan");
            if(catalog_.inspect(p.directory).provider_choices.count("engine.render")==0) return 31;
            if(catalog_.inspect(p.directory).provider_choices.at("engine.render")!="vulkan")
                return 21;
            catalog_.set_provider(p.directory,"engine.physics","");
            if(catalog_.inspect(p.directory).provider_choices.count("engine.render")==0) return 32;
            if(catalog_.inspect(p.directory).provider_choices.count("engine.physics")!=0 ||
               catalog_.inspect(p.directory).provider_choices.at("engine.render")!="vulkan")
                return 22;
            catalog_.set_provider(p.directory,"engine.render","");
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

            // A successful Editor handoff must close the Project Browser.
            // Launch a short-lived copy of this executable instead of a real
            // GUI Editor so that the smoke test is deterministic and headless.
            // The child exits immediately with --smoke-editor-child.
            selectProject(p.directory);
            catalog_.set_provider(p.directory,"engine.render","vulkan");
            catalog_.set_provider(p.directory,"engine.physics","jolt");
            auto const marker=p.directory/"provider-launch-smoke.txt";
            smokeEditorHandoff_ = true;
            openProject();
            if (!editorLaunched_ || isVisible())return 13;
            // Verify the detached child ACTUALLY inherited both choices.
            for(unsigned attempt=0;attempt<100&&!fs::exists(marker);++attempt)
                QThread::msleep(10);
            QFile observed(qpath(marker));
            if(!observed.open(QIODevice::ReadOnly))return 40;
            if(observed.readAll()!="vulkan\njolt\n")return 41;
            return 0;
        } catch (std::exception const& e) {
            qWarning("Qt Project Browser smoke failure: %s",e.what());
            auto diagnostic=qgetenv("NORTHSTARCPP_QT_SMOKE_ERROR_FILE");
            if(!diagnostic.isEmpty()) {
                QFile log(QString::fromUtf8(diagnostic));
                if(log.open(QIODevice::WriteOnly|QIODevice::Truncate))
                    log.write(e.what());
            }
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
        detail->setFixedWidth(328);
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
        detailLayout->addSpacing(8);
        detailLayout->addWidget(makeLabel("ENGINE API PROVIDERS","eyebrow",detail));
        providerHint_=makeLabel("Select a project to configure providers.","muted",detail);
        providerHint_->setWordWrap(true);
        detailLayout->addWidget(providerHint_);
        providerSettingsButton_=actionButton(
            "Configure Engine Providers...",0xf085,detail);
        providerSettingsButton_->setObjectName("providerSettingsButton");
        connect(providerSettingsButton_,&QPushButton::clicked,this,
            [this]{configureProjectProviders();});
        detailLayout->addWidget(providerSettingsButton_);
        detailLayout->addSpacing(8);
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
        try {
            projects_=catalog_.list();
            providerChoices_=ns::discover_provider_choices(ns::executable_directory());
        } catch(std::exception const& e) {
            messageError(this,"Cannot refresh project or provider catalog",e);
            return;
        }
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
    using ProviderGroups=std::map<std::string,std::vector<ns::ProviderChoice>>;
    ProviderGroups providerGroups() const
    {
        ProviderGroups groups;
        for(auto const& choice:providerChoices_)
            groups[choice.capability].push_back(choice);
        return groups;
    }

    void compareRenderProviders()
    {
        std::vector<ns::ProviderChoice> renderers;
        for(auto const& option:providerChoices_)
            if(option.capability=="engine.render")renderers.push_back(option);
        if(renderers.empty())return;
        QDialog dialog(this);
        dialog.setWindowTitle("Renderer API Compatibility");
        dialog.resize(820,620);
        auto* layout=new QVBoxLayout(&dialog);
        auto* subtitle=makeLabel(
            "ABI exposed: validated plugin function table. Declared: compiled "
            "GPU pass (not a hardware/performance guarantee). "
            "Not reported: no feature declaration.",{},&dialog);
        subtitle->setWordWrap(true);
        layout->addWidget(subtitle);
        auto* matrix=new QTableWidget(
            static_cast<int>(std::size(renderCapabilities)),
            static_cast<int>(renderers.size())+1,&dialog);
        matrix->setObjectName("renderProviderCompatibilityMatrix");
        QStringList headings{"Capability"};
        for(auto const& option:renderers)headings<<qs(option.id);
        matrix->setHorizontalHeaderLabels(headings);
        matrix->verticalHeader()->hide();
        matrix->setEditTriggers(QAbstractItemView::NoEditTriggers);
        matrix->setSelectionMode(QAbstractItemView::NoSelection);
        matrix->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for(int row=0;row<static_cast<int>(std::size(renderCapabilities));++row) {
            auto const& entry=renderCapabilities[row];
            matrix->setItem(row,0,new QTableWidgetItem(entry.label));
            for(int col=0;col<static_cast<int>(renderers.size());++col) {
                auto* item=new QTableWidgetItem(
                    renderCapabilityStatus(renderers[col],entry));
                if(item->text()=="ABI unavailable"||item->text()=="Not implemented")
                    item->setForeground(QBrush(QColor("#ad4632")));
                else if(item->text()=="Not reported")
                    item->setForeground(QBrush(QColor("#756f64")));
                matrix->setItem(row,col+1,item);
            }
        }
        layout->addWidget(matrix);
        auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
        connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(close);
        dialog.exec();
    }
    void startGpuDeviceProbe(QDialog* dialog,
                             ns::ProviderChoice const& provider,
                             QPushButton* button,QLabel* result)
    {
        auto const executable=fs::path(
            QApplication::applicationDirPath().toStdWString())/
            L"northstarcpp_gpu_provider_probe.exe";
        if(!fs::is_regular_file(executable)) {
            result->setText("GPU probe executable is missing. Rebuild "
                            "northstarcpp_gpu_provider_probe.");
            result->setStyleSheet("color:#ae4835;");
            return;
        }
        if(provider.builtin) {
            result->setText("This builtin provider has no external GPU probe "
                            "module. A runnable external provider is required.");
            return;
        }
        auto const dll=fs::u8path(provider.source);
        if(!fs::is_regular_file(dll)) {
            result->setText("Selected provider DLL is no longer on disk.");
            result->setStyleSheet("color:#ae4835;");
            return;
        }
        button->setEnabled(false);
        result->setStyleSheet("color:#62798c;");
        result->setText("Initializing hardware renderer and checking GPU "
                        "readback / materials. No automatic backend fallback.");
        auto* process=new QProcess(dialog); // Driver faults isolated from Qt.
        process->setProgram(qpath(executable));
        process->setArguments({qpath(dll),"--full"});
        process->setWorkingDirectory(qpath(executable.parent_path()));
        auto environment=QProcessEnvironment::systemEnvironment();
        environment.remove("NORTHSTARCPP_RENDER_PROVIDER");
        environment.remove("NORTHSTARCPP_RENDER_PLUGIN");
        process->setProcessEnvironment(environment);
        QPointer<QPushButton> buttonGuard(button);
        QPointer<QLabel> resultGuard(result);
        QPointer<QProcess> processGuard(process);
        QTimer::singleShot(45000,process,[processGuard] {
            if(processGuard && processGuard->state()!=QProcess::NotRunning)
                processGuard->kill(); // Clear failure, no UI hang.
        });
        connect(process,&QProcess::finished,dialog,
            [this,processGuard,buttonGuard,resultGuard,provider](
                int exitCode,QProcess::ExitStatus status) {
                if(buttonGuard)buttonGuard->setEnabled(true);
                if(!processGuard||!resultGuard)return;
                auto const output=processGuard->readAllStandardOutput();
                auto const document=QJsonDocument::fromJson(output);
                auto const details=document.object();
                bool const passed=status==QProcess::NormalExit && exitCode==0 &&
                    details.value("status").toString()=="passed" &&
                    details.value("provider").toString()==qs(provider.id) &&
                    details.value("device_initialized").toBool() &&
                    details.value("geometry_gpu_readback").toBool();
                if(!passed) {
                    auto diagnostic=details.value("error").toString();
                    if(diagnostic.isEmpty()) {
                        diagnostic=processGuard->errorString();
                        if(diagnostic.isEmpty()||diagnostic=="Unknown error")
                            diagnostic="GPU process exited without a valid result "
                                       "(timeout, driver fault or bad ABI).";
                    }
                    resultGuard->setText("GPU test failed: "+diagnostic);
                    resultGuard->setStyleSheet("color:#ae4835;");
                }else {
                    auto text=QString("Hardware initialized; geometry GPU readback passed.");
                    if(details.value("device_identity_verified").toBool()) {
                        auto const gpu=details.value("adapter_name").toString();
                        auto const vendor=details.value("vendor_id").toInt();
                        auto const device=details.value("device_id").toInt();
                        text=QString("GPU: %1 (Vendor 0x%2, Device 0x%3). ")
                            .arg(gpu.isEmpty()?"unnamed":gpu)
                            .arg(vendor,4,16,QChar('0'))
                            .arg(device,4,16,QChar('0'))+text;
                    }else {
                        text="GPU identity extension unavailable. "+text;
                    }
                    if(details.value("texture_api_available").toBool())
                        text+=details.value("texture_gpu_readback").toBool()?
                            " Textured PBR: GPU verified.":
                            " Textured PBR: GPU verification failed.";
                    else text+=" Textured PBR: unsupported.";
                    if(details.value("graph_api_available").toBool())
                        text+=details.value("graph_gpu_readback").toBool()?
                            " Material Graph: GPU verified.":
                            " Material Graph: GPU verification failed.";
                    else text+=" Material Graph: unsupported.";
                    if(details.value("lights_api_available").toBool()) {
                        text+=details.value("light_gpu_readback").toBool()?
                            " Dynamic light: GPU verified.":
                            " Dynamic light API exposed; GPU light probe not completed.";
                    }else text+=" Dynamic light API: unavailable.";
                    if(details.value("gpu_timestamp_valid").toBool()) {
                        text+=QString(" GPU timestamp: %1 ms.")
                            .arg(details.value("gpu_frame_ms").toDouble(),0,'f',3);
                    }else text+=" GPU timing: unavailable.";
                    text+=" Only these test paths are validated, not all"
                          " HDR, CSM and post-processing combinations.";
                    resultGuard->setText(text);
                    resultGuard->setStyleSheet("color:#126b58;");
                }
                processGuard->deleteLater();
            });
        connect(process,&QProcess::errorOccurred,dialog,
            [processGuard,buttonGuard,resultGuard](QProcess::ProcessError error) {
                if(!processGuard||error!=QProcess::FailedToStart)return;
                if(buttonGuard)buttonGuard->setEnabled(true);
                if(resultGuard) {
                    resultGuard->setText(
                        "GPU test could not start: "+processGuard->errorString());
                    resultGuard->setStyleSheet("color:#ae4835;");
                }
                processGuard->deleteLater();
            });
        process->start();
    }

    void updateProviderControls(ns::Project const* project)
    {
        if(!providerHint_ || !providerSettingsButton_)return;
        bool const ready=project && project->status==ns::Project::Status::ready;
        providerSettingsButton_->setEnabled(ready);
        if(!project) {
            providerHint_->setText("Select a project to configure providers.");
            return;
        }
        auto const groups=providerGroups();
        int configurable=0;
        for(auto const& [capability,items]:groups) {
            if(items.size()>1||project->provider_choices.count(capability))
                ++configurable;
        }
        for(auto const& [capability,provider]:project->provider_choices)
            if(!groups.count(capability))++configurable;
        auto const saved=static_cast<int>(project->provider_choices.size());
        providerHint_->setText(!ready
            ?"Project is unavailable. Provider settings cannot be edited."
            :configurable
                ?QString("%1 configurable APIs, %2 explicit overrides. "
                         "Open the full settings window to choose backends, "
                         "compare renderer features, or restore Automatic.")
                     .arg(configurable).arg(saved)
                :"No alternative compatible providers discovered.");
    }

    void configureProjectProviders()
    {
        auto const* project=selection();
        if(!project || project->status!=ns::Project::Status::ready)return;
        auto const path=project->directory;
        auto groups=providerGroups();
        for(auto const& [capability,provider]:project->provider_choices)
            groups.try_emplace(capability);

        QDialog dialog(this);
        dialog.setObjectName("providerSettingsDialog");
        dialog.setWindowTitle("NorthStarCpp | Engine API Providers");
        dialog.setMinimumSize(580,440);
        dialog.resize(800,660);
        auto* layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(22,20,22,18);
        layout->setSpacing(12);
        auto* title=makeLabel("ENGINE API PROVIDERS","section",&dialog);
        layout->addWidget(title);
        auto* description=makeLabel(
            "Configure API implementations for this project. Automatic uses "
            "the engine default. Changes are applied only when you click Save.",
            "muted",&dialog);
        description->setWordWrap(true);
        layout->addWidget(description);

        auto* scroll=new QScrollArea(&dialog);
        scroll->setObjectName("providerSettingsScroll");
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* panel=new QWidget(scroll);
        panel->setObjectName("providerSettingsContent");
        auto* rows=new QVBoxLayout(panel);
        rows->setContentsMargins(4,4,9,8);
        rows->setSpacing(12);
        std::vector<std::pair<std::string,QComboBox*>> inputs;
        auto requiredFeatures=project->required_renderer_features;
        for(auto const& [capability,items]:groups) {
            auto const saved=project->provider_choices.find(capability);
            if(items.size()<=1&&saved==project->provider_choices.end())continue;

            auto* card=new QFrame(panel);
            card->setObjectName("providerSettingCard");
            auto* cardLayout=new QVBoxLayout(card);
            cardLayout->setContentsMargins(16,13,16,14);
            cardLayout->setSpacing(8);
            auto* name=makeLabel(qs(capability),"eyebrow",card);
            cardLayout->addWidget(name);
            auto* combo=new QComboBox(card);
            combo->setObjectName("providerChoice_"+qs(capability));
            combo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            combo->setMinimumContentsLength(12);
            combo->setSizeAdjustPolicy(
                QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumHeight(39);
            combo->addItem("Automatic (engine default)",QString{});
            for(auto const& option:items) {
                auto caption=qs(option.id)+
                    (option.builtin?"  [builtin]":"  [external]");
                if(option.recommended)caption+="  (default)";
                if(option.capability=="engine.render" && option.id=="vulkan")
                    caption+="  (experimental)";
                if(!option.version.empty() && option.version!="embedded")
                    caption+="  v"+qs(option.version);
                combo->addItem(caption,qs(option.id));
                combo->setItemData(combo->count()-1,
                    option.builtin?"Compiled into engine":
                    qpath(fs::u8path(option.source)),Qt::ToolTipRole);
            }
            if(saved!=project->provider_choices.end()) {
                auto index=combo->findData(qs(saved->second));
                if(index<0) {
                    combo->addItem("Unavailable: "+qs(saved->second),
                                   qs(saved->second));
                    index=combo->count()-1;
                    name->setText(qs(capability)+" (missing provider)");
                    name->setStyleSheet("color:#ae4835;");
                }
                combo->setCurrentIndex(index);
            }
            cardLayout->addWidget(combo);
            if(capability=="engine.render") {
                auto* warning=makeLabel("","muted",card);
                warning->setWordWrap(true);
                warning->setTextInteractionFlags(Qt::TextSelectableByMouse);
                auto preview=[combo,warning,items](int index) {
                    auto const selected=combo->itemData(index).toString();
                    auto it=std::find_if(items.begin(),items.end(),
                        [&](auto const& option){return qs(option.id)==selected;});
                    if(it==items.end()) {
                        warning->setText(selected.isEmpty()
                            ?"Automatic: renderer selected at runtime."
                            :"Selected renderer is not available or ABI-compatible.");
                        warning->setStyleSheet(selected.isEmpty()?
                            "color:#62798c;":"color:#ae4835;");
                        return;
                    }
                    warning->setText(rendererCompatibilityWarning(*it));
                    warning->setStyleSheet(lacksMaterialApis(*it)?
                        "color:#ad4632;":"color:#62798c;");
                };
                connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),
                    &dialog,preview);
                preview(combo->currentIndex());
                cardLayout->addWidget(warning);
                auto* compare=actionButton("Compare Render Features",
                    0xf080,card);
                connect(compare,&QPushButton::clicked,&dialog,
                    [this]{compareRenderProviders();});
                cardLayout->addWidget(compare);
                auto* probe=actionButton("Test on This GPU",0xf108,card);
                probe->setObjectName("gpuProbe_"+qs(capability));
                cardLayout->addWidget(probe);
                auto* deviceStatus=makeLabel(
                    "Hardware status: not tested in this session.","muted",card);
                deviceStatus->setObjectName("gpuProbeResult_"+qs(capability));
                deviceStatus->setWordWrap(true);
                deviceStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
                cardLayout->addWidget(deviceStatus);
                connect(probe,&QPushButton::clicked,&dialog,
                    [this,&dialog,combo,probe,deviceStatus,items] {
                        auto const selected=combo->currentData().toString();
                        auto it=std::find_if(items.begin(),items.end(),
                            [&](auto const& option){return qs(option.id)==selected;});
                        if(it==items.end()) {
                            deviceStatus->setText("Choose an explicit external "
                                "renderer before running a GPU test.");
                            return;
                        }
                        startGpuDeviceProbe(&dialog,*it,probe,deviceStatus);
                    });
            }
            rows->addWidget(card);
            inputs.push_back({capability,combo});
        }
        if(inputs.empty()) {
            auto* empty=makeLabel(
                "No configurable providers were discovered for this project.",
                "muted",panel);
            empty->setWordWrap(true);
            rows->addWidget(empty);
        }
        // Mandatory requirements belong to the project, not the backend:
        // choosing Vulkan must never silently disable explicitly required HDR
        // or authored material effects. Stage changes until Save is pressed.
        auto* requirementsCard=new QFrame(panel);
        requirementsCard->setObjectName("providerSettingCard");
        auto* requirementsLayout=new QVBoxLayout(requirementsCard);
        auto* requirementsTitle=makeLabel("Required Rendering Features","eyebrow",
                                          requirementsCard);
        requirementsLayout->addWidget(requirementsTitle);
        auto* requirementsInfo=makeLabel("", "muted",requirementsCard);
        requirementsInfo->setObjectName("requiredRendererFeaturesSummary");
        requirementsInfo->setWordWrap(true);
        auto updateRequirementLabel=[requirementsInfo,&requiredFeatures] {
            requirementsInfo->setText(requiredFeatures.empty()?
                "No strict project-specific requirements; incomplete backends "
                "may be selected for testing.":
                QString("%1 mandatory features. An incompatible renderer will "
                        "be blocked when opening the project.")
                    .arg(requiredFeatures.size()));
        };
        updateRequirementLabel();
        requirementsLayout->addWidget(requirementsInfo);
        auto* configureFeatures=actionButton("Choose Mandatory Features",
                                            0xf0ae,requirementsCard);
        configureFeatures->setObjectName("configureRequiredRendererFeatures");
        requirementsLayout->addWidget(configureFeatures);
        connect(configureFeatures,&QPushButton::clicked,&dialog,
            [&dialog,&requiredFeatures,updateRequirementLabel] {
                QDialog select(&dialog);
                select.setWindowTitle("Required Renderer Features");
                select.resize(660,580);
                auto* body=new QVBoxLayout(&select);
                auto* note=makeLabel(
                    "Mark features this project cannot function without. "
                    "Unavailable or unreported functions will block launching "
                    "an incompatible renderer.",{},&select);
                note->setWordWrap(true);body->addWidget(note);
                auto* scroll=new QScrollArea(&select);
                scroll->setWidgetResizable(true);
                auto* content=new QWidget(scroll);
                auto* grid=new QGridLayout(content);
                std::vector<std::pair<std::string,QCheckBox*>> controls;
                int row=0;
                for(auto const& feature:
                    northstarcpp::render::feature_requirements) {
                    auto* check=new QCheckBox(QString::fromUtf8(feature.label),content);
                    check->setObjectName("requiredFeature_"+QString::fromUtf8(feature.key));
                    check->setChecked(std::find(requiredFeatures.begin(),
                        requiredFeatures.end(),feature.key)!=requiredFeatures.end());
                    grid->addWidget(check,row/2,row%2);
                    controls.push_back({feature.key,check});
                    ++row;
                }
                scroll->setWidget(content);
                body->addWidget(scroll);
                auto* buttons=new QDialogButtonBox(
                    QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&select);
                connect(buttons,&QDialogButtonBox::accepted,
                    &select,&QDialog::accept);
                connect(buttons,&QDialogButtonBox::rejected,
                    &select,&QDialog::reject);
                body->addWidget(buttons);
                if(select.exec()!=QDialog::Accepted)return;
                requiredFeatures.clear();
                for(auto const& [id,check]:controls)
                    if(check->isChecked())requiredFeatures.push_back(id);
                updateRequirementLabel();
            });
        rows->addWidget(requirementsCard);
        rows->addStretch();
        scroll->setWidget(panel);
        layout->addWidget(scroll,1);

        auto* buttons=new QDialogButtonBox(
            QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        auto* reset=buttons->addButton(
            "Reset All to Automatic",QDialogButtonBox::ResetRole);
        reset->setEnabled(!inputs.empty());
        connect(reset,&QPushButton::clicked,&dialog,[&inputs] {
            for(auto const& entry:inputs)entry.second->setCurrentIndex(0);
        });
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        layout->addWidget(buttons);

        if(dialog.exec()!=QDialog::Accepted)return;
        try {
            if(requiredFeatures!=project->required_renderer_features)
                catalog_.set_renderer_requirements(path,requiredFeatures);
            for(auto const& [capability,combo]:inputs) {
                auto const selected=combo->currentData().toString().toUtf8().toStdString();
                auto const previous=project->provider_choices.find(capability);
                if(previous!=project->provider_choices.end()
                    ?previous->second==selected:selected.empty())
                    continue;
                catalog_.set_provider(path,capability,selected);
            }
            refresh(path);
        } catch(std::exception const& error) {
            messageError(this,"Cannot save provider selections",error);
            refresh(path);
        }
    }

    bool chooseUnconfiguredProviders(ns::Project const& project)
    {
        if(smoke_)return true;
        auto groups=providerGroups();
        std::vector<std::pair<std::string,std::vector<ns::ProviderChoice>>> missing;
        for(auto const& [capability,options]:groups)
            if(options.size()>1&&!project.provider_choices.count(capability))
                missing.push_back({capability,options});
        if(missing.empty())return true;
        QDialog dialog(this);
        dialog.setWindowTitle("Select Engine API Providers");
        dialog.setMinimumSize(580,430);
        dialog.resize(780,620);
        auto* layout=new QVBoxLayout(&dialog);
        layout->setContentsMargins(22,20,22,18);
        layout->setSpacing(12);
        auto* summary=makeLabel(
            "Several implementations are available. Choose a provider for "
            "each engine API before opening the project.",{},&dialog);
        summary->setWordWrap(true);
        layout->addWidget(summary);
        auto* scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* panel=new QWidget(scroll);
        auto* choices=new QVBoxLayout(panel);
        choices->setContentsMargins(3,5,9,8);
        choices->setSpacing(12);
        std::vector<std::pair<std::string,QComboBox*>> inputs;
        for(auto const& [capability,options]:missing) {
            choices->addWidget(makeLabel(qs(capability),{},panel));
            auto* combo=new QComboBox(panel);
            combo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            combo->setMinimumContentsLength(12);
            combo->setSizeAdjustPolicy(
                QComboBox::AdjustToMinimumContentsLengthWithIcon);
            int preferred=0;
            for(auto const& option:options) {
                auto label=qs(option.id)+
                    (option.builtin?" [builtin]":" [external]");
                if(option.capability=="engine.render" && option.id=="vulkan")
                    label+=" [experimental]";
                combo->addItem(label,qs(option.id));
                if(option.recommended)preferred=combo->count()-1;
                // Viewport's established automatic order is D3D12, D3D11.
                // Registry's d3d11 default refers to packaging, not viewport.
                if(capability=="engine.render"&&option.id=="d3d12")
                    preferred=combo->count()-1;
            }
            combo->setCurrentIndex(preferred);
            choices->addWidget(combo);
            if(capability=="engine.render") {
                auto* warning=makeLabel("", "muted",panel);
                warning->setWordWrap(true);
                choices->addWidget(warning);
                auto describe=[warning,combo,options](int index) {
                    auto const selected=combo->itemData(index).toString();
                    auto it=std::find_if(options.begin(),options.end(),
                        [&](auto const& item){return qs(item.id)==selected;});
                    if(it==options.end())return;
                    warning->setText(rendererCompatibilityWarning(*it));
                    warning->setStyleSheet(lacksMaterialApis(*it)?
                        "color:#ad4632;":"color:#62798c;");
                };
                connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),
                    &dialog,describe);
                describe(combo->currentIndex());
            }
            inputs.push_back({capability,combo});
        }
        choices->addStretch();
        scroll->setWidget(panel);
        layout->addWidget(scroll,1);
        auto* buttons=new QDialogButtonBox(
            QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return false;
        for(auto const& [capability,combo]:inputs)
            catalog_.set_provider(project.directory,capability,
                combo->currentData().toString().toUtf8().toStdString());
        refresh(project.directory);
        return true;
    }

    void updateDetails()
    {
        auto const* p=selection();
        bool const ready=p && p->status==ns::Project::Status::ready;
        for (auto* button:projectActions_) button->setEnabled(p!=nullptr);
        if (!p) {
            updateProviderControls(nullptr);
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
        updateProviderControls(p);
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
        // A double click and the Open Project action can both queue a launch.
        // Do not create a second Editor while this window is closing.
        if (editorLaunched_) return;
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
            // A saved choice must remain available. Do not silently launch
            // another backend when its selected DLL was removed.
            for(auto const& [capability,provider]:current.provider_choices) {
                auto found=std::find_if(providerChoices_.begin(),providerChoices_.end(),
                    [&](auto const& candidate) {
                        return candidate.capability==capability && candidate.id==provider;
                    });
                if(found==providerChoices_.end()) {
                    QMessageBox::warning(this,"Selected engine provider unavailable",
                        qs(capability)+" uses '"+qs(provider)+
                        "', which is missing or ABI-incompatible. "
                        "Choose another provider or Automatic before opening.");
                    return;
                }
            }
            if(!chooseUnconfiguredProviders(current))return;
            auto const configured=catalog_.inspect(path);
            std::string requirementResolvedRenderer;
            if(!configured.required_renderer_features.empty()) {
                auto compatible=[&](ns::ProviderChoice const& option) {
                    return option.capability=="engine.render" &&
                        incompatibleRendererRequirements(
                            option,configured.required_renderer_features).isEmpty();
                };
                auto explicitRenderer=configured.provider_choices.find("engine.render");
                if(explicitRenderer!=configured.provider_choices.end()) {
                    auto found=std::find_if(providerChoices_.begin(),providerChoices_.end(),
                        [&](auto const& option) {
                            return option.capability=="engine.render" &&
                                   option.id==explicitRenderer->second;
                        });
                    if(found==providerChoices_.end() || !compatible(*found)) {
                        auto missing=found==providerChoices_.end()?
                            QStringList{"Selected renderer is missing"}:
                            incompatibleRendererRequirements(
                                *found,configured.required_renderer_features);
                        QMessageBox::warning(this,"Renderer requirements unmet",
                            "Selected provider '"+qs(explicitRenderer->second)+
                            "' cannot satisfy mandatory rendering features:\n"+
                            missing.join(", ")+
                            "\n\nChoose a compatible renderer or revise project requirements.");
                        return;
                    }
                }else {
                    // Automatic selects only a candidate with ALL declared
                    // requirements. Do not rely on viewport's fallback from
                    // an incompatible backend after scene initialization.
                    auto best=std::find_if(providerChoices_.begin(),providerChoices_.end(),
                        [&](auto const& option) {
                            return option.id=="d3d12"&&compatible(option);
                        });
                    if(best==providerChoices_.end())
                        best=std::find_if(providerChoices_.begin(),providerChoices_.end(),
                            compatible);
                    if(best==providerChoices_.end()) {
                        QMessageBox::warning(this,"No compatible renderer",
                            "No installed provider exposes every mandatory "
                            "rendering feature required by this project.");
                        return;
                    }
                    requirementResolvedRenderer=best->id;
                }
            }
            if(!smoke_) {
                auto it=configured.provider_choices.find("engine.render");
                if(it!=configured.provider_choices.end()) {
                    auto provider=std::find_if(providerChoices_.begin(),providerChoices_.end(),
                        [&](auto const& option) {
                            return option.capability=="engine.render" &&
                                   option.id==it->second;
                        });
                    if(provider!=providerChoices_.end()&&lacksMaterialApis(*provider)) {
                        if(QMessageBox::warning(this,"Renderer functionality warning",
                            "The selected renderer is missing the textured-material "
                            "and/or Material Graph API required by many scenes.\n\n"+
                            rendererCompatibilityWarning(*provider)+
                            "\n\nLaunch with reduced rendering functionality?",
                            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)
                                !=QMessageBox::Yes)return;
                    }
                }
            }
            fs::path executable=ns::executable_directory()/L"northstarcpp_editor_qt.exe";
            if(!fs::is_regular_file(executable))
                executable=ns::executable_directory().parent_path()/
                    L"northstarcpp_editor_qt.exe";
            if (!fs::is_regular_file(executable))
                throw std::runtime_error("NorthStarCpp editor is missing");
            QStringList args{"--scene",qpath(current.startup_scene)};
            if (smoke_ && smokeEditorHandoff_) {
                executable=fs::path(QApplication::applicationFilePath().toStdWString());
                args={"--smoke-editor-child",
                      qpath(current.directory/"provider-launch-smoke.txt")};
            }
            QProcess child;
            child.setProgram(qpath(executable));
            child.setArguments(args);
            child.setWorkingDirectory(qpath(executable.parent_path()));
            auto environment=QProcessEnvironment::systemEnvironment();
            // Do not inherit a stale user/session provider override for a
            // project that deliberately uses Automatic selection. Remove
            // only known API provider-selection environment keys, leaving
            // unrelated process environment and direct DLL path overrides.
            std::set<std::string> capabilities;
            for(auto const& choice:providerChoices_)
                capabilities.insert(choice.capability);
            for(auto const& [capability,provider]:configured.provider_choices)
                capabilities.insert(capability);
            for(auto const& capability:capabilities)
                environment.remove(qs(ns::provider_environment_key(capability)));
            for(auto const& [capability,provider]:configured.provider_choices)
                environment.insert(qs(ns::provider_environment_key(capability)),qs(provider));
            if(!requirementResolvedRenderer.empty())
                environment.insert("NORTHSTARCPP_RENDER_PROVIDER",
                    qs(requirementResolvedRenderer));
            environment.remove("NORTHSTARCPP_RENDER_REQUIRED_FEATURES");
            if(!configured.required_renderer_features.empty()) {
                QStringList features;
                for(auto const& id:configured.required_renderer_features)
                    features<<qs(id);
                environment.insert("NORTHSTARCPP_RENDER_REQUIRED_FEATURES",
                    features.join(","));
            }
            child.setProcessEnvironment(environment);
            if(!child.startDetached())
                throw std::runtime_error("Failed to launch NorthStarCpp editor");

            editorLaunched_=true;
            // Recent-project bookkeeping must not keep the manager open after
            // a successful detached launch (the Editor owns its own process).
            try {
                catalog_.opened(path);
            } catch (std::exception const& error) {
                qWarning("Could not update recently opened project: %s",error.what());
            }
            close();  // Last top-level window: QApplication exits normally.
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
    bool smokeEditorHandoff_=false;
    bool editorLaunched_=false;
    QTemporaryDir temp_;
    ns::Catalog catalog_;
    std::vector<ns::Project> projects_;
    fs::path selectedPath_;
    int filter_=0;
    int visibleCount_=0;
    std::vector<QToolButton*> nav_;
    std::vector<QPushButton*> projectActions_;
    std::vector<ns::ProviderChoice> providerChoices_;
    QPushButton* providerSettingsButton_=nullptr;
    QLabel* providerHint_=nullptr;
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
    if (app.arguments().contains("--smoke-editor-child")) {
        auto const index=app.arguments().indexOf("--smoke-editor-child");
        if(index+1>=app.arguments().size())return 42;
        QFile marker(app.arguments().at(index+1));
        if(!marker.open(QIODevice::WriteOnly|QIODevice::Truncate))return 43;
        marker.write(qgetenv("NORTHSTARCPP_RENDER_PROVIDER"));
        marker.write("\n");
        marker.write(qgetenv("NORTHSTARCPP_PHYSICS_PROVIDER"));
        marker.write("\n");
        marker.close();
        return 0; // Detached Editor child, no actual project launch.
    }
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
        northstarcpp::editor::installMenuTheme(
            app,northstarcpp::editor::MenuTheme::Light);
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
