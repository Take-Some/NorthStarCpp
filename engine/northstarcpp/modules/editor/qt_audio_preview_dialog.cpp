// Content Browser audio preview. Uses the SAME NAC decoder and replaceable
// engine.audio provider as runtime; the inspector never edits imported assets.
#include "qt_audio_preview_dialog.hpp"
#include <northstarcpp/audio.hpp>
#include <northstarcpp/nac.hpp>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef NORTHSTARCPP_QT_MULTIMEDIA
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#endif

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
std::uint16_t u16(std::uint8_t const* data) {
    return std::uint16_t(data[0])|std::uint16_t(data[1])<<8;
}
std::uint32_t u32(std::uint8_t const* data) {
    return std::uint32_t(data[0])|(std::uint32_t(data[1])<<8)|
           (std::uint32_t(data[2])<<16)|(std::uint32_t(data[3])<<24);
}
std::vector<std::uint8_t> loadBounded(fs::path const& path) {
    std::error_code ec;
    if(fs::is_symlink(fs::symlink_status(path,ec)) || ec)
        throw std::runtime_error("Symlink audio sources are not allowed");
    auto const size=fs::file_size(path,ec);
    if(ec || size<44 || size>64ull*1024ull*1024ull)
        throw std::runtime_error("Audio file is missing or exceeds 64 MiB");
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot open audio source");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(!in.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(size)))
        throw std::runtime_error("Truncated audio source");
    return bytes;
}
nac::Track loadWave(std::vector<std::uint8_t> const& b, std::string name) {
    if(b.size()<44 || std::memcmp(b.data(),"RIFF",4)!=0 ||
       std::memcmp(b.data()+8,"WAVE",4)!=0 ||
       std::uint64_t(u32(b.data()+4))+8>b.size())
        throw std::runtime_error("Only RIFF/WAVE audio is supported");
    auto const end=std::uint64_t(u32(b.data()+4))+8;
    std::uint32_t rate=0;
    std::uint16_t channels=0,format=0,bits=0;
    std::size_t pcm_offset=0,pcm_length=0;
    bool fmt=false,data=false;
    for(std::uint64_t offset=12;offset+8<=end;) {
        auto const block=std::uint64_t(u32(b.data()+offset+4));
        auto const start=offset+8;
        if(block>end-start)throw std::runtime_error("Truncated WAV chunk");
        if(std::memcmp(b.data()+offset,"fmt ",4)==0) {
            if(fmt || block<16)throw std::runtime_error("Invalid WAV format");
            fmt=true;
            format=u16(b.data()+start);
            channels=u16(b.data()+start+2);
            rate=u32(b.data()+start+4);
            bits=u16(b.data()+start+14);
            if(u16(b.data()+start+12)!=channels*2 ||
               u32(b.data()+start+8)!=rate*channels*2)
                throw std::runtime_error("Inconsistent WAV byte rate");
        } else if(std::memcmp(b.data()+offset,"data",4)==0) {
            if(data)throw std::runtime_error("Duplicate WAV data chunk");
            data=true;pcm_offset=static_cast<std::size_t>(start);
            pcm_length=static_cast<std::size_t>(block);
        }
        offset=start+block+(block&1);
    }
    if(!fmt || !data || !pcm_length || format!=1 || bits!=16 ||
       (channels!=1 && channels!=2) || rate<8000 || rate>192000 ||
       pcm_length%(channels*2))
        throw std::runtime_error("WAV preview requires PCM16 mono/stereo (8-192 kHz)");
    nac::Track result;
    result.name=std::move(name);
    result.channels=channels;result.sample_rate=rate;
    result.samples.resize(pcm_length/2);
    for(std::size_t i=0;i<result.samples.size();++i)
        result.samples[i]=static_cast<std::int16_t>(
            u16(b.data()+pcm_offset+i*2));
    return result;
}
QString clockText(double seconds) {
    int const ms=std::max(0,int(seconds*1000));
    return QString("%1:%2.%3")
        .arg(ms/60000,2,10,QLatin1Char('0'))
        .arg(ms/1000%60,2,10,QLatin1Char('0'))
        .arg(ms%1000/10,2,10,QLatin1Char('0'));
}
class Waveform final:public QWidget {
public:
    explicit Waveform(QWidget* parent=nullptr):QWidget(parent) {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }
    void load(nac::Track const& track) {
        peaks_.assign(512,0);
        std::size_t const frames=track.samples.size()/track.channels;
        for(std::size_t x=0;x<peaks_.size();++x) {
            auto const from=x*frames/peaks_.size();
            auto const to=std::min(frames,(x+1)*frames/peaks_.size());
            int peak=0;
            for(auto i=from;i<to;++i)
                for(unsigned c=0;c<track.channels;++c)
                    peak=std::max(peak,std::abs(int(track.samples[i*track.channels+c])));
            peaks_[x]=static_cast<float>(peak/32768.0);
        }
        progress_=0;update();
    }
    void position(double fraction) {
        progress_=std::clamp(fraction,0.,1.);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),QColor("#151f2c"));
        painter.setPen(QColor("#304255"));
        painter.drawLine(0,height()/2,width(),height()/2);
        for(std::size_t i=0;i<peaks_.size();++i) {
            int const x=int(i*std::size_t(width())/peaks_.size());
            int const h=int(peaks_[i]*(height()-20)*.5);
            painter.setPen(QColor(double(i)/peaks_.size()<=progress_?
                "#f4bd59":"#71bed9"));
            painter.drawLine(x,height()/2-h,x,height()/2+h);
        }
    }
