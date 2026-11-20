// Hit and kill sounds. Detection listens for the game's own hit / kill feedback (see below).
// Playback is WAV files from <dll folder>\sounds (16-bit PCM, one folder for both pickers),
// scaled to the volume in memory.
#include "features.h"
#include "../core/config.h"
#include "../sdk/il2cpp.h"
#include "MinHook.h"
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace hitsound {
namespace {
    std::wstring Dir(bool) { return DllDir() + L"\\sounds"; } // shared: any sound can be a hit or a kill sound
    const char* Fallback(bool kill) { return kill ? "fatality.wav" : "bameware.wav"; }

    // names come from the config file, so refuse anything that could leave the folder
    bool SafeName(const std::string& n) {
        return !n.empty() && n.size() < 200 && n.find_first_of("\\/:") == std::string::npos && n.find("..") == std::string::npos;
    }

    std::wstring Wide(const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    }
    std::string Narrow(const std::wstring& w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
        return s;
    }

    // Reads a 16-bit PCM WAV into `out` with the sample data scaled by `vol`.
    bool Load(const std::wstring& path, float vol, std::vector<uint8_t>& out, std::string* why = nullptr) {
        auto fail = [&](const char* m) { if (why) *why = m; return false; };
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return fail("could not open the file");
        LARGE_INTEGER sz{};
        GetFileSizeEx(h, &sz);
        if (sz.QuadPart < 44 || sz.QuadPart > 8 * 1024 * 1024) { CloseHandle(h); return fail("file is empty or larger than 8 MB"); }
        out.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
        CloseHandle(h);
        if (!ok || got != out.size()) return fail("read failed");
        if (memcmp(out.data(), "RIFF", 4) || memcmp(out.data() + 8, "WAVE", 4)) return fail("not a WAV file");
        bool pcm16 = false;
        size_t pos = 12;
        while (pos + 8 <= out.size()) {
            const uint32_t len = *reinterpret_cast<uint32_t*>(&out[pos + 4]);
            const size_t body = pos + 8;
            if (!memcmp(&out[pos], "fmt ", 4) && body + 16 <= out.size()) {
                const uint16_t fmt = *reinterpret_cast<uint16_t*>(&out[body]);
                const uint16_t bits = *reinterpret_cast<uint16_t*>(&out[body + 14]);
                pcm16 = fmt == 1 && bits == 16;
            } else if (!memcmp(&out[pos], "data", 4)) {
                if (!pcm16) return fail("only 16-bit PCM WAV is supported; convert it first");
                const size_t n = std::min<size_t>(len, out.size() - body) / 2;
                int16_t* s = reinterpret_cast<int16_t*>(&out[body]);
                for (size_t i = 0; i < n; i++) s[i] = static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, s[i] * vol)));
                return true;
            }
            pos = body + len + (len & 1);
        }
        return fail("no audio data in the file");
    }

    // Every sound is its own waveOut voice on its own short-lived thread. PlaySound has one stream: each
    // new hit restarted the previous sound, and a hit right after a kill cut the kill sound off. Voices
    // overlap, so a spray of hits layers up and a kill is always heard in full.
    std::atomic<int> g_voices{ 0 };
    std::atomic<bool> g_stop{ false };

    // The format and sample data of an already validated 16-bit PCM WAV held in memory.
    bool ParseWav(const std::vector<uint8_t>& b, WAVEFORMATEX& f, size_t& dataOff, DWORD& dataLen) {
        bool haveFmt = false;
        size_t pos = 12;
        while (pos + 8 <= b.size()) {
            const uint32_t len = *reinterpret_cast<const uint32_t*>(&b[pos + 4]);
            const size_t body = pos + 8;
            if (!memcmp(&b[pos], "fmt ", 4) && body + 16 <= b.size()) {
                f = {};
                f.wFormatTag = *reinterpret_cast<const uint16_t*>(&b[body]);
                f.nChannels = *reinterpret_cast<const uint16_t*>(&b[body + 2]);
                f.nSamplesPerSec = *reinterpret_cast<const uint32_t*>(&b[body + 4]);
                f.nAvgBytesPerSec = *reinterpret_cast<const uint32_t*>(&b[body + 8]);
                f.nBlockAlign = *reinterpret_cast<const uint16_t*>(&b[body + 12]);
                f.wBitsPerSample = *reinterpret_cast<const uint16_t*>(&b[body + 14]);
                haveFmt = true;
            } else if (!memcmp(&b[pos], "data", 4)) {
                dataOff = body;
                dataLen = static_cast<DWORD>(std::min<size_t>(len, b.size() - body));
                return haveFmt;
            }
            pos = body + len + (len & 1);
        }
        return false;
    }

    void Voice(std::vector<uint8_t> buf) {
        WAVEFORMATEX f{};
        size_t off = 0;
        DWORD len = 0;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HWAVEOUT h = nullptr;
        if (ev && ParseWav(buf, f, off, len) && waveOutOpen(&h, WAVE_MAPPER, &f, reinterpret_cast<DWORD_PTR>(ev), 0, CALLBACK_EVENT) == MMSYSERR_NOERROR) {
            WAVEHDR hdr{};
            hdr.lpData = reinterpret_cast<LPSTR>(buf.data() + off);
            hdr.dwBufferLength = len;
            if (waveOutPrepareHeader(h, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
                if (waveOutWrite(h, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
                    const DWORD limit = GetTickCount() + 2000 + static_cast<DWORD>(len * 1000ull / std::max<DWORD>(1, f.nAvgBytesPerSec));
                    while (!(reinterpret_cast<volatile WAVEHDR&>(hdr).dwFlags & WHDR_DONE) && !g_stop && GetTickCount() < limit)
                        WaitForSingleObject(ev, 30);
                    if (!(reinterpret_cast<volatile WAVEHDR&>(hdr).dwFlags & WHDR_DONE)) waveOutReset(h);
                }
                waveOutUnprepareHeader(h, &hdr, sizeof(hdr));
            }
            waveOutClose(h);
        }
        if (ev) CloseHandle(ev);
        g_voices--;
    }

    // The decoded, volume-scaled file is kept for the next hit: reading and scaling 300 KB on every
    // hit of a spray is wasted work.
    struct Cached { std::string name; float vol = -1.f; std::vector<uint8_t> data; };
    Cached g_cache[2];

    void PlayFile(bool kill, const std::string& name, float vol) {
        Cached& c = g_cache[kill ? 1 : 0];
        const float v = std::max(0.f, std::min(vol, 1.f));
        if (c.vol != v || c.name != name || c.data.empty()) {
            // a saved name whose file was deleted or renamed falls back to the bundled default
            std::vector<uint8_t> b;
            const bool ok = (SafeName(name) && Load(Dir(kill) + L"\\" + Wide(name), v, b)) ||
                            Load(Dir(kill) + L"\\" + Wide(Fallback(kill)), v, b);
            if (!ok) return;
            c.name = name; c.vol = v; c.data = std::move(b);
        }
        if (g_voices >= 8 || g_stop) return;
        g_voices++;
        std::thread(Voice, c.data).detach(); // each voice owns a copy; the cache stays ready
    }

    // ---- detection -----------------------------------------------------------------
    // Nothing on your side can see a shot register by itself: the victim's client owns the damage, and
    // a ray that ends in an enemy is not a hit (the game casts rays for aim and effects too). What the
    // game does tell you is its own feedback: WeaponDataScriptableObject holds the clips it plays when
    // your shot lands or kills (ShootToEnemyClip, KillEnemyClip and the headshot pair). Hooks on
    // AudioSource playback catch those clips and play yours instead. Fallbacks, used only while no
    // game clip has ever been seen: ApplyDamage on an enemy, and a kill inferred from an enemy that
    // dies within 2 s of you shooting at it.
    enum { C_HIT, C_KILL, C_HEAD_HIT, C_HEAD_KILL };
    void* g_clip[4]{};                  // refreshed on the game thread, so the hooks only compare pointers
    int g_clipOff[4] = { -1, -1, -1, -1 };
    void* g_fnInstance = nullptr;       // WeaponDataScriptableObject.get_Instance
    void* g_fnName = nullptr;           // Object.get_name
    void* g_fnGetClip = nullptr;        // AudioSource.get_clip
    std::atomic<int> g_gameHit{ 0 }, g_gameKill{ 0 }, g_clipCalls{ 0 }, g_dmgSeen{ 0 };

    struct Note { void* obj; bool hit; }; // hit=false: only "you were shooting at it"
    std::mutex g_noteMtx;
    std::vector<Note> g_notes;
    std::unordered_map<void*, bool> g_prev;      // enemy -> was alive last frame
    std::unordered_map<void*, DWORD> g_hitOn;    // enemy -> when you last hit or shot at it
    DWORD g_lastHit = 0, g_lastKill = 0;
    int g_logged = 0;

    void* Call0(void* fn, void* self) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fn)(self, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    void* ReadPtr(void* base, int off) {
        __try { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(base) + off); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    using One1 = void(__fastcall*)(void*, void*, void*);
    using One2 = void(__fastcall*)(void*, void*, float, void*);
    using Play0 = void(__fastcall*)(void*, void*);
    One1 oOne1 = nullptr;
    One2 oOne2 = nullptr;
    Play0 oPlay = nullptr;
    std::unordered_map<void*, bool> g_seenClip; // game thread only
    int g_clipLog = 0;

    // true = swallow the game's own sound, yours replaces it
    bool OnClip(void* clip) {
        g_clipCalls++;
        for (int i = 0; i < 4; i++) {
            if (!clip || clip != g_clip[i]) continue;
            const bool kill = i == C_KILL || i == C_HEAD_KILL;
            if (kill) g_gameKill++; else g_gameHit++;
            return kill ? cfg.sound.kill : cfg.sound.hit;
        }
        return false;
    }
    // Names of what the game plays while you hold fire: if the pointers above ever miss, this shows which clip is the feedback.
    void LogClip(void* clip) {
        if (g_clipLog >= 40 || !clip || !g_fnName || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) || g_seenClip.count(clip)) return;
        g_seenClip[clip] = true;
        g_clipLog++;
        Log("[sound] while firing the game plays '%s'", il2::Str(Call0(g_fnName, clip)).c_str());
    }
    thread_local int audioDepth = 0;
    struct AudioScope {
        bool outer = ++audioDepth == 1;
        ~AudioScope() { --audioDepth; }
    };
    void __fastcall hkOne1(void* self, void* clip, void* mi) { AudioScope scope; if (scope.outer && OnClip(clip)) return; oOne1(self, clip, mi); if (scope.outer) LogClip(clip); }
    void __fastcall hkOne2(void* self, void* clip, float vol, void* mi) { AudioScope scope; if (scope.outer && OnClip(clip)) return; oOne2(self, clip, vol, mi); if (scope.outer) LogClip(clip); }
    void __fastcall hkPlay(void* self, void* mi) {
        AudioScope scope;
        void* clip = g_fnGetClip ? Call0(g_fnGetClip, self) : nullptr;
        if (scope.outer && OnClip(clip)) return;
        oPlay(self, mi);
        if (scope.outer) LogClip(clip);
    }

    void HookAudio(void* cls, const char* name, int argc, void* detour, void** orig) {
        void* m = il2::MethodPtr(cls, name, argc);
        const MH_STATUS s = m ? MH_CreateHook(m, detour, orig) : MH_ERROR_NOT_CREATED;
        Log("[sound] AudioSource.%s/%d hook=%p create=%d enable=%d", name, argc, m, s, s == MH_OK ? MH_EnableHook(m) : -1);
    }
}

// Game thread, after il2cpp and MinHook are up.
void Init() {
    void* wd = il2::Class("WeaponDataScriptableObject");
    g_fnInstance = wd ? il2::MethodPtr(wd, "get_Instance", 0) : nullptr;
    const char* fields[4] = { "ShootToEnemyClip", "KillEnemyClip", "HeadshotHitClip", "HeadshotKillClip" };
    for (int i = 0; i < 4; i++) g_clipOff[i] = wd ? il2::FieldOffset(wd, fields[i]) : -1;
    g_fnName = il2::MethodPtr(il2::Class("Object", "UnityEngine"), "get_name", 0);
    void* as = il2::Class("AudioSource", "UnityEngine");
    g_fnGetClip = il2::MethodPtr(as, "get_clip", 0);
    Log("[sound] game clips: instance=%p offsets %d/%d/%d/%d", g_fnInstance, g_clipOff[0], g_clipOff[1], g_clipOff[2], g_clipOff[3]);
    HookAudio(as, "PlayOneShot", 1, &hkOne1, reinterpret_cast<void**>(&oOne1));
    HookAudio(as, "PlayOneShot", 2, &hkOne2, reinterpret_cast<void**>(&oOne2));
    HookAudio(as, "Play", 0, &hkPlay, reinterpret_cast<void**>(&oPlay));
}

// Game thread, once per frame: keeps the game's clip pointers current.
void Tick() {
    static DWORD t = 0;
    const DWORD now = GetTickCount();
    if (now - t < 500 || !g_fnInstance) return;
    t = now;
    void* so = Call0(g_fnInstance, nullptr);
    if (!Ok(so)) return;
    static bool logged = false;
    for (int i = 0; i < 4; i++) g_clip[i] = g_clipOff[i] > 0 ? ReadPtr(so, g_clipOff[i]) : nullptr;
    if (!logged && g_clip[0]) {
        logged = true;
        for (int i = 0; i < 4; i++)
            Log("[sound] game clip %d = %p '%s'", i, g_clip[i], g_clip[i] && g_fnName ? il2::Str(Call0(g_fnName, g_clip[i])).c_str() : "-");
    }
}

void Stats(int& clipCalls, int& hitClips, int& killClips) { clipCalls = g_clipCalls; hitClips = g_gameHit; killClips = g_gameKill; }

// Unload: voices run on their own threads inside this DLL, so wait for them before it goes away.
void Shutdown() {
    g_stop = true;
    for (int i = 0; i < 150 && g_voices > 0; i++) Sleep(20);
    Sleep(50); // a finished voice is still returning from its thread
}

static void Push(void* enemy, bool hit) {
    std::lock_guard<std::mutex> g(g_noteMtx);
    if (g_notes.size() < 64) g_notes.push_back({ enemy, hit });
}
void NoteHit(void* enemy) { g_dmgSeen++; Push(enemy, true); }
void NoteTarget(void* enemy) { Push(enemy, false); }

void Frame() {
    if (!cfg.sound.hit && !cfg.sound.kill) {
        g_prev.clear();
        g_hitOn.clear();
        std::lock_guard<std::mutex> g(g_noteMtx);
        g_notes.clear();
        return;
    }
    const DWORD now = GetTickCount();
    bool hit = false, kill = false, noteHit = false;
    {
        std::lock_guard<std::mutex> g(g_noteMtx);
        for (const Note& n : g_notes) { g_hitOn[n.obj] = now; noteHit |= n.hit; }
        g_notes.clear();
    }
    static int s_gh = 0, s_gk = 0;
    const int gh = g_gameHit, gk = g_gameKill;
    bool clipHit = false;
    if (gh != s_gh) { s_gh = gh; clipHit = true; }
    if (gk != s_gk) { s_gk = gk; kill = true; if (g_logged++ < 12) Log("[sound] the game played its kill clip"); }
    const bool clips = gh + gk > 0; // the game's own feedback works: trust it for kills, skip the guesses
    // A hit is every damage the game applies to an enemy: that is one per bullet that landed. The game's own
    // hit clip only plays for about 60% of them, which is what made the sound feel random.
    hit = g_dmgSeen > 0 ? noteHit : clipHit;
    if (hit && g_logged++ < 12) Log("[sound] hit (%s)", g_dmgSeen > 0 ? "damage applied to an enemy" : "the game's hit clip");

    for (auto it = g_hitOn.begin(); it != g_hitOn.end();) it = now - it->second > 5000 ? g_hitOn.erase(it) : std::next(it);
    auto recent = [&](void* o) { const auto h = g_hitOn.find(o); return h != g_hitOn.end() && now - h->second < 2000; };
    std::unordered_map<void*, bool> cur;
    for (const auto& p : game::Snapshot()) {
        if (p.local) continue;
        const auto it = g_prev.find(p.obj);
        if (!clips && it != g_prev.end() && it->second && !p.alive && recent(p.obj)) {
            kill = true;
            if (g_logged++ < 12) Log("[sound] kill inferred: %s died right after your shots", p.name.c_str());
        }
        cur[p.obj] = p.alive;
    }
    // a killed player's object can despawn before the next frame sees it dead
    if (!clips)
        for (const auto& e : g_prev)
            if (e.second && !cur.count(e.first) && recent(e.first)) {
                kill = true;
                if (g_logged++ < 12) Log("[sound] kill inferred: an enemy despawned right after your shots");
            }
    g_prev.swap(cur);

    if (kill && cfg.sound.kill && now - g_lastKill > 150) { g_lastKill = now; PlayFile(true, cfg.sound.kill_file, cfg.sound.kill_vol); }
    else if (hit && cfg.sound.hit && now - g_lastHit > 60 && now - g_lastKill > 250) { g_lastHit = now; PlayFile(false, cfg.sound.hit_file, cfg.sound.hit_vol); } // the kill sound covers the killing blow
}

std::vector<std::string> Files(bool kill) {
    std::vector<std::string> out;
    CreateDirectoryW((DllDir() + L"\\sounds").c_str(), nullptr);
    CreateDirectoryW(Dir(kill).c_str(), nullptr);
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((Dir(kill) + L"\\*.wav").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(Narrow(fd.cFileName)); } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

void Preview(bool kill) {
    PlayFile(kill, kill ? cfg.sound.kill_file : cfg.sound.hit_file, kill ? cfg.sound.kill_vol : cfg.sound.hit_vol);
}

namespace {
    std::mutex g_msgMtx;
    std::string g_msg;
    std::atomic<bool> g_busy{ false };
    std::atomic<int> g_imports{ 0 };
    void SetMsg(const std::string& m) { std::lock_guard<std::mutex> g(g_msgMtx); g_msg = m; }
}

// The file dialog is modal, so it runs on its own thread instead of freezing the game's frame.
void Import(bool kill) {
    if (g_busy.exchange(true)) return;
    std::thread([kill] {
        wchar_t file[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = L"WAV audio (16-bit PCM)\0*.wav\0\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = kill ? L"Choose a kill sound" : L"Choose a hit sound";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) {
            std::vector<uint8_t> probe;
            std::string why;
            if (!Load(file, 1.f, probe, &why)) {
                SetMsg("Not imported: " + why);
            } else {
                const wchar_t* base = wcsrchr(file, L'\\');
                const std::wstring name = base ? base + 1 : file;
                CreateDirectoryW((DllDir() + L"\\sounds").c_str(), nullptr);
                CreateDirectoryW(Dir(kill).c_str(), nullptr);
                if (CopyFileW(file, (Dir(kill) + L"\\" + name).c_str(), FALSE)) { SetMsg("Imported " + Narrow(name)); g_imports++; }
                else SetMsg("Not imported: could not copy the file");
            }
        }
        g_busy = false;
    }).detach();
}

int ImportCount() { return g_imports; }

std::string LastMessage() {
    std::lock_guard<std::mutex> g(g_msgMtx);
    return g_msg;
}

void OpenFolder() {
    CreateDirectoryW((DllDir() + L"\\sounds").c_str(), nullptr);
    ShellExecuteW(nullptr, L"open", (DllDir() + L"\\sounds").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
}
