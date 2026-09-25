#include <SDL.h>
#include <SDL_ttf.h>

#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

using namespace cinder;

namespace {

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 720;
constexpr int SidebarWidth = 320;

struct Color { Uint8 r, g, b, a = 255; };
constexpr Color Background{7, 13, 20}, Panel{13, 24, 34}, Edge{38, 69, 78};
constexpr Color Ink{213, 232, 237}, Muted{106, 139, 150}, Cyan{56, 232, 214};
constexpr Color Teal{11, 207, 183}, Coral{245, 62, 44}, Violet{164, 87, 244};
constexpr Color Gold{255, 174, 36}, Ore{91, 210, 227}, Danger{255, 92, 70};

Color teamColor(int team) {
    switch (team) {
        case 0: return Teal;
        case 1: return Coral;
        case 2: return Violet;
        case 3: return Gold;
        default: return Muted;
    }
}

void setColor(SDL_Renderer* renderer, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, Color color) {
    setColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

void outlineRect(SDL_Renderer* renderer, const SDL_Rect& rect, Color color) {
    setColor(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

void line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, Color color) {
    setColor(renderer, color);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

void circle(SDL_Renderer* renderer, int cx, int cy, int radius, Color color, bool filled) {
    setColor(renderer, color);
    for (int y = -radius; y <= radius; ++y) {
        const int half = static_cast<int>(std::sqrt(std::max(0, radius * radius - y * y)));
        if (filled) SDL_RenderDrawLine(renderer, cx - half, cy + y, cx + half, cy + y);
        else {
            SDL_RenderDrawPoint(renderer, cx - half, cy + y);
            SDL_RenderDrawPoint(renderer, cx + half, cy + y);
        }
    }
}

class Fonts {
public:
    bool open(const std::filesystem::path& assetRoot) {
        const auto path = assetRoot / "DejaVuSans.ttf";
        normal_ = TTF_OpenFont(path.c_str(), 15);
        small_ = TTF_OpenFont(path.c_str(), 12);
        title_ = TTF_OpenFont(path.c_str(), 29);
        return normal_ && small_ && title_;
    }

    void close() {
        if (normal_) TTF_CloseFont(normal_);
        if (small_) TTF_CloseFont(small_);
        if (title_) TTF_CloseFont(title_);
        normal_ = small_ = title_ = nullptr;
    }

    ~Fonts() { close(); }

    TTF_Font* normal() const { return normal_; }
    TTF_Font* small() const { return small_; }
    TTF_Font* title() const { return title_; }

private:
    TTF_Font* normal_ = nullptr;
    TTF_Font* small_ = nullptr;
    TTF_Font* title_ = nullptr;
};

void text(SDL_Renderer* renderer, TTF_Font* font, const std::string& value, int x, int y,
          Color color = Ink, int maximumWidth = 0) {
    if (!font || value.empty()) return;
    SDL_Color sdlColor{color.r, color.g, color.b, color.a};
    SDL_Surface* surface = maximumWidth > 0
        ? TTF_RenderUTF8_Blended_Wrapped(font, value.c_str(), sdlColor, static_cast<Uint32>(maximumWidth))
        : TTF_RenderUTF8_Blended(font, value.c_str(), sdlColor);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect destination{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

bool inside(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

struct Camera {
    float x = 1100, y = 1100, zoom = 0.145f;

    SDL_FPoint screen(Vec2 point, const SDL_Rect& view) const {
        return {view.x + view.w * 0.5f + (point.x - x) * zoom,
                view.y + view.h * 0.5f + (point.y - y) * zoom};
    }

    Vec2 world(int screenX, int screenY, const SDL_Rect& view) const {
        return {x + (screenX - view.x - view.w * 0.5f) / zoom,
                y + (screenY - view.y - view.h * 0.5f) / zoom};
    }

    void home(const Simulation& sim, const SDL_Rect& view) {
        zoom = std::min(view.w / sim.worldSize(), view.h / sim.worldSize());
        x = y = sim.worldSize() * 0.5f;
        for (const auto& entity : sim.entities()) {
            if (entity.alive() && entity.team == 0 && entity.kind == Kind::Headquarters) {
                x = entity.pos.x + 300;
                y = entity.pos.y + 200;
                zoom = std::max(zoom, 0.23f);
                break;
            }
        }
        clamp(sim, view);
    }

    void clamp(const Simulation& sim, const SDL_Rect& view) {
        const float world = sim.worldSize();
        zoom = std::clamp(zoom, std::min(view.w / world, view.h / world), 0.75f);
        const float halfX = std::min(world * 0.5f, view.w / (2 * zoom));
        const float halfY = std::min(world * 0.5f, view.h / (2 * zoom));
        x = std::clamp(x, halfX, world - halfX);
        y = std::clamp(y, halfY, world - halfY);
    }
};

enum class ButtonAction { Start, Pause, Resume, Restart, UnitsTab, BuildTab, SelectArmy, SelectWorkers,
                          Train, Build, Research, Save, Load, Home, Quit };

struct Button {
    SDL_Rect rect{};
    std::string label;
    ButtonAction action = ButtonAction::Start;
    Kind kind = Kind::Worker;
    int parameter = 0;
    bool enabled = true;
};

class Game {
public:
    Game(SDL_Renderer* renderer, Fonts& fonts, std::filesystem::path savePath)
        : renderer_(renderer), fonts_(fonts), savePath_(std::move(savePath)) {
        sim_.reset();
    }

    void start(int map = 0) {
        Config config;
        config.map = map;
        sim_.reset(config);
        selected_.clear();
        placement_ = false;
        attackMove_ = false;
        menu_ = false;
        paused_ = false;
        status_ = "Select units with left click. Right click to move, gather, or attack.";
        camera_.home(sim_, battlefield());
    }

    void update(float seconds) {
        if (!menu_ && !paused_ && sim_.winner() == -1) sim_.update(std::min(seconds, 0.1f));
        for (auto it = selected_.begin(); it != selected_.end();) {
            const Entity* entity = sim_.find(*it);
            if (!entity || !entity->alive()) it = selected_.erase(it); else ++it;
        }
    }

    void event(const SDL_Event& event) {
        if (event.type == SDL_MOUSEWHEEL && !menu_) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            if (inside(mx, my, battlefield())) {
                Vec2 before = camera_.world(mx, my, battlefield());
                camera_.zoom *= event.wheel.y > 0 ? 1.16f : 0.86f;
                camera_.clamp(sim_, battlefield());
                Vec2 after = camera_.world(mx, my, battlefield());
                camera_.x += before.x - after.x;
                camera_.y += before.y - after.y;
                camera_.clamp(sim_, battlefield());
            }
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) mouseDown(event.button);
        if (event.type == SDL_MOUSEBUTTONUP) mouseUp(event.button);
        if (event.type == SDL_KEYDOWN && !event.key.repeat) keyDown(event.key.keysym.sym);
    }

    void moveCamera(float seconds, const Uint8* keys) {
        if (menu_ || paused_) return;
        const float amount = 900 * seconds / camera_.zoom;
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) camera_.x -= amount;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) camera_.x += amount;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) camera_.y -= amount;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) camera_.y += amount;
        camera_.clamp(sim_, battlefield());
    }

    void draw() {
        setColor(renderer_, Background);
        SDL_RenderClear(renderer_);
        buttons_.clear();
        if (menu_) drawMenu();
        else {
            drawWorld();
            drawSidebar();
            if (paused_) drawPause();
            if (sim_.winner() != -1) drawResult();
        }
        SDL_RenderPresent(renderer_);
    }

    bool capture(const std::filesystem::path& path) {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, WindowWidth, WindowHeight, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surface) return false;
        const bool ok = SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                              surface->pixels, surface->pitch) == 0 &&
                        SDL_SaveBMP(surface, path.c_str()) == 0;
        SDL_FreeSurface(surface);
        return ok;
    }