private:
    std::vector<float> peaks_;
    double progress_=0;
};
class AudioDialog final:public QDialog {
public:
    AudioDialog(QWidget* parent,fs::path const& path,std::string const& reference)
        :QDialog(parent),path_(path) {
        setObjectName("northstarAudioPreviewDialog");
        setWindowTitle("NorthStar Audio Preview");
        resize(710,430);
        setStyleSheet(
            "QDialog{background:#161f2a;color:#dbe7f4}"
            "QLabel{color:#c4d6e8}"
            "QComboBox,QSlider{background:#253344;color:#e9f4ff}"
            "QPushButton{background:#2e465c;color:#f5faff;"
            "border:1px solid #496b83;padding:8px;border-radius:5px}"
            "QPushButton:hover{background:#426587}"
            "QPushButton:disabled{color:#738699;background:#253242}");
        auto* layout=new QVBoxLayout(this);
        layout->setSpacing(12);
        auto* title=new QLabel(QString::fromStdString(reference),this);
        title->setWordWrap(true);
        title->setObjectName("audioPreviewReference");
        layout->addWidget(title);
        selector_=new QComboBox(this);
        selector_->setObjectName("audioPreviewTrackSelector");
        layout->addWidget(selector_);
        metadata_=new QLabel(this);
        metadata_->setObjectName("audioPreviewMetadata");
        layout->addWidget(metadata_);
        wave_=new Waveform(this);
        wave_->setObjectName("audioPreviewWaveform");
        layout->addWidget(wave_,1);
        auto* controls=new QHBoxLayout;
        play_=new QPushButton("Play",this);
        play_->setObjectName("audioPreviewPlay");
        stop_=new QPushButton("Stop",this);
        stop_->setObjectName("audioPreviewStop");
        controls->addWidget(play_);
        controls->addWidget(stop_);
        clock_=new QLabel("00:00.00 / 00:00.00",this);
        clock_->setObjectName("audioPreviewClock");
        controls->addWidget(clock_,1);
        layout->addLayout(controls);
        status_=new QLabel(this);
        status_->setWordWrap(true);
        status_->setObjectName("audioPreviewStatus");
        layout->addWidget(status_);
        timer_.setInterval(33);
        QObject::connect(&timer_,&QTimer::timeout,this,[this]{
            if(!active_ || selector_->currentIndex()<0)return;
            auto const& clip=tracks_[std::size_t(selector_->currentIndex())];
            auto const duration=double(clip.samples.size())/
                                  (clip.channels*clip.sample_rate);
            auto const elapsed=std::min(duration,
                double(watch_.elapsed())/1000.);
            wave_->position(duration>0?elapsed/duration:0);
            clock_->setText(clockText(elapsed)+" / "+clockText(duration));
            if(elapsed>=duration)stop();
        });
        QObject::connect(play_,&QPushButton::clicked,this,[this]{play();});
        QObject::connect(stop_,&QPushButton::clicked,this,[this]{stop();});
        QObject::connect(selector_,QOverload<int>::of(&QComboBox::currentIndexChanged),
                         this,[this](int i){stop();select(i);});
        try { load(reference); }
        catch(std::exception const& e) {
            selector_->setEnabled(false);
            play_->setEnabled(false);
            status_->setText("Audio preview unavailable: "+QString::fromUtf8(e.what()));
        }
    }
    ~AudioDialog() override {stop();}
private:
    void load(std::string const& reference) {
        auto const ext=path_.extension().u8string();
        if(ext==".nac") {
            auto const bytes=loadBounded(path_);
            std::string error;
            if(!nac::decode(bytes,tracks_,error))
                throw std::runtime_error("Invalid NEF8/NAC: "+error);
        } else if(ext==".wav") {
            tracks_.push_back(loadWave(loadBounded(path_),
                                       path_.stem().u8string()));
        } else {
#ifdef NORTHSTARCPP_QT_MULTIMEDIA
            // Qt Multimedia is optional. Use it only for compressed formats
            // not handled by the native PCM16/NEF8 codec.
            if(ext!=".ogg" && ext!=".mp3" && ext!=".flac")
                throw std::runtime_error("Audio codec unavailable");
            multimedia_=std::make_unique<QMediaPlayer>();
            output_=std::make_unique<QAudioOutput>();
            multimedia_->setAudioOutput(output_.get());
            multimedia_->setSource(QUrl::fromLocalFile(
                QString::fromStdWString(path_.wstring())));
            selector_->addItem(QString::fromStdWString(path_.filename().wstring()));
            selector_->setEnabled(false);
            metadata_->setText("Compressed audio (Qt Multimedia)");
            status_->setText("Ready for playback");
            return;
#else
            throw std::runtime_error("Unsupported codec (requires Qt Multimedia)");
#endif
        }
        if(tracks_.empty())throw std::runtime_error("No audio tracks");
        if(tracks_.size()>1024)throw std::runtime_error("Too many tracks");
        std::size_t initial=0;
        auto const separator=reference.find('@');
        if(separator!=std::string::npos) {
            auto const item=reference.substr(separator+1);
            if(item.rfind("track-",0)!=0)
                throw std::runtime_error("Invalid NAC track reference");
            std::size_t offset=0;
            auto const id=std::stoul(item.substr(6),&offset);
            if(offset!=item.size()-6)
                throw std::runtime_error("Malformed NAC track ID");
            auto const it=std::find_if(tracks_.begin(),tracks_.end(),
                [id](auto const& track){return track.id==id;});
            if(it==tracks_.end())
                throw std::runtime_error("NAC track ID not present");
            initial=std::size_t(it-tracks_.begin());
        }
        for(auto const& clip:tracks_)
            selector_->addItem(QString::fromStdString(clip.name));
        selector_->setCurrentIndex(static_cast<int>(initial));
        select(static_cast<int>(initial));
        status_->setText("Ready. Playback uses the active engine.audio provider.");
    }
    void select(int index) {
#ifdef NORTHSTARCPP_QT_MULTIMEDIA
        if(multimedia_)return;
#endif
        if(index<0 || std::size_t(index)>=tracks_.size())return;
        auto const& clip=tracks_[std::size_t(index)];
        auto const duration=double(clip.samples.size())/
                              (clip.channels*clip.sample_rate);
        metadata_->setText(QString("%1 Hz  |  %2 channel(s)  |  PCM16  |  %3 frames")
            .arg(clip.sample_rate).arg(clip.channels)
            .arg(clip.samples.size()/clip.channels));
        clock_->setText(clockText(0)+" / "+clockText(duration));
        wave_->load(clip);
    }
    void play() {
        stop();
        try {
#ifdef NORTHSTARCPP_QT_MULTIMEDIA
            if(multimedia_) {
                multimedia_->play();
                active_=true;status_->setText("Playing compressed audio");
                return;
            }
#endif
            if(selector_->currentIndex()<0 ||
               std::size_t(selector_->currentIndex())>=tracks_.size())return;
            if(!audio_)audio_=std::make_unique<AudioEngine>();
            auto const& clip=tracks_[std::size_t(selector_->currentIndex())];
            NorthStarPcm16 const pcm{clip.samples.data(),
                static_cast<std::uint32_t>(clip.samples.size()/clip.channels),
                clip.sample_rate,clip.channels};
            if(audio_->supports_voices()) {
                NorthStarAudioVoiceOptions options{
                    sizeof(NorthStarAudioVoiceOptions),1.f,0,0,{0,0,0}};
                voice_=audio_->play_voice(pcm,options);
            } else audio_->play(pcm);
            active_=true;watch_.start();timer_.start();
            status_->setText("Playing via "+QString::fromStdString(audio_->provider_name()));
        } catch(std::exception const& e) {
            status_->setText("Playback error: "+QString::fromUtf8(e.what()));
        }
    }
    void stop() {
        timer_.stop();
        if(audio_) {
            try {
                if(voice_)audio_->stop_voice(voice_);
                else if(active_)audio_->stop();
            } catch(std::exception const& e) {
                status_->setText("Audio stop: "+QString::fromUtf8(e.what()));
            }
        }
#ifdef NORTHSTARCPP_QT_MULTIMEDIA
        if(multimedia_)multimedia_->stop();
#endif
        voice_=0;active_=false;
    }
    fs::path path_;
    std::vector<nac::Track> tracks_;
    std::unique_ptr<AudioEngine> audio_;
    NorthStarAudioVoiceId voice_=0;
#ifdef NORTHSTARCPP_QT_MULTIMEDIA
    std::unique_ptr<QMediaPlayer> multimedia_;
    std::unique_ptr<QAudioOutput> output_;
#endif
    QComboBox* selector_{};
    QLabel* metadata_{};
    QLabel* status_{};
    QLabel* clock_{};
    QPushButton* play_{};
    QPushButton* stop_{};
    Waveform* wave_{};
    QElapsedTimer watch_;
    QTimer timer_;
    bool active_=false;
};
}
int openAudioPreviewModal(QWidget* parent,fs::path const& physical,
                          std::string const& reference) {
    AudioDialog dialog(parent,physical,reference);
    return dialog.exec();
}
} // namespace northstarcpp::editor
