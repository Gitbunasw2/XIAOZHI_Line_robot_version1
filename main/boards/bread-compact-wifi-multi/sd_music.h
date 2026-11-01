// Phat nhac MP3 tu the nho bang giong noi ("mo bai ...", "phat nhac", "tat nhac").
// Duong di: MP3 (the nho) -> giai ma -> mono -> 24 kHz -> nen Opus 60 ms -> AudioService
// (cung hang doi phat voi giong AI, nen am luong / loa / codec dung chung).
// Nhac chi phat khi robot RANH (idle): bam nut hoac goi ten robot se dung nhac.
// File dat o /sdcard hoac /sdcard/music, ten file nen KHONG DAU (vd: "Son Tung - Lac Troi.mp3").
#ifndef _MULTI_SD_MUSIC_H_
#define _MULTI_SD_MUSIC_H_

#include "application.h"
#include "board.h"
#include "device_state.h"
#include "display.h"
#include "mcp_server.h"
#include "oled_canvas.h"

#include <dirent.h>
#include <esp_ae_rate_cvt.h>
#include <esp_audio_types.h>
#include <esp_log.h>
#include <esp_mp3_dec.h>
#include <esp_opus_enc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class SdMusicPlayer {
public:
    // root: thu muc goc the nho ("/sdcard"). on_state(true/false): bao dang phat (OLED, giu khong ngu)
    explicit SdMusicPlayer(std::string root, std::function<void(bool)> on_state = nullptr)
        : root_(std::move(root)), on_state_(std::move(on_state)) {
        AddTools();
    }

    bool IsPlaying() const { return playing_; }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        pending_ = false;
    }