    bool quitRequested() const { return quit_; }

private:
    SDL_Rect battlefield() const { return {0, 0, WindowWidth - SidebarWidth, WindowHeight}; }

    SDL_FPoint screen(Vec2 point) const { return camera_.screen(point, battlefield()); }

    const Entity* entityAt(int x, int y) const {
        const Vec2 point = camera_.world(x, y, battlefield());
        const Entity* best = nullptr;
        float bestDistance = 1e9f;
        for (const auto& entity : sim_.entities()) {
            if (!entity.alive()) continue;
            if (entity.team > 0 && !sim_.visible(0, entity.pos)) continue;
            if (entity.kind == Kind::Resource && !sim_.explored(0, entity.pos)) continue;
            const float dx = entity.pos.x - point.x, dy = entity.pos.y - point.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float pickRadius = std::max(definition(entity.kind).radius, 18.0f / camera_.zoom);
            if (distance < pickRadius && distance < bestDistance) {
                best = &entity;
                bestDistance = distance;
            }
        }
        return best;
    }

    std::vector<Id> selectionVector() const { return {selected_.begin(), selected_.end()}; }

    void feedback(const CommandResult& result) {
        status_ = result.message.empty() ? (result.accepted ? "Order confirmed." : "Order unavailable.") : result.message;
    }

