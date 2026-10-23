// SPDX-License-Identifier: GPL-3.0-only
// TRIBE, native: the window and the sound of the desktop build (desktop/build.sh, SFML 2.x). The panel is laid
// out as the browser page is (web/emu/index.html), with the same keys and mouse controls. The sound stream asks
// the device for its samples (tribe_pull), so the sound, the screen and the lights all follow one clock.

#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "tribe_core.h"

namespace {

constexpr unsigned W = 1000, H = 640;                 // the panel, in the page's units: one pixel each here
constexpr unsigned RATE = 44100, CHUNK = 256;         // the device's sample rate, and its I2S half buffer
constexpr int MASTER_START = 2800, MASTER_MAX = 4096;

// the device's button bits (B_* in app/plat.h) and encoder roles (EN_* in app/plat.h)
enum { BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC,
       BTN_OCTDN, BTN_OCTUP };
enum { ROLE_SELECT, ROLE_ALGO, ROLE_PRESET, ROLE_K1, ROLE_K2, ROLE_K3, ROLE_K4 };
constexpr int ROLE_MASTER = -1;                       // the MASTER pot is not an encoder: the device gets its value

// the colours of the page
const sf::Color BODY(58, 61, 66), TRAY(38, 40, 44), KNOB(31, 33, 36), KNOB_RIM(68, 71, 76), BTN_FACE(46, 49, 53),
    BTN_RIM(74, 77, 82), LABEL(232, 232, 234), KEY_WHITE(217, 218, 220), KEY_BLACK(28, 29, 32), SCREEN_RIM(17, 18, 20),
    LIT_WHITE(243, 178, 92), LIT_BLACK(224, 144, 44), DIM_WHITE(246, 220, 182), DIM_BLACK(122, 83, 38),
    FAINT_WHITE(232, 224, 210), FAINT_BLACK(70, 52, 32), BTN_DIM(96, 80, 52), BTN_LIT(243, 178, 92),
    HELD(240, 160, 67), LATCHED(111, 195, 255), INK_DARK(30, 30, 32), HINT(150, 152, 158),
    HINT_ON_WHITE(70, 72, 78), HINT_ON_BLACK(190, 192, 198);

struct Knob { float x, y, r; const char *label; int role; };
const Knob KNOBS[] = {
    {105, 100, 24, "MASTER", ROLE_MASTER}, {215, 100, 24, "SELECT", ROLE_SELECT}, {105, 205, 24, "PRESETS", ROLE_PRESET},
    {215, 205, 24, "ALGORITHM", ROLE_ALGO}, {610, 100, 22, "KNOB1", ROLE_K1}, {714, 100, 22, "KNOB2", ROLE_K2},
    {818, 100, 22, "KNOB3", ROLE_K3}, {922, 100, 22, "KNOB4", ROLE_K4},
};
constexpr int NKNOBS = sizeof KNOBS / sizeof KNOBS[0];
constexpr int SELECT_KNOB = 1;                        // KNOBS[] index of SELECT: the arrow keys turn it

struct Button { float x, y, w, h; int bit; const char *top; const char *bottom; std::string hint; };
std::vector<Button> make_buttons()
{
    std::vector<Button> b = {{72, 268, 82, 38, BTN_OCTDN, "OCT-", nullptr, ""}, {170, 268, 82, 38, BTN_OCTUP, "OCT+", nullptr, ""}};
    const char *row1[] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO"};
    const char *row2[] = {"HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
    for (int i = 0; i < 6; i++) {
        b.push_back({582.0f + i * 63, 174, 54, 48, i, row1[i], nullptr, ""});
        b.push_back({582.0f + i * 63, 236, 54, 48, 6 + i, row2[i], i == 4 ? "STOP" : nullptr, ""});
    }
    return b;
}

struct Key { float x, y, w, h; int key; bool black; std::string hint; };
std::vector<Key> make_keys()
{
    const float x0 = 36, y0 = 340, w = 928, h = 270, pitch = (w - 24) / 16, kw = pitch * 0.78f;
    const int black_after[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    std::vector<Key> k;
    for (int i = 0; i < 16; i++)
        k.push_back({x0 + 12 + i * pitch + (pitch - kw) / 2, y0 + h * 0.43f, kw, h * 0.52f, tribe_white_key(i), false, ""});
    for (int i = 0; i < 11; i++)
        k.push_back({x0 + 12 + (black_after[i] + 1) * pitch - kw / 2, y0 + h * 0.06f, kw, h * 0.33f, tribe_black_key(i), true, ""});
    return k;
}

// the computer's keys: Q-I and J-; white keys, 1-0 and - black keys, A S D F G H FX SEL ENV LFO EDIT GLO,
// Z X C V B N HOME SAVE ARP SEQ PLAY REC, Left and Right OCT- and OCT+
struct Bind { sf::Keyboard::Key code; bool is_key; int index; };
sf::Keyboard::Key letter(char c) { return sf::Keyboard::Key(sf::Keyboard::A + (c - 'a')); }
sf::Keyboard::Key digit(char c) { return c == '0' ? sf::Keyboard::Num0 : sf::Keyboard::Key(sf::Keyboard::Num1 + (c - '1')); }
std::vector<Bind> make_binds()
{
    std::vector<Bind> b;
    const char *white_a = "qwertyui", *black = "1234567890";
    for (int i = 0; i < 8; i++)
        b.push_back({letter(white_a[i]), true, tribe_white_key(i)});
    const sf::Keyboard::Key white_b[8] = {letter('j'), letter('k'), letter('l'), letter('m'), sf::Keyboard::Comma,
                                          sf::Keyboard::Period, sf::Keyboard::Slash, sf::Keyboard::SemiColon};
    for (int i = 0; i < 8; i++)
        b.push_back({white_b[i], true, tribe_white_key(8 + i)});
    for (int i = 0; i < 10; i++)
        b.push_back({digit(black[i]), true, tribe_black_key(i)});
    b.push_back({sf::Keyboard::Hyphen, true, tribe_black_key(10)});
    b.push_back({letter('a'), false, BTN_FX});
    b.push_back({letter('s'), false, BTN_SEL});
    b.push_back({letter('d'), false, BTN_ENV});
    b.push_back({letter('f'), false, BTN_LFO});
    b.push_back({letter('g'), false, BTN_EDIT});
    b.push_back({letter('h'), false, BTN_GLO});
    b.push_back({letter('z'), false, BTN_HOME});
    b.push_back({letter('x'), false, BTN_SAVE});
    b.push_back({letter('c'), false, BTN_ARP});
    b.push_back({letter('v'), false, BTN_SEQ});
    b.push_back({letter('b'), false, BTN_PLAY});
    b.push_back({letter('n'), false, BTN_REC});
    b.push_back({sf::Keyboard::Left, false, BTN_OCTDN});
    b.push_back({sf::Keyboard::Right, false, BTN_OCTUP});
    return b;
}

// the sound card: the device makes the samples it is asked for
std::mutex device_lock;                               // the device: the stream runs it, the window reads and feeds it
class DeviceSound : public sf::SoundStream {
public:
    DeviceSound() { initialize(2, RATE); }

private:
    bool onGetData(Chunk &data) override
    {
        std::lock_guard<std::mutex> lock(device_lock);
        tribe_pull(samples, CHUNK);
        data.samples = samples;
        data.sampleCount = CHUNK * 2;
        return true;
    }
    void onSeek(sf::Time) override {}
    sf::Int16 samples[CHUNK * 2];
};

std::string key_name(sf::Keyboard::Key c)
{
    if (c >= sf::Keyboard::A && c <= sf::Keyboard::Z) return std::string(1, static_cast<char>('A' + (c - sf::Keyboard::A)));
    if (c >= sf::Keyboard::Num0 && c <= sf::Keyboard::Num9) return std::string(1, static_cast<char>('0' + (c - sf::Keyboard::Num0)));
    switch (c) {
    case sf::Keyboard::Hyphen: return "-";
    case sf::Keyboard::Comma: return ",";
    case sf::Keyboard::Period: return ".";
    case sf::Keyboard::Slash: return "/";
    case sf::Keyboard::SemiColon: return ";";
    case sf::Keyboard::Left: return "LEFT";
    case sf::Keyboard::Right: return "RIGHT";
    default: return "";
    }
}

bool inside(float x, float y, float px, float py, float w, float h)
{
    return x >= px && x < px + w && y >= py && y < py + h;
}

class Panel {
public:
    Panel(std::string dir, const sf::Font &font) : dir_(std::move(dir)), font_(font)
    {
        screen_.create(240, 240);
        screen_.setSmooth(false);
        rgba_.resize(240 * 240 * 4);
        buttons_ = make_buttons();
        keys_ = make_keys();
        binds_ = make_binds();
        for (Button &b : buttons_) b.hint = hint_of(false, b.bit);
        for (Key &k : keys_) k.hint = hint_of(true, k.key);
        set_master(MASTER_START);
    }

    void event(const sf::Event &e, sf::RenderWindow &window)
    {
        switch (e.type) {
        case sf::Event::Closed:
            window.close();
            break;
        case sf::Event::LostFocus:
            kb_down_.clear();
            break;
        case sf::Event::KeyPressed:
            if (e.key.code == sf::Keyboard::Up || e.key.code == sf::Keyboard::Down) {
                turn(SELECT_KNOB, e.key.code == sf::Keyboard::Up ? 1 : -1);
            } else {
                kb_down_.insert(e.key.code);
            }
            break;
        case sf::Event::KeyReleased:
            kb_down_.erase(e.key.code);
            break;
        case sf::Event::MouseButtonPressed:
            press(e.mouseButton.button, e.mouseButton.x, e.mouseButton.y);
            break;
        case sf::Event::MouseButtonReleased:
            if (e.mouseButton.button == sf::Mouse::Left) {
                mouse_btn_ = 0;
                mouse_keys_ = 0;
                drag_ = -1;
            }
            break;
        case sf::Event::MouseMoved:
            if (drag_ >= 0) {
                acc_ += drag_y_ - e.mouseMove.y;
                drag_y_ = e.mouseMove.y;
                const int step = KNOBS[drag_].role == ROLE_MASTER ? 3 : 7;
                const int n = static_cast<int>(acc_ / step);
                if (n) {
                    acc_ -= n * step;
                    turn(drag_, n);
                }
            }
            break;
        case sf::Event::MouseWheelScrolled:
            if (e.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel) {
                const int k = knob_at(e.mouseWheelScroll.x, e.mouseWheelScroll.y);
                if (k >= 0 && e.mouseWheelScroll.delta != 0.0f)
                    turn(k, e.mouseWheelScroll.delta > 0 ? 1 : -1);
            }
            break;
        default:
            break;
        }
    }

    // once a frame: the input goes to the device, and what the device shows is copied out
    void update()
    {
        std::lock_guard<std::mutex> lock(device_lock);
        uint32_t btn = latched_ | mouse_btn_, keys = mouse_keys_;
        for (const Bind &b : binds_)
            if (kb_down_.count(b.code))
                (b.is_key ? keys : btn) |= 1u << b.index;
        tribe_buttons(btn);
        tribe_keys(keys);
        held_btn_ = btn;
        held_keys_ = keys;
        screen_to_rgba(tribe_screen());
        lit_btn_ = tribe_lit_buttons();
        dim_btn_ = tribe_dim_buttons();
        lit_keys_ = tribe_lit_keys();
        dim_keys_ = tribe_dim_keys();
        faint_keys_ = tribe_faint_keys();
        tribe_save(dir_.c_str());
    }

    void draw(sf::RenderTarget &t) const
    {
        rect(t, 4, 4, 992, 632, BODY);
        for (const Knob &k : KNOBS) draw_knob(t, k, angle_[&k - KNOBS]);

        rect(t, 58, 258, 210, 58, TRAY);
        rect(t, 300, 44, 236, 236, SCREEN_RIM);
        rect(t, 568, 160, 400, 140, TRAY);
        rect(t, 36, 340, 928, 270, TRAY);
        for (const Key &k : keys_)
            if (!k.black) draw_key(t, k);
        for (const Key &k : keys_)
            if (k.black) draw_key(t, k);
        for (const Button &b : buttons_) draw_button(t, b);

        sf::Sprite spr(screen_);
        spr.setPosition(298, 42);
        t.draw(spr);
    }

    // the window's own pointer: a click on a control holds it, a shift or right click latches it
    void press(sf::Mouse::Button b, float x, float y)
    {
        if (b == sf::Mouse::Left) {
            const int k = knob_at(x, y);
            if (k >= 0) {
                drag_ = k;
                drag_y_ = y;
                acc_ = 0;
                return;
            }
            for (const Button &btn : buttons_) {
                if (!inside(x, y, btn.x, btn.y, btn.w, btn.h)) continue;
                const uint32_t bit = 1u << btn.bit;
                if (sf_shift()) { latched_ ^= bit; return; }
                if (latched_ & bit) { latched_ &= ~bit; return; }
                mouse_btn_ = bit;
                return;
            }
            const int key = key_at(x, y);
            if (key >= 0) mouse_keys_ = 1u << key;
        } else if (b == sf::Mouse::Right) {
            for (const Button &btn : buttons_)
                if (inside(x, y, btn.x, btn.y, btn.w, btn.h)) latched_ ^= 1u << btn.bit;
        }
    }

private:
    // the computer key that presses a control (the first one listed for it)
    std::string hint_of(bool is_key, int index) const
    {
        for (const Bind &b : binds_)
            if (b.is_key == is_key && b.index == index) return key_name(b.code);
        return "";
    }

    // the knobs turn as the page's do: a MASTER step is 64 of 4096, an encoder's detent is 18 degrees
    void turn(int k, int n)
    {
        if (KNOBS[k].role == ROLE_MASTER) {
            set_master(static_cast<int>(master_) + n * 64);
        } else {
            angle_[k] = std::fmod(std::fmod(angle_[k] + n * 18.0f, 360.0f) + 360.0f, 360.0f);
            std::lock_guard<std::mutex> lock(device_lock);
            tribe_turn(KNOBS[k].role, n);
        }
    }

    void set_master(int v)
    {
        master_ = v < 0 ? 0 : v > MASTER_MAX ? MASTER_MAX : v;
        angle_[0] = -135.0f + 270.0f * master_ / MASTER_MAX;
        std::lock_guard<std::mutex> lock(device_lock);
        tribe_master(master_);
    }

    int knob_at(float x, float y) const
    {
        for (int i = 0; i < NKNOBS; i++) {
            const float dx = x - KNOBS[i].x, dy = y - KNOBS[i].y, r = KNOBS[i].r + 14;
            if (dx * dx + dy * dy <= r * r) return i;
        }
        return -1;
    }

    int key_at(float x, float y) const
    {
        for (const Key &k : keys_)
            if (k.black && inside(x, y, k.x, k.y, k.w, k.h)) return k.key;
        for (const Key &k : keys_)
            if (!k.black && inside(x, y, k.x, k.y, k.w, k.h)) return k.key;
        return -1;
    }

    static bool sf_shift()
    {
        return sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);
    }

    void screen_to_rgba(const uint16_t *fb)
    {
        for (int i = 0; i < 240 * 240; i++) {
            const uint16_t p = static_cast<uint16_t>((fb[i] >> 8) | (fb[i] << 8));
            rgba_[i * 4] = static_cast<uint8_t>(((p >> 11) & 31) * 255 / 31);
            rgba_[i * 4 + 1] = static_cast<uint8_t>(((p >> 5) & 63) * 255 / 63);
            rgba_[i * 4 + 2] = static_cast<uint8_t>((p & 31) * 255 / 31);
            rgba_[i * 4 + 3] = 255;
        }
        screen_.update(rgba_.data());
    }

    static void rect(sf::RenderTarget &t, float x, float y, float w, float h, sf::Color c)
    {
        sf::RectangleShape r({w, h});
        r.setPosition(x, y);
        r.setFillColor(c);
        t.draw(r);
    }

    void label(sf::RenderTarget &t, float cx, float cy, const char *s, unsigned size, sf::Color c) const
    {
        sf::Text txt(s, font_, size);
        txt.setFillColor(c);
        const sf::FloatRect b = txt.getLocalBounds();
        txt.setOrigin(b.left + b.width / 2, b.top + b.height / 2);
        txt.setPosition(cx, cy);
        t.draw(txt);
    }

    void draw_knob(sf::RenderTarget &t, const Knob &k, float angle) const
    {
        sf::CircleShape tray(k.r + 5);
        tray.setOrigin(k.r + 5, k.r + 5);
        tray.setPosition(k.x, k.y);
        tray.setFillColor(TRAY);
        t.draw(tray);

        sf::CircleShape cap(k.r);
        cap.setOrigin(k.r, k.r);
        cap.setPosition(k.x, k.y);
        cap.setFillColor(KNOB);
        cap.setOutlineColor(KNOB_RIM);
        cap.setOutlineThickness(2);
        t.draw(cap);

        // the pointer: a short line from the rim inwards, turned to the knob's angle
        sf::RectangleShape ind({3, 10});
        ind.setOrigin(1.5f, k.r - 5);
        ind.setPosition(k.x, k.y);
        ind.setRotation(angle);
        ind.setFillColor(LABEL);
        t.draw(ind);

        label(t, k.x, k.y - k.r - 16, k.label, 17, LABEL);
        if (k.role == ROLE_SELECT) label(t, k.x, k.y + k.r + 14, "UP DOWN", 10, HINT);
    }

    void draw_button(sf::RenderTarget &t, const Button &b) const
    {
        const uint32_t bit = 1u << b.bit;
        const bool lit = lit_btn_ & bit, dim = dim_btn_ & bit;
        sf::RectangleShape face({b.w, b.h});
        face.setPosition(b.x, b.y);
        face.setFillColor(lit ? BTN_LIT : dim ? BTN_DIM : BTN_FACE);
        face.setOutlineColor(BTN_RIM);
        face.setOutlineThickness(1.5f);
        if (held_btn_ & bit) {
            face.setOutlineColor(HELD);
            face.setOutlineThickness(3);
        } else if (latched_ & bit) {
            face.setOutlineColor(LATCHED);
            face.setOutlineThickness(3);
        }
        t.draw(face);
        const sf::Color ink = lit ? INK_DARK : LABEL;
        if (b.bottom) {
            label(t, b.x + b.w / 2, b.y + b.h / 2 - 7, b.top, 12, ink);
            label(t, b.x + b.w / 2, b.y + b.h / 2 + 7, b.bottom, 12, ink);
        } else {
            label(t, b.x + b.w / 2, b.y + b.h / 2, b.top, 15, ink);
        }
        if (!b.hint.empty()) label(t, b.x + b.w / 2, b.y + b.h - 6, b.hint.c_str(), 10, lit ? INK_DARK : HINT);
    }

    void draw_key(sf::RenderTarget &t, const Key &k) const
    {
        const uint32_t bit = 1u << k.key;
        sf::Color c = k.black ? KEY_BLACK : KEY_WHITE;
        if (lit_keys_ & bit) c = k.black ? LIT_BLACK : LIT_WHITE;
        else if (dim_keys_ & bit) c = k.black ? DIM_BLACK : DIM_WHITE;
        else if (faint_keys_ & bit) c = k.black ? FAINT_BLACK : FAINT_WHITE;
        sf::RectangleShape r({k.w, k.h});
        r.setPosition(k.x, k.y);
        r.setFillColor(c);
        if (held_keys_ & bit) {
            r.setOutlineColor(HELD);
            r.setOutlineThickness(3);
        }
        t.draw(r);
        if (!k.hint.empty()) label(t, k.x + k.w / 2, k.y + k.h - (k.black ? 12.0f : 14.0f), k.hint.c_str(), 11, k.black ? HINT_ON_BLACK : HINT_ON_WHITE);
    }

    std::string dir_;
    const sf::Font &font_;
    sf::Texture screen_;
    std::vector<uint8_t> rgba_;
    std::vector<Button> buttons_;
    std::vector<Key> keys_;
    std::vector<Bind> binds_;
    std::set<int> kb_down_;                           // the computer's keys held (sf::Keyboard::Key)
    uint32_t latched_ = 0, mouse_btn_ = 0, mouse_keys_ = 0;
    uint32_t held_btn_ = 0, held_keys_ = 0, lit_btn_ = 0, dim_btn_ = 0, lit_keys_ = 0, dim_keys_ = 0, faint_keys_ = 0;
    int drag_ = -1;
    float drag_y_ = 0, acc_ = 0;
    int master_ = 0;
    float angle_[NKNOBS] = {};
};

// where the window was last: "x y" in window.txt of the data folder; the first run opens at the top left
sf::Vector2i load_place(const std::string &dir)
{
    std::ifstream f(dir + "/window.txt");
    int x = 0, y = 0;
    if (f >> x >> y) return {x, y};
    return {0, 0};
}

void save_place(const std::string &dir, sf::Vector2i p)
{
    std::ofstream f(dir + "/window.txt");
    f << p.x << ' ' << p.y << '\n';
}

std::string default_dir()
{
    if (const char *x = std::getenv("XDG_DATA_HOME"); x && *x) return std::string(x) + "/tribe";
    if (const char *h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share/tribe";
    return "tribe-data";
}

}  // namespace

int main(int argc, char **argv)
{
    std::string dir = default_dir(), shot;
    bool reset = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--reset") reset = true;
        else if (a == "--data" && i + 1 < argc) dir = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else {
            std::fprintf(stderr, "usage: tribe [--reset] [--data DIR] [--shot FILE.png]\n");
            return 2;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) std::fprintf(stderr, "cannot create %s: nothing will be saved\n", dir.c_str());
    if (reset) tribe_forget(dir.c_str());
    {
        std::lock_guard<std::mutex> lock(device_lock);
        tribe_init(dir.c_str());
    }

    sf::Font font;
    if (!font.loadFromFile(TRIBE_FONT)) std::fprintf(stderr, "no font at %s: the labels are not drawn\n", TRIBE_FONT);

    if (!shot.empty()) {
        // no window: two seconds of sound run the device (as the sound card would), then the panel is drawn to a PNG
        Panel panel(dir, font);
        std::vector<sf::Int16> buf(CHUNK * 2);
        for (unsigned n = 0; n < 2 * RATE / CHUNK; n++) {
            std::lock_guard<std::mutex> lock(device_lock);
            tribe_pull(buf.data(), CHUNK);
        }
        panel.update();
        sf::RenderTexture canvas;
        canvas.create(W, H);
        panel.draw(canvas);
        canvas.display();
        return canvas.getTexture().copyToImage().saveToFile(shot) ? 0 : 1;
    }

    sf::RenderWindow window(sf::VideoMode(W, H), "TRIBE", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);
    window.setPosition(load_place(dir));
    Panel panel(dir, font);

    // the place is saved when the window is moved, once the window manager has placed it (the first half second is skipped)
    sf::Vector2i place = window.getPosition();
    int frames = 0;
    DeviceSound sound;
    sound.play();
    while (window.isOpen()) {
        for (sf::Event e; window.pollEvent(e);) panel.event(e, window);
        panel.update();
        window.clear(sf::Color::Black);
        panel.draw(window);
        window.display();
        if (++frames > 30 && window.getPosition() != place) {
            place = window.getPosition();
            save_place(dir, place);
        }
    }
    sound.stop();
    std::lock_guard<std::mutex> lock(device_lock);
    tribe_save(dir.c_str());
    return 0;
}