private:
    static constexpr const char* TAG = "SdMusic";
    static constexpr int kSampleRate = 24000;   // = AUDIO_OUTPUT_SAMPLE_RATE, khong phai doi tan so lan 2
    static constexpr int kFrameMs = 60;
    static constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;  // 1440

    std::string root_;
    std::function<void(bool)> on_state_;
    std::mutex mutex_;
    std::vector<std::string> list_;    // danh sach dang phat (ca the nho)
    size_t index_ = 0;                 // bai hien tai trong list_
    bool pending_ = false;             // con bai can phat
    std::atomic<bool> stop_{false};
    std::atomic<bool> skip_{false};
    std::atomic<bool> playing_{false};
    TaskHandle_t task_ = nullptr;

    // ---------- danh sach bai ----------
    static bool IsMp3(const std::string& n) {
        if (n.size() < 5 || n[0] == '.') return false;  // bo file an "._xxx" cua macOS
        std::string ext = n.substr(n.size() - 4);
        for (auto& ch : ext) ch = (char)tolower((unsigned char)ch);
        return ext == ".mp3";
    }

    std::vector<std::string> ListSongs() const {
        std::vector<std::string> out;
        for (const char* sub : {"", "/music", "/nhac"}) {
            std::string dir = root_ + sub;
            DIR* d = opendir(dir.c_str());
            if (d == nullptr) continue;
            while (auto* e = readdir(d)) {
                if (e->d_type != DT_DIR && IsMp3(e->d_name)) out.push_back(dir + "/" + e->d_name);
            }
            closedir(d);
        }
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        });
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    static std::string Title(const std::string& path) {
        size_t s = path.find_last_of('/');
        std::string n = path.substr(s == std::string::npos ? 0 : s + 1);
        return n.size() > 4 ? n.substr(0, n.size() - 4) : n;
    }

    // chu thuong, bo dau, chi giu chu/so -> so khop ten bai
    static std::string Key(const std::string& s) {
        std::string a = oled::Utf8ToAscii(s.c_str()), k;
        for (char ch : a)
            if (isalnum((unsigned char)ch)) k += (char)tolower((unsigned char)ch);
        return k;
    }

    // Tim bai theo so thu tu ("3", "bai 3") hoac ten (gan dung, khong dau)
    static int FindSong(const std::vector<std::string>& songs, const std::string& query) {
        std::string q = Key(query);
        if (q.empty()) return -1;
        std::string num = q.rfind("bai", 0) == 0 ? q.substr(3) : q;
        if (!num.empty() && std::all_of(num.begin(), num.end(), [](char ch) { return isdigit((unsigned char)ch); })) {
            int n = atoi(num.c_str());
            if (n >= 1 && n <= (int)songs.size()) return n - 1;
        }
        for (size_t i = 0; i < songs.size(); i++)
            if (Key(Title(songs[i])) == q) return (int)i;
        for (size_t i = 0; i < songs.size(); i++)
            if (Key(Title(songs[i])).find(q) != std::string::npos) return (int)i;
        // khop nhieu chu nhat (moi tu >= 2 ky tu)
        int best = -1, best_score = 0;
        std::string a = oled::Utf8ToAscii(query.c_str());
        for (size_t i = 0; i < songs.size(); i++) {
            std::string t = Key(Title(songs[i]));
            int score = 0;
            size_t p = 0;
            while (p < a.size()) {
                size_t e = a.find(' ', p);
                if (e == std::string::npos) e = a.size();
                std::string w = Key(a.substr(p, e - p));
                if (w.size() >= 2 && t.find(w) != std::string::npos) score += (int)w.size();
                p = e + 1;
            }
            if (score > best_score) {
                best_score = score;
                best = (int)i;
            }
        }
        return best;
    }

    // ---------- lenh giong noi ----------
    void AddTools() {
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool("self.music.list",
                    "Liet ke cac bai hat MP3 tren the nho. List the MP3 songs on the SD card. "
                    "Dung khi nguoi dung hoi 'co bai nao', 'danh sach nhac'.",
                    PropertyList(), [this](const PropertyList&) -> ReturnValue {
                        auto songs = ListSongs();
                        if (songs.empty()) return std::string("The nho khong co bai MP3 nao (hoac chua cam the)");
                        std::string r = std::to_string(songs.size()) + " bai: ";
                        for (size_t i = 0; i < songs.size() && i < 60; i++) {
                            if (i) r += "; ";
                            r += std::to_string(i + 1) + ". " + Title(songs[i]);
                        }
                        return r;
                    });

        mcp.AddTool("self.music.play",
                    "Phat nhac MP3 tu the nho: mo nhac, phat nhac, mo bai ..., hat bai ..., nghe nhac. "
                    "Play music from the SD card. `name`: ten bai (gan dung) hoac so thu tu; de trong = phat "
                    "tat ca tu dau. Nhac bat dau sau khi ban noi xong, hay tra loi that ngan.",
                    PropertyList({Property("name", kPropertyTypeString, std::string(""))}),
                    [this](const PropertyList& props) -> ReturnValue {
                        auto songs = ListSongs();
                        if (songs.empty()) return std::string("Khong co bai MP3 nao tren the nho");
                        std::string name = props["name"].value<std::string>();
                        int i = 0;
                        if (!name.empty()) {
                            i = FindSong(songs, name);
                            if (i < 0) return "Khong tim thay bai '" + name + "'. Hay goi self.music.list de xem ten bai";
                        }
                        Start(songs, i);
                        return "Se phat: " + Title(songs[i]);
                    });

        mcp.AddTool("self.music.next", "Bai tiep theo, chuyen bai, qua bai, next song.", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue { return Jump(1); });

        mcp.AddTool("self.music.previous", "Bai truoc, quay lai bai truoc, previous song.", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue { return Jump(-1); });

        mcp.AddTool("self.music.resume", "Tiep tuc nghe nhac, phat lai bai vua nghe, resume music.", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue { return Jump(0); });

        mcp.AddTool("self.music.stop", "Tat nhac, dung nhac, stop music.", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        Stop();
                        return true;
                    });
    }

    ReturnValue Jump(int delta) {
        std::vector<std::string> list;
        int i;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (list_.empty()) return std::string("Chua phat bai nao. Hay dung self.music.play");
            list = list_;
            i = ((int)index_ + delta + (int)list.size()) % (int)list.size();
        }
        Start(list, i);
        return "Se phat: " + Title(list[i]);
    }

    void Start(const std::vector<std::string>& list, int index) {
        std::lock_guard<std::mutex> lock(mutex_);
        list_ = list;
        index_ = index;
        pending_ = true;
        stop_ = false;
        skip_ = true;  // dang phat bai khac -> chuyen bai
        if (task_ == nullptr) {
            // nen Opus can stack lon (giong task opus_codec cua AudioService)
            if (xTaskCreate([](void* arg) { static_cast<SdMusicPlayer*>(arg)->Worker(); }, "sd_music", 2048 * 12, this,
                            2, &task_) != pdPASS) {
                task_ = nullptr;
                pending_ = false;
                ESP_LOGE(TAG, "Khong tao duoc task phat nhac");
            }
        }
    }

    static void SetMessage(const std::string& text) {
        Application::GetInstance().Schedule([text]() {
            auto display = Board::GetInstance().GetDisplay();
            if (display) display->SetChatMessage("system", text.c_str());
        });
    }

    // Cho AI noi xong cau tra loi, dong phien hoi thoai -> idle. Tra ve false neu khong ranh duoc.
    bool WaitIdle() {
        auto& app = Application::GetInstance();
        auto state = [&app]() { return app.GetDeviceState(); };
        // 1) cho bat dau noi (toi da 5 s) roi noi xong (toi da 60 s)
        for (int i = 0; i < 50 && state() != kDeviceStateSpeaking && state() != kDeviceStateIdle; i++)
            vTaskDelay(pdMS_TO_TICKS(100));
        for (int i = 0; i < 600 && (state() == kDeviceStateSpeaking || state() == kDeviceStateConnecting); i++)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (stop_) return false;
        // 2) dang nghe tiep (che do hoi thoai lien tuc) -> dong phien
        if (state() == kDeviceStateListening) {
            app.ToggleChatState();
            for (int i = 0; i < 50 && state() != kDeviceStateIdle; i++) vTaskDelay(pdMS_TO_TICKS(100));
        }
        // 3) cho loa phat het cau cuoi
        for (int i = 0; i < 30 && !app.GetAudioService().IsPlaybackIdle(); i++) vTaskDelay(pdMS_TO_TICKS(100));
        return state() == kDeviceStateIdle;
    }

    bool Interrupted() {
        return stop_ || skip_ || Application::GetInstance().GetDeviceState() != kDeviceStateIdle;
    }

    void SetPlaying(bool on) {
        if (playing_ == on) return;
        playing_ = on;
        if (!on) SetMessage("");
        if (on_state_) on_state_(on);
    }

    void Worker() {
        auto& app = Application::GetInstance();
        while (true) {
            std::string path;
            bool has_song;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                has_song = !stop_ && pending_ && index_ < list_.size();
                if (has_song) {
                    path = list_[index_];
                    skip_ = false;
                }
            }
            if (!has_song) {
                SetPlaying(false);
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stop_ && pending_ && index_ < list_.size()) continue;  // vua co lenh phat moi
                pending_ = false;
                task_ = nullptr;  // Start() sau do se tao task moi
                break;
            }
            if (!playing_) {
                if (!WaitIdle()) {
                    ESP_LOGW(TAG, "Robot dang ban, huy phat nhac");
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_ = false;
                    continue;
                }
                SetPlaying(true);
            }
            SetMessage("Nhac: " + Title(path));
            bool done = PlayFile(path);
            bool by_user = !done && !stop_ && !skip_;  // nguoi dung bam nut / goi robot -> tam dung
            // Chuyen bai / tat nhac khi dang ranh: bo phan nhac con trong hang doi.
            // (Dang hoi thoai thi KHONG xoa, tranh cat giong AI.)
            if ((stop_ || skip_) && app.GetDeviceState() == kDeviceStateIdle) app.GetAudioService().ResetDecoder();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (done && index_ < list_.size() && list_[index_] == path) index_++;  // het bai -> bai sau
                if (by_user) pending_ = false;  // giu list_/index_ de "tiep tuc" / "bai tiep"
            }
            if (by_user || app.GetDeviceState() != kDeviceStateIdle) SetPlaying(false);
        }
        vTaskDelete(nullptr);
    }

    // Phat 1 file. Tra ve true neu het bai, false neu bi ngat / loi.
    bool PlayFile(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (f == nullptr) {
            ESP_LOGE(TAG, "Khong mo duoc %s", path.c_str());
            return true;  // bo qua bai loi, phat bai sau
        }
        ESP_LOGI(TAG, "Phat %s", path.c_str());
        // bo the ID3v2 (anh bia...) o dau file
        uint8_t hdr[10];
        if (fread(hdr, 1, 10, f) == 10 && memcmp(hdr, "ID3", 3) == 0) {
            long sz = ((hdr[6] & 0x7F) << 21) | ((hdr[7] & 0x7F) << 14) | ((hdr[8] & 0x7F) << 7) | (hdr[9] & 0x7F);
            fseek(f, 10 + sz + ((hdr[5] & 0x10) ? 10 : 0), SEEK_SET);
        } else {
            fseek(f, 0, SEEK_SET);
        }

        void* mp3 = nullptr;
        void* opus = nullptr;
        esp_ae_rate_cvt_handle_t cvt = nullptr;
        uint32_t cvt_rate = 0;
        bool finished = false;

        esp_opus_enc_config_t ocfg = {};
        ocfg.sample_rate = kSampleRate;
        ocfg.channel = ESP_AUDIO_MONO;
        ocfg.bits_per_sample = ESP_AUDIO_BIT16;
        ocfg.bitrate = 48000;
        ocfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
        ocfg.application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO;
        ocfg.complexity = 0;
        ocfg.enable_fec = false;
        ocfg.enable_dtx = false;
        ocfg.enable_vbr = true;

        const size_t kInSize = 4096;
        std::vector<uint8_t> in(kInSize);
        std::vector<uint8_t> out(1152 * 2 * 2);
        std::vector<int16_t> mono, resampled, pcm;
        std::vector<uint8_t> enc_out;
        int opus_in = 0, opus_out = 0;
        size_t fill = 0, pos = 0;
        bool eof = false;
        int errors = 0;

        if (esp_mp3_dec_open(nullptr, 0, &mp3) != ESP_AUDIO_ERR_OK ||
            esp_opus_enc_open(&ocfg, sizeof(ocfg), &opus) != ESP_AUDIO_ERR_OK ||
            esp_opus_enc_get_frame_size(opus, &opus_in, &opus_out) != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Khong mo duoc bo giai ma MP3 / nen Opus");
            goto done;
        }
        enc_out.resize(opus_out);

        while (!Interrupted()) {
            // nap them du lieu tu the
            if (!eof && fill - pos < 2048) {
                memmove(in.data(), in.data() + pos, fill - pos);
                fill -= pos;
                pos = 0;
                size_t n = fread(in.data() + fill, 1, kInSize - fill, f);
                fill += n;
                if (n == 0) eof = true;
            }
            if (fill - pos == 0) {
                finished = true;
                break;
            }

            esp_audio_dec_in_raw_t raw = {};
            raw.buffer = in.data() + pos;
            raw.len = fill - pos;
            esp_audio_dec_out_frame_t frame = {};
            frame.buffer = out.data();
            frame.len = out.size();
            esp_audio_dec_info_t info = {};
            esp_audio_err_t ret = esp_mp3_dec_decode(mp3, &raw, &frame, &info);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                out.resize(frame.needed_size);
                continue;
            }
            if (ret == ESP_AUDIO_ERR_DATA_LACK) {
                pos += raw.consumed;
                if (eof) {
                    finished = true;
                    break;
                }
                if (raw.consumed == 0 && pos == 0 && fill == kInSize) pos = fill;  // rac khong giai ma duoc
                continue;
            }
            if (ret != ESP_AUDIO_ERR_OK) {
                pos += raw.consumed > 0 ? raw.consumed : 1;  // bo byte loi, tim khung tiep
                if (++errors > 2000) {
                    ESP_LOGE(TAG, "File MP3 hong: %s", path.c_str());
                    finished = true;
                    break;
                }
                continue;
            }
            errors = 0;
            pos += raw.consumed;
            if (frame.decoded_size == 0 || info.channel == 0 || info.sample_rate == 0) continue;

            // stereo -> mono
            int ch = info.channel;
            size_t samples = frame.decoded_size / 2 / ch;
            const int16_t* s = (const int16_t*)out.data();
            mono.resize(samples);
            for (size_t i = 0; i < samples; i++) {
                int acc = 0;
                for (int c = 0; c < ch; c++) acc += s[i * ch + c];
                mono[i] = (int16_t)(acc / ch);
            }

            // doi tan so mau -> 24 kHz
            const std::vector<int16_t>* src = &mono;
            if (info.sample_rate != (uint32_t)kSampleRate) {
                if (cvt == nullptr || cvt_rate != info.sample_rate) {
                    if (cvt) esp_ae_rate_cvt_close(cvt);
                    cvt = nullptr;
                    esp_ae_rate_cvt_cfg_t rc = {};
                    rc.src_rate = info.sample_rate;
                    rc.dest_rate = kSampleRate;
                    rc.channel = 1;
                    rc.bits_per_sample = ESP_AUDIO_BIT16;
                    rc.complexity = 2;
                    rc.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                    if (esp_ae_rate_cvt_open(&rc, &cvt) != ESP_AE_ERR_OK) {
                        ESP_LOGE(TAG, "Khong doi duoc tan so %lu Hz", (unsigned long)info.sample_rate);
                        cvt = nullptr;
                        break;
                    }
                    cvt_rate = info.sample_rate;
                }
                uint32_t out_n = 0;
                esp_ae_rate_cvt_get_max_out_sample_num(cvt, samples, &out_n);
                resampled.resize(out_n);
                esp_ae_rate_cvt_process(cvt, (esp_ae_sample_t)mono.data(), samples,
                                        (esp_ae_sample_t)resampled.data(), &out_n);
                resampled.resize(out_n);
                src = &resampled;
            }
            pcm.insert(pcm.end(), src->begin(), src->end());

            // nen Opus tung khung 60 ms va day vao hang doi phat
            if (!PushFrames(opus, opus_in, pcm, enc_out, false)) break;
        }
        if (finished && !Interrupted()) PushFrames(opus, opus_in, pcm, enc_out, true);  // khung cuoi (dem 0)

    done:
        if (cvt) esp_ae_rate_cvt_close(cvt);
        if (opus) esp_opus_enc_close(opus);
        if (mp3) esp_mp3_dec_close(mp3);
        fclose(f);
        return finished;
    }

    bool PushFrames(void* opus, int in_bytes, std::vector<int16_t>& pcm, std::vector<uint8_t>& enc_out, bool flush) {
        const size_t frame = (size_t)in_bytes / 2;  // = kFrameSamples
        if (flush && !pcm.empty() && pcm.size() < frame) pcm.resize(frame, 0);
        size_t off = 0;
        auto& audio = Application::GetInstance().GetAudioService();
        while (pcm.size() - off >= frame) {
            esp_audio_enc_in_frame_t ein = {};
            ein.buffer = (uint8_t*)(pcm.data() + off);
            ein.len = in_bytes;
            esp_audio_enc_out_frame_t eout = {};
            eout.buffer = enc_out.data();
            eout.len = enc_out.size();
            off += frame;
            if (esp_opus_enc_process(opus, &ein, &eout) != ESP_AUDIO_ERR_OK || eout.encoded_bytes == 0) continue;
            auto packet = std::make_unique<AudioStreamPacket>();
            packet->sample_rate = kSampleRate;
            packet->frame_duration = kFrameMs;
            packet->payload.assign(enc_out.data(), enc_out.data() + eout.encoded_bytes);
            if (Interrupted() || !audio.PushPacketToDecodeQueue(std::move(packet), true)) {
                pcm.erase(pcm.begin(), pcm.begin() + off);
                return false;
            }
        }
        pcm.erase(pcm.begin(), pcm.begin() + off);
        return true;
    }
};

#endif  // _MULTI_SD_MUSIC_H_