    void commandPoint(CommandType type, Vec2 point, Id target = 0) {
        Command command;
        command.type = type;
        command.team = 0;
        command.units = selectionVector();
        command.point = point;
        command.target = target;
        feedback(sim_.command(command));
    }

    void mouseDown(const SDL_MouseButtonEvent& mouse) {
        if (mouse.button == SDL_BUTTON_LEFT) {
            for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
                if (inside(mouse.x, mouse.y, it->rect)) {
                    activate(*it);
                    return;
                }
            }
            if (menu_ || paused_ || sim_.winner() != -1 || !inside(mouse.x, mouse.y, battlefield())) return;
            if (placement_) {
                Command command;
                command.type = CommandType::AutoBuild;
                command.team = 0;
                command.kind = placementKind_;
                command.point = camera_.world(mouse.x, mouse.y, battlefield());
                CommandResult result = sim_.command(command);
                feedback(result);
                if (result.accepted && !(SDL_GetModState() & KMOD_SHIFT)) placement_ = false;
                return;
            }
            if (attackMove_) {
                commandPoint(CommandType::AttackMove, camera_.world(mouse.x, mouse.y, battlefield()));
                attackMove_ = false;
                return;
            }
            dragging_ = true;
            dragStart_ = {mouse.x, mouse.y};
            dragCurrent_ = dragStart_;
        } else if (mouse.button == SDL_BUTTON_RIGHT && !menu_ && !paused_ && inside(mouse.x, mouse.y, battlefield())) {
            placement_ = attackMove_ = false;
            if (selected_.empty()) {
                status_ = "Select one or more units first.";
                return;
            }
            const Entity* hit = entityAt(mouse.x, mouse.y);
            Vec2 point = camera_.world(mouse.x, mouse.y, battlefield());
            if (hit && hit->kind == Kind::Resource) commandPoint(CommandType::Gather, point, hit->id);
            else if (hit && hit->team > 0) commandPoint(CommandType::Attack, point, hit->id);
            else {
                bool onlyBuildings = true;
                for (Id id : selected_) {
                    const Entity* entity = sim_.find(id);
                    if (entity && !definition(entity->kind).building) onlyBuildings = false;
                }
                commandPoint(onlyBuildings ? CommandType::Rally : CommandType::Move, point);
            }
        }
    }

    void mouseUp(const SDL_MouseButtonEvent& mouse) {
        if (mouse.button != SDL_BUTTON_LEFT || !dragging_) return;
        dragging_ = false;
        dragCurrent_ = {mouse.x, mouse.y};
        const int left = std::min(dragStart_.x, dragCurrent_.x);
        const int right = std::max(dragStart_.x, dragCurrent_.x);
        const int top = std::min(dragStart_.y, dragCurrent_.y);
        const int bottom = std::max(dragStart_.y, dragCurrent_.y);
        const bool additive = SDL_GetModState() & KMOD_SHIFT;
        if (!additive) selected_.clear();
        if (right - left < 6 && bottom - top < 6) {
            const Entity* hit = entityAt(mouse.x, mouse.y);
            if (hit && hit->team == 0 && hit->kind != Kind::Resource) {
                if (additive && selected_.count(hit->id)) selected_.erase(hit->id);
                else selected_.insert(hit->id);
                if (mouse.clicks >= 2) {
                    for (const auto& entity : sim_.entities()) {
                        SDL_FPoint location = screen(entity.pos);
                        if (entity.alive() && entity.team == 0 && entity.kind == hit->kind &&
                            inside(static_cast<int>(location.x), static_cast<int>(location.y), battlefield()))
                            selected_.insert(entity.id);
                    }
                }
            }
        } else {
            for (const auto& entity : sim_.entities()) {
                SDL_FPoint location = screen(entity.pos);
                if (entity.alive() && entity.team == 0 && !definition(entity.kind).building &&
                    location.x >= left && location.x <= right && location.y >= top && location.y <= bottom)
                    selected_.insert(entity.id);
            }
        }
    }

    void keyDown(SDL_Keycode key) {
        if (key == SDLK_ESCAPE) {
            if (placement_ || attackMove_) placement_ = attackMove_ = false;
            else if (!menu_ && sim_.winner() == -1) paused_ = !paused_;
            return;
        }
        if (menu_) {
            if (key == SDLK_RETURN) start();
            return;
        }
        if (sim_.winner() != -1 && key == SDLK_r) { start(sim_.config().map); return; }
        if (key == SDLK_SPACE || key == SDLK_p) paused_ = !paused_;
        else if (key == SDLK_f) camera_.home(sim_, battlefield());
        else if (key == SDLK_q) selectKind(Kind::Worker);
        else if (key == SDLK_e) selectArmy();
        else if (key == SDLK_x) issueSimple(CommandType::Stop);
        else if (key == SDLK_h) issueSimple(CommandType::Hold);
        else if (key == SDLK_z && !selected_.empty()) { attackMove_ = true; placement_ = false; status_ = "Click the battlefield to attack-move."; }
        else if (key == SDLK_F5) save();
        else if (key == SDLK_F9) load();
    }

    void issueSimple(CommandType type) {
        if (selected_.empty()) { status_ = "Select one or more units first."; return; }
        Command command;
        command.type = type;
        command.team = 0;
        command.units = selectionVector();
        feedback(sim_.command(command));
    }

    void selectKind(Kind kind) {
        selected_.clear();
        for (const auto& entity : sim_.entities())
            if (entity.alive() && entity.team == 0 && entity.kind == kind) selected_.insert(entity.id);
        status_ = "Selected " + std::to_string(selected_.size()) + " " + definition(kind).name + ".";
    }

    void selectArmy() {
        selected_.clear();
        for (const auto& entity : sim_.entities()) {
            const auto& type = definition(entity.kind);
            if (entity.alive() && entity.team == 0 && !type.building && entity.kind != Kind::Worker)
                selected_.insert(entity.id);
        }
        status_ = "Selected " + std::to_string(selected_.size()) + " combat units.";
    }

    void save() {
        std::error_code error;
        std::filesystem::create_directories(savePath_.parent_path(), error);
        status_ = sim_.save(savePath_.string()) ? "Game saved." : "Could not save the game.";
    }

    void load() {
        if (sim_.load(savePath_.string())) {
            selected_.clear();
            menu_ = paused_ = false;
            camera_.home(sim_, battlefield());
            status_ = "Saved game loaded.";
        } else status_ = "No valid saved game was found.";
    }

    void activate(const Button& button) {
        if (!button.enabled) { status_ = "Requirements not met or insufficient ore."; return; }
        switch (button.action) {
            case ButtonAction::Start: start(button.parameter); break;
            case ButtonAction::Pause: paused_ = true; break;
            case ButtonAction::Resume: paused_ = false; break;
            case ButtonAction::Restart: start(sim_.config().map); break;
            case ButtonAction::UnitsTab: buildTab_ = false; break;
            case ButtonAction::BuildTab: buildTab_ = true; break;
            case ButtonAction::SelectArmy: selectArmy(); break;
            case ButtonAction::SelectWorkers: selectKind(Kind::Worker); break;
            case ButtonAction::Train: {
                Command command;
                command.type = CommandType::AutoTrain;
                command.team = 0;
                command.kind = button.kind;
                command.queueIndex = 1;
                feedback(sim_.command(command));
                break;
            }
            case ButtonAction::Build:
                placement_ = true;
                attackMove_ = false;
                placementKind_ = button.kind;
                status_ = std::string("Place ") + definition(button.kind).name + " on visible clear ground.";
                break;
            case ButtonAction::Research: {
                Command command;
                command.type = CommandType::AutoResearch;
                command.team = 0;
                command.queueIndex = button.parameter;
                feedback(sim_.command(command));
                break;
            }
            case ButtonAction::Save: save(); break;
            case ButtonAction::Load: load(); break;
            case ButtonAction::Home: camera_.home(sim_, battlefield()); break;
            case ButtonAction::Quit: quit_ = true; break;
        }
    }

    void addButton(int x, int y, int width, int height, const std::string& label, ButtonAction action,
                   Kind kind = Kind::Worker, int parameter = 0, bool enabled = true) {
        Button button{{x, y, width, height}, label, action, kind, parameter, enabled};
        buttons_.push_back(button);
        fillRect(renderer_, button.rect, enabled ? Color{22, 47, 58} : Color{22, 31, 37});
        outlineRect(renderer_, button.rect, enabled ? Edge : Color{35, 43, 47});
        text(renderer_, fonts_.small(), label, x + 8, y + 8, enabled ? Ink : Muted, width - 16);
    }

    void drawMenu() {
        fillRect(renderer_, {0, 0, WindowWidth, WindowHeight}, Background);
        text(renderer_, fonts_.title(), "CINDERLINE", 493, 116, Cyan);
        text(renderer_, fonts_.normal(), "An original real-time strategy game", 480, 160, Muted);
        text(renderer_, fonts_.normal(), "Choose a battlefield", 544, 238, Ink);
        const std::array<const char*, 3> maps{"SHATTERED RIFT", "EMBER BASIN", "ASHEN CROSSING"};
        for (int i = 0; i < 3; ++i) addButton(470, 280 + i * 58, 340, 44, maps[i], ButtonAction::Start, Kind::Worker, i);
        text(renderer_, fonts_.small(), "Ubuntu playtest build  |  Press Enter for Shattered Rift", 470, 482, Muted);
        text(renderer_, fonts_.small(), "Local skirmish against AI. No account or internet connection needed.", 411, 523, Muted);
        addButton(550, 576, 180, 40, "QUIT", ButtonAction::Quit);
    }

    void drawWorld() {
        const SDL_Rect view = battlefield();
        fillRect(renderer_, view, Color{15, 27, 31});

        const int fogStep = 6;
        const float cell = sim_.worldSize() / Simulation::FogSize;
        for (int y = 0; y < Simulation::FogSize; y += fogStep) {
            for (int x = 0; x < Simulation::FogSize; x += fogStep) {
                Vec2 center{(x + fogStep * 0.5f) * cell, (y + fogStep * 0.5f) * cell};
                SDL_FPoint topLeft = screen({x * cell, y * cell});
                SDL_FPoint bottomRight = screen({(x + fogStep) * cell, (y + fogStep) * cell});
                SDL_Rect tile{static_cast<int>(topLeft.x), static_cast<int>(topLeft.y),
                              static_cast<int>(bottomRight.x - topLeft.x) + 1,
                              static_cast<int>(bottomRight.y - topLeft.y) + 1};
                if (!sim_.explored(0, center)) fillRect(renderer_, tile, Color{5, 9, 13, 255});
                else if (!sim_.visible(0, center)) fillRect(renderer_, tile, Color{9, 16, 21, 255});
            }
        }

        for (const auto& obstacle : sim_.obstacles()) {
            SDL_FPoint a = screen({obstacle.center.x - obstacle.half.x, obstacle.center.y - obstacle.half.y});
            SDL_FPoint b = screen({obstacle.center.x + obstacle.half.x, obstacle.center.y + obstacle.half.y});
            SDL_Rect rect{static_cast<int>(a.x), static_cast<int>(a.y),
                          std::max(1, static_cast<int>(b.x - a.x)), std::max(1, static_cast<int>(b.y - a.y))};
            fillRect(renderer_, rect, Color{46, 54, 54});
            outlineRect(renderer_, rect, Color{73, 77, 70});
        }

        for (const auto& effect : sim_.effects()) {
            if (!sim_.effectLinkVisible(effect, 0)) continue;
            SDL_FPoint from = screen(effect.from), to = screen(effect.to);
            line(renderer_, static_cast<int>(from.x), static_cast<int>(from.y),
                 static_cast<int>(to.x), static_cast<int>(to.y), effect.type == EffectType::Heal ? Cyan : Gold);
        }

        for (const auto& entity : sim_.entities()) drawEntity(entity);

        if (dragging_) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            dragCurrent_ = {mx, my};
            SDL_Rect rect{std::min(dragStart_.x, mx), std::min(dragStart_.y, my),
                          std::abs(dragStart_.x - mx), std::abs(dragStart_.y - my)};
            outlineRect(renderer_, rect, Cyan);
        }

        if (placement_) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            if (inside(mx, my, view)) {
                Vec2 site = camera_.world(mx, my, view);
                const bool valid = sim_.autoBuildStatus(0, placementKind_, &site).accepted;
                const int radius = std::max(10, static_cast<int>(definition(placementKind_).radius * camera_.zoom));
                circle(renderer_, mx, my, radius, valid ? Cyan : Danger, false);
            }
        }
    }

    void drawEntity(const Entity& entity) {
        if (!entity.alive()) return;
        if (entity.team > 0 && !sim_.visible(0, entity.pos)) return;
        if (entity.kind == Kind::Resource && !sim_.explored(0, entity.pos)) return;
        SDL_FPoint location = screen(entity.pos);
        if (!inside(static_cast<int>(location.x), static_cast<int>(location.y), battlefield())) return;
        const Definition& type = definition(entity.kind);
        const int radius = std::max(type.building ? 7 : 4, static_cast<int>(type.radius * camera_.zoom));
        Color color = entity.kind == Kind::Resource ? Ore : teamColor(entity.team);
        if (type.building || entity.kind == Kind::Resource) {
            SDL_Rect body{static_cast<int>(location.x) - radius, static_cast<int>(location.y) - radius,
                          radius * 2, radius * 2};
            fillRect(renderer_, body, color);
            outlineRect(renderer_, body, Ink);
        } else {
            circle(renderer_, static_cast<int>(location.x), static_cast<int>(location.y), radius, color, true);
            if (type.air) circle(renderer_, static_cast<int>(location.x), static_cast<int>(location.y), radius + 2, Ink, false);
        }
        if (selected_.count(entity.id)) circle(renderer_, static_cast<int>(location.x), static_cast<int>(location.y), radius + 5, Cyan, false);
        if (entity.kind != Kind::Resource && (selected_.count(entity.id) || entity.hp < type.hp)) {
            const int width = std::max(16, radius * 2);
            SDL_Rect back{static_cast<int>(location.x) - width / 2, static_cast<int>(location.y) - radius - 8, width, 3};
            fillRect(renderer_, back, Color{68, 28, 27});
            SDL_Rect health = back;
            health.w = std::max(0, static_cast<int>(width * entity.hp / type.hp));
            fillRect(renderer_, health, Teal);
        }
        if (entity.progress < 1 && type.building) {
            SDL_Rect progress{static_cast<int>(location.x) - radius, static_cast<int>(location.y) + radius + 3,
                              static_cast<int>(radius * 2 * entity.progress), 3};
            fillRect(renderer_, progress, Gold);
        }
    }

    void drawSidebar() {
        const int x = WindowWidth - SidebarWidth;
        fillRect(renderer_, {x, 0, SidebarWidth, WindowHeight}, Panel);
        line(renderer_, x, 0, x, WindowHeight, Edge);
        const Player& player = sim_.players()[0];
        text(renderer_, fonts_.title(), "CINDERLINE", x + 18, 14, Cyan);
        text(renderer_, fonts_.normal(), "ORE " + std::to_string(player.ore) + "   CREW " +
             std::to_string(sim_.supply(0)) + "/" + std::to_string(sim_.capacity(0)), x + 18, 55, Ink);
        const int minutes = static_cast<int>(sim_.time()) / 60;
        const int seconds = static_cast<int>(sim_.time()) % 60;
        char clock[32];
        std::snprintf(clock, sizeof(clock), "%02d:%02d   TIER %d", minutes, seconds, player.tier);
        text(renderer_, fonts_.small(), clock, x + 18, 82, Muted);

        addButton(x + 16, 112, 138, 36, "ARMY [E]", ButtonAction::SelectArmy);
        addButton(x + 166, 112, 138, 36, "DRUDGES [Q]", ButtonAction::SelectWorkers);
        addButton(x + 16, 160, 138, 34, "TRAIN", ButtonAction::UnitsTab);
        addButton(x + 166, 160, 138, 34, "BUILD", ButtonAction::BuildTab);

        const std::array<Kind, 8> units{Kind::Worker, Kind::Striker, Kind::Lancer, Kind::Scout,
                                        Kind::Bastion, Kind::Mortar, Kind::Mender, Kind::Kite};
        const std::array<Kind, 5> buildings{Kind::Processor, Kind::Foundry, Kind::MotorPool,
                                            Kind::Laboratory, Kind::Turret};
        int y = 210;
        if (!buildTab_) {
            for (Kind kind : units) {
                const Definition& type = definition(kind);
                const JobPlan plan = sim_.autoTrainStatus(0, kind, 1);
                addButton(x + 16, y, 288, 35, std::string(type.name) + "  " + std::to_string(type.cost) + " ORE",
                          ButtonAction::Train, kind, 0, plan.accepted);
                y += 39;
            }
        } else {
            for (Kind kind : buildings) {
                const Definition& type = definition(kind);
                const JobPlan plan = sim_.autoBuildStatus(0, kind);
                addButton(x + 16, y, 288, 35, std::string(type.name) + "  " + std::to_string(type.cost) + " ORE",
                          ButtonAction::Build, kind, 0, plan.accepted);
                y += 39;
            }
            addButton(x + 16, y + 3, 138, 36, "WEAPONS", ButtonAction::Research, Kind::Worker, 0,
                      sim_.autoResearchStatus(0, 0).accepted);
            addButton(x + 166, y + 3, 138, 36, "ARMOR", ButtonAction::Research, Kind::Worker, 1,
                      sim_.autoResearchStatus(0, 1).accepted);
        }

        text(renderer_, fonts_.small(), std::to_string(selected_.size()) + " SELECTED", x + 18, 545, Cyan);
        std::string selection;
        if (!selected_.empty()) {
            const Entity* entity = sim_.find(*selected_.begin());
            if (entity) selection = definition(entity->kind).name;
            if (selected_.size() > 1) selection += " + " + std::to_string(selected_.size() - 1);
        } else selection = "Click or drag on the battlefield";
        text(renderer_, fonts_.small(), selection, x + 18, 568, Ink, 286);
        text(renderer_, fonts_.small(), status_, x + 18, 600, Muted, 286);

        addButton(x + 16, 674, 65, 30, "SAVE", ButtonAction::Save);
        addButton(x + 88, 674, 65, 30, "LOAD", ButtonAction::Load);
        addButton(x + 160, 674, 65, 30, "HOME", ButtonAction::Home);
        addButton(x + 232, 674, 72, 30, "PAUSE", ButtonAction::Pause);
    }

    void drawPause() {
        SDL_Rect panel{315, 180, 330, 350};
        fillRect(renderer_, panel, Color{9, 20, 27});
        outlineRect(renderer_, panel, Cyan);
        text(renderer_, fonts_.title(), "PAUSED", 423, 210, Cyan);
        addButton(365, 280, 230, 42, "RESUME", ButtonAction::Resume);
        addButton(365, 338, 230, 42, "RESTART SKIRMISH", ButtonAction::Restart);
        addButton(365, 396, 230, 42, "SAVE", ButtonAction::Save);
        addButton(365, 454, 230, 42, "QUIT", ButtonAction::Quit);
        text(renderer_, fonts_.small(), "Esc or Space also resumes", 397, 507, Muted);
    }

    void drawResult() {
        SDL_Rect panel{260, 225, 440, 235};
        fillRect(renderer_, panel, Color{9, 20, 27});
        outlineRect(renderer_, panel, sim_.winner() == 0 ? Cyan : Danger);
        text(renderer_, fonts_.title(), sim_.winner() == 0 ? "VICTORY" : "DEFEAT", 405, 255,
             sim_.winner() == 0 ? Cyan : Danger);
        text(renderer_, fonts_.normal(), "The opposing Anchor has fallen.", 352, 305, Ink);
        addButton(365, 350, 230, 42, "REMATCH [R]", ButtonAction::Restart);
        addButton(365, 406, 230, 36, "QUIT", ButtonAction::Quit);
    }

    SDL_Renderer* renderer_;
    Fonts& fonts_;
    std::filesystem::path savePath_;
    Simulation sim_;
    Camera camera_;
    std::unordered_set<Id> selected_;
    std::vector<Button> buttons_;
    std::string status_;
    bool menu_ = true, paused_ = false, quit_ = false, buildTab_ = false;
    bool placement_ = false, attackMove_ = false, dragging_ = false;
    Kind placementKind_ = Kind::Foundry;
    SDL_Point dragStart_{}, dragCurrent_{};
};

std::filesystem::path executableDirectory(const char* executable) {
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(executable, error);
    return error ? std::filesystem::current_path() : path.parent_path();
}

std::filesystem::path dataDirectory() {
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local/share/cinderline";
    return std::filesystem::current_path();
}

} // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    std::filesystem::path capturePath;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--smoke-test") smoke = true;
        else if (argument == "--capture" && i + 1 < argc) capturePath = argv[++i];
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        SDL_Log("SDL_ttf initialization failed: %s", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Cinderline", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           WindowWidth, WindowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_Log("Renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, WindowWidth, WindowHeight);

    std::filesystem::path assets = executableDirectory(argv[0]) / "../share";
    if (const char* overridePath = std::getenv("CINDERLINE_ASSET_DIR")) assets = overridePath;
    Fonts fonts;
    if (!fonts.open(assets)) {
        SDL_Log("Could not load %s/DejaVuSans.ttf: %s", assets.c_str(), TTF_GetError());
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); fonts.close(); TTF_Quit(); SDL_Quit();
        return 1;
    }

    Game game(renderer, fonts, dataDirectory() / "skirmish.cinder");
    if (smoke || !capturePath.empty()) game.start();
    bool running = true;
    Uint64 previous = SDL_GetPerformanceCounter();
    int smokeFrames = 0;
    while (running && !game.quitRequested()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            else game.event(event);
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const float seconds = static_cast<float>(now - previous) / SDL_GetPerformanceFrequency();
        previous = now;
        game.moveCamera(seconds, SDL_GetKeyboardState(nullptr));
        game.update(smoke ? 0.05f : seconds);
        game.draw();
        ++smokeFrames;
        if (!capturePath.empty() && smokeFrames == 2) {
            if (!game.capture(capturePath)) {
                SDL_Log("Screenshot capture failed: %s", SDL_GetError());
                running = false;
            } else running = false;
        }
        if (smoke && smokeFrames >= 120) running = false;
        if (smoke) SDL_Delay(1);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    fonts.close();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
